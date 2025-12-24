/// <summary>
/// Thread-pool-based scheduler implementation for NGIN::Execution::IScheduler (header-only).
/// </summary>
#pragma once

#include "WorkItem.hpp"
#include <NGIN/Execution/Thread.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <mutex>
#include <atomic>
#include <utility>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Sync/SpinLock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Execution
{
    /// <summary>
    /// Scheduler that dispatches coroutines onto a pool of worker threads.
    /// </summary>
    class ThreadPoolScheduler
    {
    public:
        /// <summary>
        /// Construct a thread pool with the given number of threads.
        /// </summary>
        explicit ThreadPoolScheduler(size_t threadCount = static_cast<size_t>(ThisThread::HardwareConcurrency()))
            : m_stop(false)
        {
            if (threadCount == 0)
            {
                threadCount = 1;
            }
            m_workers.resize(threadCount);

            for (size_t i = 0; i < threadCount; ++i)
            {
                Thread::Options options {};
                options.name = MakeIndexedThreadName("NGIN.TPW", i);
                m_threads.emplace_back([this, i] { WorkerLoop(i); }, options);
            }
            {
                Thread::Options options {};
                options.name = ThreadName("NGIN.TPT");
                m_timerThread.Start([this] { TimerLoop(); }, options);
            }
        }

        /// <summary>
        /// Destructor - stops all workers and cleans up.
        /// </summary>
        ~ThreadPoolScheduler()
        {
            m_stop.store(true, std::memory_order_release);
            m_workWake.NotifyAll();
            m_timerWake.NotifyAll();
            for (auto& t: m_threads)
            {
                if (t.IsJoinable())
                {
                    t.Join();
                }
            }
            if (m_timerThread.IsJoinable())
            {
                m_timerThread.Join();
            }
            ClearAllWork();
            {
                std::lock_guard<std::mutex> lock(m_timersMutex);
                m_timerHeap.clear();
            }
        }

        void Execute(WorkItem item) noexcept
        {
            if (TryEnqueueToLocal(item))
            {
                m_workWake.NotifyOne();
                return;
            }
            EnqueueToInjection(std::move(item));
            m_workWake.NotifyOne();
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

        void Schedule(std::coroutine_handle<> coro) noexcept
        {
            Execute(WorkItem(coro));
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt)
        {
            ExecuteAt(WorkItem(coro), resumeAt);
        }


        bool RunOne() noexcept
        {
            WorkItem work = TryDequeueAny();
            if (work.IsEmpty())
            {
                return false;
            }
            work.Invoke();
            return true;
        }

        void RunUntilIdle() noexcept
        {
            while (RunOne()) {}
        }

        void CancelAll() noexcept
        {
            ClearAllWork();
            {
                std::lock_guard<std::mutex> timerLock(m_timersMutex);
                m_timerHeap.clear();
            }
            m_workWake.NotifyAll();
            m_timerWake.NotifyAll();
        }

        void SetPriority(int priority) noexcept
        {
            m_priority = priority;
        }

        void SetAffinity(uint64_t affinityMask) noexcept
        {
            m_affinityMask = affinityMask;
            // Optionally: apply affinity to worker threads
        }

        void OnTaskStart(uint64_t, const char*) noexcept {}
        void OnTaskSuspend(uint64_t) noexcept {}
        void OnTaskResume(uint64_t) noexcept {}
        void OnTaskComplete(uint64_t) noexcept {}


    private:
        static ThreadName MakeIndexedThreadName(std::string_view prefix, std::size_t index) noexcept
        {
            std::array<char, ThreadName::MaxBytes + 1> buffer {};
            const auto                                 prefixLen = std::min<std::size_t>(prefix.size(), ThreadName::MaxBytes);
            for (std::size_t i = 0; i < prefixLen; ++i)
            {
                buffer[i] = prefix[i];
            }

            std::size_t pos = prefixLen;
            if (pos < ThreadName::MaxBytes)
            {
                buffer[pos++] = '.';
            }

            std::array<char, 24> digits {};
            std::size_t          digitCount = 0;
            auto                 value      = index;
            do
            {
                digits[digitCount++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0 && digitCount < digits.size());

            while (digitCount > 0 && pos < ThreadName::MaxBytes)
            {
                buffer[pos++] = digits[--digitCount];
            }
            buffer[pos] = '\0';

            return ThreadName(std::string_view(buffer.data(), pos));
        }

        struct WorkerQueue
        {
            NGIN::Sync::SpinLock lock {};
            std::vector<WorkItem> items {};
            size_t head {0};

            WorkerQueue()                                = default;
            WorkerQueue(const WorkerQueue&)              = delete;
            WorkerQueue& operator=(const WorkerQueue&)   = delete;
            WorkerQueue(WorkerQueue&& other) noexcept
                : items(std::move(other.items))
                , head(other.head)
            {
                other.head = 0;
            }
            WorkerQueue& operator=(WorkerQueue&& other) noexcept
            {
                if (this != &other)
                {
                    items = std::move(other.items);
                    head  = other.head;
                    other.head = 0;
                }
                return *this;
            }

            void Clear() noexcept
            {
                std::lock_guard guard(lock);
                items.clear();
                head = 0;
            }

            void Push(WorkItem item) noexcept
            {
                std::lock_guard guard(lock);
                items.push_back(std::move(item));
            }

            [[nodiscard]] WorkItem TryPop() noexcept
            {
                std::lock_guard guard(lock);
                if (items.size() <= head)
                {
                    return {};
                }
                WorkItem out = std::move(items.back());
                items.pop_back();
                if (items.size() <= head)
                {
                    items.clear();
                    head = 0;
                }
                return out;
            }

            [[nodiscard]] WorkItem TrySteal() noexcept
            {
                std::lock_guard guard(lock);
                if (items.size() <= head)
                {
                    return {};
                }
                WorkItem out = std::move(items[head]);
                ++head;
                if (head >= items.size())
                {
                    items.clear();
                    head = 0;
                }
                else if (head > 1024 && head * 2 > items.size())
                {
                    // Compact occasionally to bound memory usage.
                    auto write = items.begin();
                    for (auto read = items.begin() + static_cast<std::ptrdiff_t>(head); read != items.end(); ++read)
                    {
                        *write++ = std::move(*read);
                    }
                    items.erase(write, items.end());
                    head = 0;
                }
                return out;
            }
        };

        using TimerEntry = std::pair<NGIN::Time::TimePoint, WorkItem>;
        struct TimerEntryCompare
        {
            bool operator()(const TimerEntry& a, const TimerEntry& b) const noexcept
            {
                return a.first > b.first;
            }
        };

        static inline thread_local ThreadPoolScheduler* s_currentScheduler = nullptr;
        static inline thread_local size_t s_workerIndex                    = static_cast<size_t>(-1);

        void ClearAllWork() noexcept
        {
            {
                std::lock_guard guard(m_injectionLock);
                m_injection.items.clear();
                m_injection.head = 0;
            }
            for (auto& w: m_workers)
            {
                w.Clear();
            }
        }

        void EnqueueToInjection(WorkItem item) noexcept
        {
            std::lock_guard guard(m_injectionLock);
            m_injection.items.push_back(std::move(item));
        }

        [[nodiscard]] WorkItem TryDequeueInjection() noexcept
        {
            std::lock_guard guard(m_injectionLock);
            if (m_injection.items.size() <= m_injection.head)
            {
                return {};
            }
            WorkItem out = std::move(m_injection.items[m_injection.head]);
            ++m_injection.head;
            if (m_injection.head >= m_injection.items.size())
            {
                m_injection.items.clear();
                m_injection.head = 0;
            }
            return out;
        }

        [[nodiscard]] bool TryEnqueueToLocal(WorkItem& item) noexcept
        {
            if (s_currentScheduler != this)
            {
                return false;
            }
            if (s_workerIndex >= m_workers.size())
            {
                return false;
            }
            m_workers[s_workerIndex].Push(std::move(item));
            return true;
        }

        [[nodiscard]] WorkItem TryDequeueAny() noexcept
        {
            if (s_currentScheduler == this && s_workerIndex < m_workers.size())
            {
                if (auto local = m_workers[s_workerIndex].TryPop(); !local.IsEmpty())
                {
                    return local;
                }
            }
            if (auto injected = TryDequeueInjection(); !injected.IsEmpty())
            {
                return injected;
            }
            if (s_currentScheduler == this && !m_workers.empty())
            {
                const size_t self = s_workerIndex < m_workers.size() ? s_workerIndex : 0;
                for (size_t offset = 1; offset < m_workers.size(); ++offset)
                {
                    const size_t victim = (self + offset) % m_workers.size();
                    if (auto stolen = m_workers[victim].TrySteal(); !stolen.IsEmpty())
                    {
                        return stolen;
                    }
                }
            }
            return {};
        }

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

        void WorkerLoop(size_t index) noexcept
        {
            s_currentScheduler = this;
            s_workerIndex      = index;

            for (;;)
            {
                if (m_stop.load(std::memory_order_acquire))
                {
                    break;
                }

                WorkItem work = TryDequeueAny();
                if (!work.IsEmpty())
                {
                    work.Invoke();
                    continue;
                }

                const auto observedWakeGeneration = m_workWake.Load();
                work = TryDequeueAny();
                if (!work.IsEmpty())
                {
                    work.Invoke();
                    continue;
                }
                if (m_stop.load(std::memory_order_acquire))
                {
                    break;
                }
                m_workWake.Wait(observedWakeGeneration);
            }

            s_currentScheduler = nullptr;
            s_workerIndex      = static_cast<size_t>(-1);
        }

        std::vector<WorkerThread> m_threads;
        WorkerThread              m_timerThread;

        std::vector<WorkerQueue> m_workers;

        // Injection queue for external producers (and timer thread).
        WorkerQueue m_injection;
        NGIN::Sync::SpinLock m_injectionLock {};

        NGIN::Sync::AtomicCondition m_workWake;

        std::atomic<bool> m_stop;
        int m_priority {0};
        uint64_t m_affinityMask {0};

        std::vector<TimerEntry> m_timerHeap;
        std::mutex m_timersMutex;
        NGIN::Sync::AtomicCondition m_timerWake;
    };

}// namespace NGIN::Execution
