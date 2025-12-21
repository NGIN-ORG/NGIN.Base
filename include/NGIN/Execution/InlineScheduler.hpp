/// @file InlineScheduler.hpp
/// @brief Scheduler that runs coroutines inline.
#pragma once

#include <NGIN/Execution/IScheduler.hpp>

#include <coroutine>
#include <cstdint>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Execution
{
    /// @brief Scheduler that resumes scheduled coroutines immediately on the calling thread.
    class InlineScheduler final : public IScheduler
    {
    public:
        InlineScheduler() = default;

        void Schedule(std::coroutine_handle<> coro) noexcept override
        {
            if (coro && !coro.done())
            {
                coro.resume();
            }
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) override
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            if (resumeAt > now)
            {
                const auto delayNs = resumeAt.ToNanoseconds() - now.ToNanoseconds();
                NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(delayNs)));
            }
            Schedule(coro);
        }

        bool RunOne() override
        {
            return false;
        }

        void RunUntilIdle() noexcept override {}

        void CancelAll() noexcept override {}

        void SetPriority(int) noexcept override {}

        void SetAffinity(uint64_t) noexcept override {}

        void OnTaskStart(uint64_t, const char*) noexcept override {}
        void OnTaskSuspend(uint64_t) noexcept override {}
        void OnTaskResume(uint64_t) noexcept override {}
        void OnTaskComplete(uint64_t) noexcept override {}
    };
}// namespace NGIN::Execution
