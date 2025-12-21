/// <summary>
/// Abstract interface for scheduling coroutines/tasks.
/// </summary>
#pragma once

#include <coroutine>
#include <chrono>
#include <cstdint>

namespace NGIN::Execution
{
    /// <summary>
    /// Pure virtual interface for all scheduler implementations.
    /// </summary>
    class IScheduler
    {
    public:
        /// <summary>
        /// Schedule a coroutine for execution.
        /// </summary>
        virtual void Schedule(std::coroutine_handle<> coro) noexcept = 0;

        virtual void ScheduleDelay(std::coroutine_handle<> coro, std::chrono::steady_clock::time_point resumeAt) = 0;

        /// <summary>
        /// Attempt to run one unit of work. Returns true if work was performed.
        /// </summary>
        virtual bool RunOne() = 0;

        /// <summary>
        /// Run until no more work is available.
        /// </summary>
        virtual void RunUntilIdle() noexcept = 0;

        /// <summary>
        /// Cancel all pending tasks.
        /// </summary>
        virtual void CancelAll() noexcept = 0;

        /// <summary>
        /// Set scheduling priority for subsequent tasks.
        /// </summary>
        virtual void SetPriority(int priority) noexcept = 0;

        /// <summary>
        /// Set thread affinity for subsequent tasks.
        /// </summary>
        virtual void SetAffinity(uint64_t affinityMask) noexcept = 0;

        /// <summary>
        /// Instrumentation hook: task started.
        /// </summary>
        virtual void OnTaskStart(uint64_t taskId, const char* name) noexcept = 0;
        /// <summary>
        /// Instrumentation hook: task suspended.
        /// </summary>
        virtual void OnTaskSuspend(uint64_t taskId) noexcept = 0;
        /// <summary>
        /// Instrumentation hook: task resumed.
        /// </summary>
        virtual void OnTaskResume(uint64_t taskId) noexcept = 0;
        /// <summary>
        /// Instrumentation hook: task completed or threw.
        /// </summary>
        virtual void OnTaskComplete(uint64_t taskId) noexcept = 0;

        virtual ~IScheduler() = default;
    };

}// namespace NGIN::Execution
