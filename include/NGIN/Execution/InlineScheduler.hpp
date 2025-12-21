/// @file InlineScheduler.hpp
/// @brief Scheduler that runs coroutines inline.
#pragma once

#include <NGIN/Execution/IScheduler.hpp>

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <thread>

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

        void ScheduleDelay(std::coroutine_handle<> coro, std::chrono::steady_clock::time_point resumeAt) override
        {
            auto now = std::chrono::steady_clock::now();
            if (resumeAt > now)
            {
                std::this_thread::sleep_for(resumeAt - now);
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
