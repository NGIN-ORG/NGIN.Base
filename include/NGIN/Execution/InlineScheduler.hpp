/// @file InlineScheduler.hpp
/// @brief Scheduler that runs coroutines inline.
#pragma once

#include <NGIN/Execution/WorkItem.hpp>

#include <coroutine>
#include <cstdint>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Execution
{
    /// @brief Scheduler that resumes scheduled coroutines immediately on the calling thread.
    class InlineScheduler final
    {
    public:
        InlineScheduler() = default;

        void Execute(WorkItem item) noexcept
        {
            item.Invoke();
        }

        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt)
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            if (resumeAt > now)
            {
                const auto delayNs = resumeAt.ToNanoseconds() - now.ToNanoseconds();
                NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(delayNs)));
            }
            Execute(std::move(item));
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            return false;
        }

        void RunUntilIdle() noexcept {}

        void CancelAll() noexcept {}

        void SetPriority(int) noexcept {}

        void SetAffinity(uint64_t) noexcept {}

        void OnTaskStart(uint64_t, const char*) noexcept {}
        void OnTaskSuspend(uint64_t) noexcept {}
        void OnTaskResume(uint64_t) noexcept {}
        void OnTaskComplete(uint64_t) noexcept {}
    };
}// namespace NGIN::Execution
