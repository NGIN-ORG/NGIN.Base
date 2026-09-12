/// <summary>
/// Thread-pool-based scheduler implementation for NGIN::Execution::IScheduler (header-only).
/// </summary>
#pragma once

#include "WorkItem.hpp"
#include <NGIN/Execution/ScheduleResult.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Sync/SpinLock.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

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
        explicit ThreadPoolScheduler(size_t threadCount        = static_cast<size_t>(ThisThread::HardwareConcurrency()),
                                     size_t completionCapacity = 4096)
            : m_stop(false), m_completions(completionCapacity, +[](void* state) noexcept { static_cast<ThreadPoolScheduler*>(state)->m_workWake.NotifyAll(); }, this)
        {
            if (threadCount == 0)
            {
                threadCount = 1;
            }
            m_workers.resize(threadCount);
            m_threads.reserve(threadCount);

            try
            {
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
            } catch (...)
            {
                m_stop.store(true, std::memory_order_release);
                m_workWake.NotifyAll();
                m_timerWake.NotifyAll();
                for (auto& thread: m_threads)
                    if (thread.IsJoinable())
                        thread.Join();
                if (m_timerThread.IsJoinable())
                    m_timerThread.Join();
                throw;
            }
        }

        /// <summary>
        /// Destructor - stops all workers and cleans up.
        /// </summary>
        ~ThreadPoolScheduler()
        {
            m_completions.Close();
            m_stop.store(true, std::memory_order_release);
            // Retiring an accepted cancellation-aware timer can publish its
            // reserved terminal delivery. Do this before joining the workers.
            ClearAllWork();
            {
                std::lock_guard<std::mutex> lock(m_timersMutex);
                m_timerHeap.clear();
            }
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
        }

        /// @brief Reserves terminal delivery independently of ordinary work queues.
        /// @details Destruction waits for outstanding tickets to be released or delivered.
        /// A ticket may be dispatched during destruction; its work runs on a pool worker.
        [[nodiscard]] std::expected<CompletionReservation, ScheduleError> ReserveCompletion(WorkItem item) noexcept
        {
            return m_completions.Reserve(std::move(item));
        }

        /// @brief Whether the calling thread is currently dispatching on this executor.
        [[nodiscard]] bool IsCurrent() const noexcept { return s_currentScheduler == this; }

        /// @brief Queues work for a local worker or the shared injection queue.
        [[nodiscard]] ScheduleResult Execute(WorkItem item) noexcept
        {
            if (item.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            if (m_stop.load(std::memory_order_acquire))
                return std::unexpected(ScheduleError::Stopped);

            ScheduleResult result;
            if (s_currentScheduler == this && s_workerIndex < m_workers.size())
            {
                result = m_workers[s_workerIndex].Push(std::move(item));
            }
            else
            {
                result = EnqueueToInjection(std::move(item));
            }
            if (result)
                m_workWake.NotifyOne();
            return result;
        }

        /// @brief Queues work for execution no earlier than a monotonic time point.
        [[nodiscard]] ScheduleResult ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) noexcept
        {
            if (item.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            if (m_stop.load(std::memory_order_acquire))
                return std::unexpected(ScheduleError::Stopped);
            const NGIN::Time::TimePoint now = NGIN::Time::MonotonicClock::Now();
            if (resumeAt <= now)
                return Execute(std::move(item));
            try
            {
                std::lock_guard<std::mutex> lock(m_timersMutex);
                if (m_stop.load(std::memory_order_acquire))
                    return std::unexpected(ScheduleError::Stopped);
                m_timerHeap.emplace_back(resumeAt, std::move(item));
                std::push_heap(m_timerHeap.begin(), m_timerHeap.end(), TimerEntryCompare {});
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            } catch (...)
            {
                return std::unexpected(ScheduleError::Rejected);
            }
            m_timerWake.NotifyOne();
            return {};
        }

        /// @brief Executes at most one available work item on the calling thread.
        /// @return `true` when an item was invoked.
        bool RunOne() noexcept
        {
            DispatchContext context(this);
            size_t          cursor = m_manualSourceCursor.fetch_add(1, std::memory_order_relaxed) % 4;
            return TryRunOne(s_workerIndex, cursor);
        }

        /// @brief Executes available work on the calling thread until none remains.
        void RunUntilIdle() noexcept
        {
            while (RunOne()) {}
        }

        /// @brief Discards queued and timed work without interrupting running work.
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

        /// @brief Stores a priority hint for scheduler policy.
        void SetPriority(int priority) noexcept
        {
            m_priority = priority;
        }

        /// @brief Stores an affinity hint for scheduler policy.
        void SetAffinity(uint64_t affinityMask) noexcept
        {
            m_affinityMask = affinityMask;
            // Optionally: apply affinity to worker threads
        }

        /// @brief Receives a task-start notification; currently no metrics are recorded.
        void OnTaskStart(uint64_t, const char*) noexcept {}
        /// @brief Receives a task-suspend notification; currently no metrics are recorded.
        void OnTaskSuspend(uint64_t) noexcept {}
        /// @brief Receives a task-resume notification; currently no metrics are recorded.
        void OnTaskResume(uint64_t) noexcept {}
        /// @brief Receives a task-complete notification; currently no metrics are recorded.
        void OnTaskComplete(uint64_t) noexcept {}


    private:
        struct DispatchContext
        {
            explicit DispatchContext(ThreadPoolScheduler* scheduler) noexcept
                : previous(s_currentScheduler), previousIndex(s_workerIndex)
            {
                if (s_currentScheduler != scheduler)
                {
                    s_currentScheduler = scheduler;
                    s_workerIndex      = static_cast<size_t>(-1);
                }
            }
            ~DispatchContext()
            {
                s_currentScheduler = previous;
                s_workerIndex      = previousIndex;
            }
            ThreadPoolScheduler* previous;
            size_t               previousIndex;
        };
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
            NGIN::Sync::SpinLock  lock {};
            std::vector<WorkItem> items {};
            size_t                head {0};

            WorkerQueue()                              = default;
            WorkerQueue(const WorkerQueue&)            = delete;
            WorkerQueue& operator=(const WorkerQueue&) = delete;
            WorkerQueue(WorkerQueue&& other) noexcept
                : items(std::move(other.items)), head(other.head)
            {
                other.head = 0;
            }
            WorkerQueue& operator=(WorkerQueue&& other) noexcept
            {
                if (this != &other)
                {
                    items      = std::move(other.items);
                    head       = other.head;
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

            [[nodiscard]] ScheduleResult Push(WorkItem&& item) noexcept
            {
                try
                {
                    std::lock_guard guard(lock);
                    items.push_back(std::move(item));
                    return {};
                } catch (const std::bad_alloc&)
                {
                    return std::unexpected(ScheduleError::ResourceExhausted);
                } catch (...)
                {
                    return std::unexpected(ScheduleError::Rejected);
                }
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
        static inline thread_local size_t               s_workerIndex      = static_cast<size_t>(-1);

        void ClearAllWork() noexcept
        {
            m_injection.Clear();
            for (auto& w: m_workers)
            {
                w.Clear();
            }
        }

        [[nodiscard]] ScheduleResult EnqueueToInjection(WorkItem&& item) noexcept
        {
            return m_injection.Push(std::move(item));
        }

        /// Timed work was already accepted by ExecuteAt. If the ready queue is
        /// exhausted later, execute it on the timer thread instead of silently
        /// dropping an accepted item.
        void DispatchAcceptedTimer(WorkItem item) noexcept
        {
            if (m_stop.load(std::memory_order_acquire))
                return;

            const ScheduleResult result = EnqueueToInjection(std::move(item));
            if (result)
            {
                m_workWake.NotifyOne();
            }
            else if (!item.IsEmpty() && !m_stop.load(std::memory_order_acquire))
            {
                item.Invoke();
            }
        }

        [[nodiscard]] WorkItem TryDequeueInjection() noexcept
        {
            return m_injection.TrySteal();
        }

        // Rotate between completion delivery, external injection, and both ends
        // of the local deque. New local work retains a locality-friendly turn,
        // while old local/injected work cannot be displaced forever by YieldNow.
        [[nodiscard]] bool TryRunOne(size_t index, size_t& sourceCursor) noexcept
        {
            for (size_t attempt = 0; attempt != 4; ++attempt)
            {
                const size_t source = sourceCursor;
                sourceCursor        = (sourceCursor + 1) % 4;
                WorkItem work;
                switch (source)
                {
                    case 0:
                        if (m_completions.RunOne())
                            return true;
                        break;
                    case 1:
                        work = TryDequeueInjection();
                        break;
                    case 2:
                        if (index < m_workers.size())
                            work = m_workers[index].TrySteal();
                        break;
                    case 3:
                        if (index < m_workers.size())
                            work = m_workers[index].TryPop();
                        break;
                }
                if (!work.IsEmpty())
                {
                    work.Invoke();
                    return true;
                }
            }
            const size_t first = index < m_workers.size() ? index + 1 : 0;
            for (size_t offset = 0; offset < m_workers.size(); ++offset)
            {
                const size_t victim = (first + offset) % m_workers.size();
                if (victim == index)
                    continue;
                if (auto stolen = m_workers[victim].TrySteal(); !stolen.IsEmpty())
                {
                    stolen.Invoke();
                    return true;
                }
            }
            return false;
        }

        void TimerLoop() noexcept
        {
            while (!m_stop.load(std::memory_order_acquire))
            {
                const auto            observedWakeGeneration = m_timerWake.Load();
                std::vector<WorkItem> ready;
                WorkItem              allocationFallback;
                NGIN::Time::TimePoint nextWakeAt {};
                bool                  hasNextWake = false;

                {
                    std::lock_guard<std::mutex> lock(m_timersMutex);
                    const auto                  now = NGIN::Time::MonotonicClock::Now();
                    while (!m_timerHeap.empty() && m_timerHeap.front().first <= now)
                    {
                        std::pop_heap(m_timerHeap.begin(), m_timerHeap.end(), TimerEntryCompare {});
                        try
                        {
                            ready.push_back(std::move(m_timerHeap.back().second));
                        } catch (...)
                        {
                            allocationFallback = std::move(m_timerHeap.back().second);
                        }
                        m_timerHeap.pop_back();
                        if (!allocationFallback.IsEmpty())
                            break;
                    }

                    if (!m_timerHeap.empty())
                    {
                        hasNextWake = true;
                        nextWakeAt  = m_timerHeap.front().first;
                    }
                }

                for (auto& item: ready)
                {
                    DispatchAcceptedTimer(std::move(item));
                }
                if (!allocationFallback.IsEmpty())
                    DispatchAcceptedTimer(std::move(allocationFallback));

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
                (void) m_timerWake.WaitFor(observedWakeGeneration, NGIN::Units::Nanoseconds(static_cast<double>(waitNs)));
            }
        }

        void WorkerLoop(size_t index) noexcept
        {
            s_currentScheduler  = this;
            s_workerIndex       = index;
            size_t sourceCursor = 0;

            for (;;)
            {
                if (m_stop.load(std::memory_order_acquire) && m_completions.Outstanding() == 0)
                {
                    break;
                }

                if (TryRunOne(index, sourceCursor))
                    continue;

                const auto observedWakeGeneration = m_workWake.Load();
                if (TryRunOne(index, sourceCursor))
                    continue;
                if (m_stop.load(std::memory_order_acquire) && m_completions.Outstanding() == 0)
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

        NGIN::Sync::AtomicCondition m_workWake;

        std::atomic<size_t>     m_manualSourceCursor {0};
        std::atomic<bool>       m_stop;
        detail::CompletionQueue m_completions;
        int                     m_priority {0};
        uint64_t                m_affinityMask {0};

        std::vector<TimerEntry>     m_timerHeap;
        std::mutex                  m_timersMutex;
        NGIN::Sync::AtomicCondition m_timerWake;
    };

}// namespace NGIN::Execution
