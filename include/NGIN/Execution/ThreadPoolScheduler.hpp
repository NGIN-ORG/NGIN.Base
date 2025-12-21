/// <summary>
/// Thread-pool-based scheduler implementation for NGIN::Execution::IScheduler (header-only).
/// </summary>
#pragma once

#include "IScheduler.hpp"
#include "WorkItem.hpp"
#include <algorithm>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <utility>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Execution
{
    /// <summary>
    /// Scheduler that dispatches coroutines onto a pool of worker threads.
    /// </summary>
    class ThreadPoolScheduler : public IScheduler
    {
    public:
        /// <summary>
        /// Construct a thread pool with the given number of threads.
        /// </summary>
        explicit ThreadPoolScheduler(size_t threadCount = std::thread::hardware_concurrency())
            : m_stop(false)
        {
            if (threadCount == 0)
            {
                threadCount = 1;
            }
            for (size_t i = 0; i < threadCount; ++i)
            {
                m_threads.emplace_back([this] { WorkerLoop(); });
            }
            m_timerThread = std::thread([this] { TimerLoop(); });
        }

        /// <summary>
        /// Destructor - stops all workers and cleans up.
        /// </summary>
        ~ThreadPoolScheduler() override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_cv.notify_all();
            m_timerWake.NotifyAll();
            for (auto& t: m_threads)
            {
                if (t.joinable())
                    t.join();
            }
            if (m_timerThread.joinable())
            {
                m_timerThread.join();
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                while (!m_queue.empty())
                    m_queue.pop();
            }
            {
                std::lock_guard<std::mutex> lock(m_timersMutex);
                m_timerHeap.clear();
            }
        }

        void Execute(WorkItem item) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(std::move(item));
            }
            m_cv.notify_one();
        }

        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt)
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            if (resumeAt <= now)
            {
                Execute(std::move(item));
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_timersMutex);
                m_timerHeap.emplace_back(resumeAt, std::move(item));
                std::push_heap(m_timerHeap.begin(), m_timerHeap.end(), TimerEntryCompare {});
            }
            m_timerWake.NotifyOne();
        }

        void Schedule(std::coroutine_handle<> coro) noexcept override
        {
            Execute(WorkItem(coro));
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) override
        {
            ExecuteAt(WorkItem(coro), resumeAt);
        }


        bool RunOne() noexcept override
        {
            WorkItem work;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_queue.empty())
                    return false;
                work = std::move(m_queue.front());
                m_queue.pop();
            }
            work.Invoke();
            return true;
        }

        void RunUntilIdle() noexcept override
        {
            while (RunOne()) {}
        }

        void CancelAll() noexcept override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                while (!m_queue.empty())
                    m_queue.pop();
            }
            {
                std::lock_guard<std::mutex> timerLock(m_timersMutex);
                m_timerHeap.clear();
            }
            m_timerWake.NotifyAll();
        }

        void SetPriority(int priority) noexcept override
        {
            m_priority = priority;
        }

        void SetAffinity(uint64_t affinityMask) noexcept override
        {
            m_affinityMask = affinityMask;
            // Optionally: apply affinity to worker threads
        }

        void OnTaskStart(uint64_t, const char*) noexcept override {}
        void OnTaskSuspend(uint64_t) noexcept override {}
        void OnTaskResume(uint64_t) noexcept override {}
        void OnTaskComplete(uint64_t) noexcept override {}


    private:
        using TimerEntry = std::pair<NGIN::Time::TimePoint, WorkItem>;
        struct TimerEntryCompare
        {
            bool operator()(const TimerEntry& a, const TimerEntry& b) const noexcept
            {
                return a.first > b.first;
            }
        };

        void TimerLoop() noexcept
        {
            while (!m_stop.load(std::memory_order_acquire))
            {
                const auto observedWakeGeneration = m_timerWake.Load();
                std::vector<WorkItem> ready;
                NGIN::Time::TimePoint nextWakeAt {};
                bool hasNextWake = false;

                {
                    std::lock_guard<std::mutex> lock(m_timersMutex);
                    const auto now = NGIN::Time::MonotonicClock::Now();
                    while (!m_timerHeap.empty() && m_timerHeap.front().first <= now)
                    {
                        std::pop_heap(m_timerHeap.begin(), m_timerHeap.end(), TimerEntryCompare {});
                        ready.push_back(std::move(m_timerHeap.back().second));
                        m_timerHeap.pop_back();
                    }

                    if (!m_timerHeap.empty())
                    {
                        hasNextWake = true;
                        nextWakeAt  = m_timerHeap.front().first;
                    }
                }

                for (auto& item: ready)
                {
                    Execute(std::move(item));
                }

                if (m_stop.load(std::memory_order_acquire))
                {
                    break;
                }

                if (!hasNextWake)
                {
                    m_timerWake.Wait(observedWakeGeneration);
                    continue;
                }

                const auto now = NGIN::Time::MonotonicClock::Now();
                if (nextWakeAt <= now)
                {
                    continue;
                }

                const auto waitNs = nextWakeAt.ToNanoseconds() - now.ToNanoseconds();
                (void)m_timerWake.WaitFor(observedWakeGeneration, NGIN::Units::Nanoseconds(static_cast<double>(waitNs)));
            }
        }

        void WorkerLoop() noexcept
        {
            while (true)
            {
                WorkItem work;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
                    if (m_stop && m_queue.empty())
                        return;
                    work = std::move(m_queue.front());
                    m_queue.pop();
                }
                work.Invoke();
            }
        }

        std::vector<std::thread> m_threads;
        std::thread m_timerThread;
        std::queue<WorkItem> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_stop;
        int m_priority {0};
        uint64_t m_affinityMask {0};

        std::vector<TimerEntry> m_timerHeap;
        std::mutex m_timersMutex;
        NGIN::Sync::AtomicCondition m_timerWake;
    };

}// namespace NGIN::Execution
