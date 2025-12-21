/// <summary>
/// Fiber/thread hybrid scheduler for NGIN::Execution::IScheduler (Windows-only, cooperative).
/// </summary>
#pragma once

#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <coroutine>
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>// For logging, can remove if not desired
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>
#include "IScheduler.hpp"
#include "Fiber.hpp"
#include "WorkItem.hpp"

namespace NGIN::Execution
{
    class FiberScheduler : public IScheduler
    {
    private:
        constexpr static UIntSize DEFAULT_NUM_FIBERS  = 128;
        constexpr static UIntSize DEFAULT_NUM_THREADS = 4;

    public:
        using time_point = NGIN::Time::TimePoint;

        FiberScheduler(size_t numThreads = DEFAULT_NUM_THREADS, size_t numFibers = DEFAULT_NUM_FIBERS)
            : m_stop(false)
        {
            const auto effectiveThreads = numThreads == 0 ? static_cast<size_t>(DEFAULT_NUM_THREADS) : numThreads;
            const auto effectiveFibers  = numFibers == 0 ? static_cast<size_t>(DEFAULT_NUM_FIBERS) : numFibers;
            m_fibersPerThread           = (effectiveFibers + effectiveThreads - 1) / effectiveThreads;
            if (m_fibersPerThread == 0)
            {
                m_fibersPerThread = 1;
            }

            // Launch worker threads
            for (size_t i = 0; i < effectiveThreads; ++i)
                m_threads.emplace_back([this] { WorkerLoop(); });

            // Launch driver thread (for time-based scheduling)
            m_driverThread = std::thread([this] { DriverLoop(); });
        }

        ~FiberScheduler() override
        {
            m_stop.store(true);
            CancelAll();
            m_readyCv.notify_all();// Wake up all threads exactly once


            // Join worker threads
            for (auto& t: m_threads)
                if (t.joinable())
                    t.join();

            // Join the driver thread
            if (m_driverThread.joinable())
                m_driverThread.join();

            // Clean up sleeping tasks
            {
                std::lock_guard lock(m_timersMutex);
                m_timerHeap.clear();
            }
        }

        void Execute(WorkItem item) noexcept
        {
            {
                std::lock_guard lock(m_readyMutex);
                m_readyQueue.push(std::move(item));
            }
            m_readyCv.notify_one();
        }

        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt)
        {
            {
                std::lock_guard lock(m_timersMutex);
                m_timerHeap.emplace_back(resumeAt, std::move(item));
                std::push_heap(m_timerHeap.begin(), m_timerHeap.end(), SleepEntryCompare {});
            }
        }


        void Schedule(std::coroutine_handle<> coro) noexcept override
        {
            Execute(WorkItem(coro));
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) override
        {
            ExecuteAt(WorkItem(coro), resumeAt);
        }

        bool RunOne() override
        {
            // Not needed: scheduler runs automatically.
            return false;
        }

        void RunUntilIdle() noexcept override
        {
            // Not needed: scheduler runs automatically.
        }

        void CancelAll() noexcept override
        {
            {
                std::lock_guard lock(m_readyMutex);
                std::queue<WorkItem> empty;
                std::swap(m_readyQueue, empty);
            }
            {
                std::lock_guard lock(m_timersMutex);
                m_timerHeap.clear();
            }
        }

        void SetPriority(int priority) noexcept override
        {
            m_priority = priority;
        }
        void SetAffinity(uint64_t affinityMask) noexcept override
        {
            m_affinityMask = affinityMask;
        }
        void OnTaskStart(uint64_t, const char*) noexcept override {}
        void OnTaskSuspend(uint64_t) noexcept override {}
        void OnTaskResume(uint64_t) noexcept override {}
        void OnTaskComplete(uint64_t) noexcept override {}

    private:
        std::atomic<bool> m_stop {false};
        int m_priority {0};
        uint64_t m_affinityMask {0};

        size_t m_fibersPerThread {1};

        // Worker threads
        std::vector<std::thread> m_threads;
        // Driver thread (timer pump)
        std::thread m_driverThread;

        // Ready queue
        std::queue<WorkItem> m_readyQueue;
        std::mutex m_readyMutex;
        std::condition_variable m_readyCv;

        // Delayed tasks
        using SleepEntry = std::pair<time_point, WorkItem>;
        struct SleepEntryCompare
        {
            bool operator()(const SleepEntry& a, const SleepEntry& b) const
            {
                return a.first > b.first;// Min-heap: soonest first
            }
        };
        std::vector<SleepEntry> m_timerHeap;
        std::mutex m_timersMutex;

        // Timer management
        void CheckSleepingTasks()
        {
            auto now = NGIN::Time::MonotonicClock::Now();
            std::vector<WorkItem> expired;
            {
                std::lock_guard lock(m_timersMutex);
                while (!m_timerHeap.empty() && m_timerHeap.front().first <= now)
                {
                    std::pop_heap(m_timerHeap.begin(), m_timerHeap.end(), SleepEntryCompare {});
                    expired.push_back(std::move(m_timerHeap.back().second));
                    m_timerHeap.pop_back();
                }
            }
            for (auto& item: expired)
            {
                Execute(std::move(item));
            }
        }

        // Driver thread: manages timers/delays
        void DriverLoop()
        {
            while (!m_stop)
            {
                NGIN::Units::Milliseconds nextSleep {5.0};
                bool hasSleeping                    = false;

                {
                    std::lock_guard lock(m_timersMutex);
                    if (!m_timerHeap.empty())
                    {
                        hasSleeping   = true;
                        auto now      = NGIN::Time::MonotonicClock::Now();
                        auto resumeAt = m_timerHeap.front().first;
                        if (resumeAt > now)
                        {
                            const auto deltaNs = resumeAt.ToNanoseconds() - now.ToNanoseconds();
                            nextSleep          = NGIN::Units::Milliseconds(static_cast<double>(deltaNs) / 1'000'000.0);
                        }
                        else
                            nextSleep = NGIN::Units::Milliseconds {0.0};
                    }
                }

                // Check for sleeping tasks to wake up
                CheckSleepingTasks();
                m_readyCv.notify_all();

                // Yield if no sleeping tasks, or if nextSleep is zero; otherwise sleep
                if (!hasSleeping || nextSleep.GetValue() == 0.0)
                    std::this_thread::yield();
                else
                    NGIN::Time::SleepFor(nextSleep);
            }
        }

        // Worker threads: each pulls a ready coroutine, runs it on a fiber, returns fiber to pool
        void WorkerLoop()
        {
            // Convert this thread to a fiber (for cooperative scheduling)
            Fiber::EnsureMainFiber();

            std::vector<std::unique_ptr<Fiber>> fibers;
            fibers.reserve(m_fibersPerThread);
            std::vector<Fiber*> fiberPool;
            fiberPool.reserve(m_fibersPerThread);
            for (size_t i = 0; i < m_fibersPerThread; ++i)
            {
                auto fiber = std::make_unique<Fiber>();
                fiberPool.push_back(fiber.get());
                fibers.push_back(std::move(fiber));
            }

            while (!m_stop.load())
            {
                WorkItem work;
                {
                    std::unique_lock lock(m_readyMutex);
                    if (m_stop.load())
                        break;
                    m_readyCv.wait(lock, [this] {
                        return m_stop.load() || !m_readyQueue.empty();
                    });
                    if (m_stop.load())
                        break;
                    if (!m_readyQueue.empty())
                    {
                        work = std::move(m_readyQueue.front());
                        m_readyQueue.pop();
                    }
                }

                if (work.IsEmpty())
                    continue;

                Fiber* fiber = nullptr;
                if (fiberPool.empty())
                {
                    // Defensive fallback: should not happen; run directly rather than deadlocking.
                    work.Invoke();
                    continue;
                }
                fiber = fiberPool.back();
                fiberPool.pop_back();

                // Defensive: must be assignable (not running)
                try
                {
                    fiber->Assign([work = std::move(work)]() mutable { work.Invoke(); });
                } catch (const std::exception& ex)
                {
                    // Should never happen unless fiber is misused, but handle gracefully
                    std::cerr << "[FiberScheduler] Fiber assign failed: " << ex.what() << std::endl;
                    fiberPool.push_back(fiber);
                    continue;
                }

                try
                {
                    fiber->Resume();
                } catch (const std::exception& ex)
                {
                    std::cerr << "[FiberScheduler] Exception in fiber: " << ex.what() << std::endl;
                }
                // Return fiber to pool
                fiberPool.push_back(fiber);
            }
        }

        FiberScheduler(const FiberScheduler&)            = delete;
        FiberScheduler& operator=(const FiberScheduler&) = delete;
    };
}// namespace NGIN::Execution
