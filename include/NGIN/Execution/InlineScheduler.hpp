/// @file InlineScheduler.hpp
/// @brief Scheduler that runs coroutines inline.
#pragma once

#include <NGIN/Execution/WorkItem.hpp>

#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>
#include <coroutine>
#include <cstdint>

namespace NGIN::Execution
{
    /// @brief Scheduler that resumes scheduled coroutines immediately on the calling thread.
    class InlineScheduler final
    {
    public:
        /// @brief Constructs a stateless inline scheduler.
        InlineScheduler() = default;

        /// @brief Invokes a work item synchronously on the calling thread.
        void Execute(WorkItem item) noexcept
        {
            item.Invoke();
        }

        /// @brief Sleeps the calling thread until `resumeAt`, then invokes the item synchronously.
        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt)
        {
            const NGIN::Time::TimePoint now = NGIN::Time::MonotonicClock::Now();
            if (resumeAt > now)
            {
                const NGIN::UInt64 delayNs = resumeAt.ToNanoseconds() - now.ToNanoseconds();
                NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(delayNs)));
            }
            Execute(std::move(item));
        }

        /// @brief Returns `false` because inline work is never queued.
        [[nodiscard]] bool RunOne() noexcept
        {
            return false;
        }

        /// @brief Does nothing because inline work is never queued.
        void RunUntilIdle() noexcept {}

        /// @brief Does nothing because inline work is never queued.
        void CancelAll() noexcept {}

        /// @brief Accepts a scheduler-priority hint; inline execution ignores it.
        void SetPriority(int) noexcept {}

        /// @brief Accepts an affinity hint; inline execution ignores it.
        void SetAffinity(uint64_t) noexcept {}

        /// @brief Receives a task-start notification; the inline scheduler does not record it.
        void OnTaskStart(uint64_t, const char*) noexcept {}
        /// @brief Receives a task-suspend notification; the inline scheduler does not record it.
        void OnTaskSuspend(uint64_t) noexcept {}
        /// @brief Receives a task-resume notification; the inline scheduler does not record it.
        void OnTaskResume(uint64_t) noexcept {}
        /// @brief Receives a task-complete notification; the inline scheduler does not record it.
        void OnTaskComplete(uint64_t) noexcept {}
    };
}// namespace NGIN::Execution
