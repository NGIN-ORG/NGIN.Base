/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <chrono>
#include <coroutine>
#include <utility>

#include <NGIN/Execution/IScheduler.hpp>

namespace NGIN::Async
{
    class TaskContext
    {
    public:
        explicit TaskContext(NGIN::Execution::IScheduler* scheduler = nullptr) noexcept
            : m_scheduler(scheduler)
        {
        }

        void Bind(NGIN::Execution::IScheduler* scheduler) noexcept
        {
            m_scheduler = scheduler;
        }

        [[nodiscard]] NGIN::Execution::IScheduler* GetScheduler() const noexcept
        {
            return m_scheduler;
        }

        auto Yield() const noexcept
        {
            struct Awaiter
            {
                NGIN::Execution::IScheduler* sched;
                bool                         await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    sched->Schedule(h);
                }
                void await_resume() const noexcept {}
            };
            return Awaiter {m_scheduler};
        }

        auto Delay(std::chrono::milliseconds dur) const noexcept
        {
            struct DelayAwaiter
            {
                NGIN::Execution::IScheduler*           sched;
                std::chrono::milliseconds             dur;
                std::chrono::steady_clock::time_point until;

                DelayAwaiter(NGIN::Execution::IScheduler* s, std::chrono::milliseconds d)
                    : sched(s)
                    , dur(d)
                    , until(std::chrono::steady_clock::now() + d)
                {
                }

                bool await_ready() const noexcept
                {
                    return dur.count() == 0;
                }
                void await_suspend(std::coroutine_handle<> handle) const
                {
                    sched->ScheduleDelay(handle, until);
                }
                void await_resume() const noexcept {}
            };
            return DelayAwaiter {m_scheduler, dur};
        }

        template<typename TaskT>
        TaskT Run(TaskT&& task)
        {
            task.Start(*this);
            return std::forward<TaskT>(task);
        }

        template<typename Func, typename... Args>
        auto Run(Func&& func, Args&&... args) -> decltype(auto)
        {
            auto task = func(*this, std::forward<Args>(args)...);
            task.Start(*this);
            return task;
        }

    private:
        NGIN::Execution::IScheduler* m_scheduler {nullptr};
    };
}// namespace NGIN::Async
