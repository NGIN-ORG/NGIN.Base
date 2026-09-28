/// @file CooperativeScheduler.hpp
/// @brief Single-thread, cooperative executor with timers and RunUntilIdle pumping.
#pragma once

#include <NGIN/Execution/TimerRegistration.hpp>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include <NGIN/Execution/ScheduleResult.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <NGIN/Execution/detail/WorkBudget.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Execution
{
    /// @brief A single-threaded cooperative scheduler.
    ///
    /// This scheduler never spawns background threads. Work is executed only when the caller pumps the scheduler
    /// via `RunOne`/`RunUntilIdle`.
    class CooperativeScheduler final
    {
    public:
        /// @brief Constructs an empty scheduler with reserved queue storage.
        explicit CooperativeScheduler(std::size_t completionCapacity = 4096) : m_completions(completionCapacity)
        {
            m_ready.reserve(256);
            m_timers.reserve(256);
        }

        /// @brief Reserves a thread-safe terminal-delivery slot for a future pump call.
        /// @note All tickets and queued completions must drain before scheduler destruction.
        [[nodiscard]] std::expected<CompletionReservation, ScheduleError> ReserveCompletion(WorkItem item) noexcept
        {
            return m_completions.Reserve(std::move(item));
        }

        /// @brief Whether this thread is inside this scheduler's dispatch context.
        [[nodiscard]] bool IsCurrent() const noexcept { return s_currentScheduler == this; }

        /// @brief Queues a non-empty work item for execution by a future pump call.
        [[nodiscard]] ScheduleResult Execute(WorkItem item) noexcept
        {
            if (item.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            try
            {
                // Reuse consumed prefix storage without moving live work on every dequeue.
                if (m_readyHead >= 256 && m_readyHead >= m_ready.size() / 2)
                {
                    m_ready.erase(m_ready.begin(), m_ready.begin() + m_readyHead);
                    m_readyHead = 0;
                }
                m_ready.push_back(std::move(item));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
            return {};
        }

        /// @brief Queues a non-empty work item for execution no earlier than a time point.
        [[nodiscard]] ScheduleResult ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) noexcept
        {
            auto timer = ScheduleTimer(std::move(item), resumeAt);
            if (!timer)
                return std::unexpected(timer.error());
            timer->Detach();
            return {};
        }

        /// @brief Queues removable timed work; registration and cancellation are thread-safe.
        [[nodiscard]] std::expected<TimerRegistration, ScheduleError> ScheduleTimer(
                WorkItem item, NGIN::Time::TimePoint resumeAt) noexcept
        {
            if (item.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            std::uint64_t identifier;
            try
            {
                std::lock_guard lock(m_timerMutex);
                if (m_nextTimerIdentifier == (std::numeric_limits<std::uint64_t>::max)())
                    return std::unexpected(ScheduleError::ResourceExhausted);
                identifier = ++m_nextTimerIdentifier;
                m_timers.push_back(Timer {resumeAt, std::move(item), identifier});
                std::push_heap(m_timers.begin(), m_timers.end(), &Timer::IsEarlier);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            } catch (...)
            {
                return std::unexpected(ScheduleError::Rejected);
            }
            return TimerRegistration(this, resumeAt.ToNanoseconds(), identifier, +[](void* self, std::uint64_t, std::uint64_t id) noexcept { return static_cast<CooperativeScheduler*>(self)->CancelTimer(id); });
        }

        /// @brief Executes at most one due timer or ready item using the current monotonic time.
        /// @return `true` when an item was invoked.
        [[nodiscard]] bool RunOne()
        {
            return RunOneAt(NGIN::Time::MonotonicClock::Now());
        }

        /// @brief Executes one item, rotating fairly between reserved completions, due timers, and FIFO ready work.
        /// @return `true` when an item was invoked.
        [[nodiscard]] bool RunOneAt(NGIN::Time::TimePoint now)
        {
            detail::DispatchBudget dispatch;
            if (!dispatch.Entered())
                return false;
            struct Context
            {
                CooperativeScheduler* previous;
                ~Context() { s_currentScheduler = previous; }
            } context {s_currentScheduler};
            s_currentScheduler = this;
            // Rotate before invoking user work so a reentrant pump sees the next source.
            for (unsigned attempt = 0; attempt != 3; ++attempt)
            {
                const auto source = m_nextSource;
                m_nextSource      = (m_nextSource + 1) % 3;
                if (source == 0)
                {
                    if (m_completions.RunOne())
                    {
                        dispatch.Invoked();
                        return true;
                    }
                }
                else if (source == 1)
                {
                    WorkItem item;
                    {
                        std::lock_guard lock(m_timerMutex);
                        if (!m_timers.empty() && m_timers.front().resumeAt <= now)
                        {
                            std::pop_heap(m_timers.begin(), m_timers.end(), &Timer::IsEarlier);
                            item = std::move(m_timers.back().item);
                            m_timers.pop_back();
                        }
                    }
                    if (!item.IsEmpty())
                    {
                        dispatch.Invoked();
                        item.Invoke();
                        return true;
                    }
                }
                else if (m_readyHead != m_ready.size())
                {
                    WorkItem item = std::move(m_ready[m_readyHead++]);
                    if (m_readyHead == m_ready.size())
                    {
                        m_ready.clear();
                        m_readyHead = 0;
                    }
                    dispatch.Invoked();
                    item.Invoke();
                    return true;
                }
            }
            return false;
        }

        /// @brief Runs ready work and currently due timers until no item can execute.
        void RunUntilIdle()
        {
            while (RunOne()) {}
        }

        /// @brief Runs ready work and timers due at a supplied time until idle.
        void RunUntilIdleAt(NGIN::Time::TimePoint now)
        {
            while (RunOneAt(now)) {}
        }

        /// @brief Returns the number of immediately runnable items.
        [[nodiscard]] std::size_t PendingReady() const noexcept
        {
            return m_ready.size() - m_readyHead;
        }

        /// @brief Returns the number of scheduled timer items.
        [[nodiscard]] std::size_t PendingTimers() const noexcept
        {
            std::lock_guard lock(m_timerMutex);
            return m_timers.size();
        }

    private:
        struct Timer final
        {
            NGIN::Time::TimePoint resumeAt {};
            WorkItem              item {};
            std::uint64_t         identifier {};

            static bool IsEarlier(const Timer& a, const Timer& b) noexcept
            {
                return a.resumeAt > b.resumeAt;
            }
        };

        bool CancelTimer(std::uint64_t identifier) noexcept
        {
            WorkItem retired;
            {
                std::lock_guard lock(m_timerMutex);
                auto            found = std::find_if(m_timers.begin(), m_timers.end(),
                                                     [identifier](const Timer& timer) { return timer.identifier == identifier; });
                if (found == m_timers.end())
                    return false;
                retired = std::move(found->item);
                m_timers.erase(found);
                std::make_heap(m_timers.begin(), m_timers.end(), &Timer::IsEarlier);
            }
            return true;
        }

        mutable std::mutex                               m_timerMutex;
        std::uint64_t                                    m_nextTimerIdentifier {0};
        unsigned                                         m_nextSource {0};
        std::size_t                                      m_readyHead {0};
        std::vector<WorkItem>                            m_ready {};
        std::vector<Timer>                               m_timers {};
        detail::CompletionQueue                          m_completions;
        static inline thread_local CooperativeScheduler* s_currentScheduler {};
    };
}// namespace NGIN::Execution
