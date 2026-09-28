/// @file InlineScheduler.hpp
/// @brief Explicit synchronous invocation adapter without queued submission.
#pragma once

#include <NGIN/Execution/ScheduleResult.hpp>
#include <NGIN/Execution/WorkItem.hpp>

#include <NGIN/Time/TimePoint.hpp>
#include <coroutine>
#include <cstdint>

namespace NGIN::Execution
{
    /// @brief Synchronous invocation adapter; queued submission is unsupported.
    class InlineScheduler final
    {
    public:
        /// @brief Constructs a stateless inline scheduler.
        InlineScheduler() = default;

        /// @brief Explicitly invokes a work item synchronously on the calling thread.
        /// @note This is an ordinary call, not an executor dispatch context or a yield.
        [[nodiscard]] ScheduleResult Invoke(WorkItem item) noexcept
        {
            if (item.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            item.Invoke();
            return {};
        }

        /// @brief Rejects queued submission because this adapter owns no queue.
        [[nodiscard]] ScheduleResult Execute(WorkItem) noexcept
        {
            return std::unexpected(ScheduleError::Rejected);
        }

        /// @brief Rejects timed submission without sleeping or invoking the work.
        [[nodiscard]] ScheduleResult ExecuteAt(WorkItem, NGIN::Time::TimePoint) noexcept
        {
            return std::unexpected(ScheduleError::Rejected);
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
