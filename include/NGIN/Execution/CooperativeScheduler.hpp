/// @file CooperativeScheduler.hpp
/// @brief Single-thread, cooperative executor with timers and RunUntilIdle pumping.
#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <NGIN/Execution/WorkItem.hpp>
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
        CooperativeScheduler()
        {
            m_ready.reserve(256);
            m_timers.reserve(256);
        }

        /// @brief Queues a non-empty work item for execution by a future pump call.
        void Execute(WorkItem item) noexcept
        {
            if (!item.IsEmpty())
            {
                m_ready.push_back(std::move(item));
            }
        }

        /// @brief Queues a non-empty work item for execution no earlier than a time point.
        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt)
        {
            if (item.IsEmpty())
            {
                return;
            }

            m_timers.push_back(Timer {resumeAt, std::move(item)});
            std::push_heap(m_timers.begin(), m_timers.end(), &Timer::IsEarlier);
        }

        /// @brief Executes at most one due timer or ready item using the current monotonic time.
        /// @return `true` when an item was invoked.
        [[nodiscard]] bool RunOne()
        {
            return RunOneAt(NGIN::Time::MonotonicClock::Now());
        }

        /// @brief Executes at most one timer due at `now`, otherwise one ready item.
        /// @return `true` when an item was invoked.
        [[nodiscard]] bool RunOneAt(NGIN::Time::TimePoint now)
        {
            if (!m_timers.empty())
            {
                Timer& next = m_timers.front();
                if (next.resumeAt <= now)
                {
                    std::pop_heap(m_timers.begin(), m_timers.end(), &Timer::IsEarlier);
                    WorkItem item = std::move(m_timers.back().item);
                    m_timers.pop_back();
                    item.Invoke();
                    return true;
                }
            }

            if (!m_ready.empty())
            {
                WorkItem item = std::move(m_ready.back());
                m_ready.pop_back();
                item.Invoke();
                return true;
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
            return m_ready.size();
        }

        /// @brief Returns the number of scheduled timer items.
        [[nodiscard]] std::size_t PendingTimers() const noexcept
        {
            return m_timers.size();
        }

    private:
        struct Timer final
        {
            NGIN::Time::TimePoint resumeAt {};
            WorkItem              item {};

            static bool IsEarlier(const Timer& a, const Timer& b) noexcept
            {
                return a.resumeAt > b.resumeAt;
            }
        };

        std::vector<WorkItem> m_ready {};
        std::vector<Timer>    m_timers {};
    };
}// namespace NGIN::Execution
