/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <coroutine>
#include <utility>

#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class TaskContext
    {
    public:
        constexpr TaskContext() noexcept = default;

        explicit TaskContext(NGIN::Execution::ExecutorRef executor) noexcept
            : m_executor(executor)
        {
        }

        template<typename TScheduler>
        explicit TaskContext(TScheduler& scheduler) noexcept
            : m_executor(NGIN::Execution::ExecutorRef::From(scheduler))
        {
        }

        void Bind(NGIN::Execution::ExecutorRef executor) noexcept
        {
            m_executor = executor;
        }

        template<typename TScheduler>
        void Bind(TScheduler& scheduler) noexcept
        {
            m_executor = NGIN::Execution::ExecutorRef::From(scheduler);
        }

        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() const noexcept
        {
            return m_executor;
        }

        auto Yield() const noexcept
        {
            struct Awaiter
            {
                NGIN::Execution::ExecutorRef exec;
                bool                         await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    exec.Schedule(h);
                }
                void await_resume() const noexcept {}
            };
            return Awaiter {m_executor};
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        auto Delay(const TUnit& dur) const noexcept
        {
            struct DelayAwaiter
            {
                NGIN::Execution::ExecutorRef           exec;
                TUnit                                  dur;
                NGIN::Time::TimePoint                  until;

                DelayAwaiter(NGIN::Execution::ExecutorRef e, const TUnit& d)
                    : exec(e)
                    , dur(d)
                    , until([&] {
                        const auto now = NGIN::Time::MonotonicClock::Now();
                        const auto ns  = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(d).GetValue();
                        if (ns <= 0.0)
                        {
                            return now;
                        }
                        auto add = static_cast<NGIN::UInt64>(ns);
                        if (static_cast<double>(add) < ns)
                        {
                            ++add;
                        }
                        return NGIN::Time::TimePoint::FromNanoseconds(now.ToNanoseconds() + add);
                    }())
                {
                }

                bool await_ready() const noexcept
                {
                    return NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(dur).GetValue() <= 0.0;
                }
                void await_suspend(std::coroutine_handle<> handle) const
                {
                    exec.ScheduleAt(handle, until);
                }
                void await_resume() const noexcept {}
            };
            return DelayAwaiter {m_executor, dur};
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
        NGIN::Execution::ExecutorRef m_executor {};
    };
}// namespace NGIN::Async
