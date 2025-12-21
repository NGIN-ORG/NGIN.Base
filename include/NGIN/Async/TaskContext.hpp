/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <coroutine>
#include <utility>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class TaskContext
    {
    public:
        constexpr TaskContext() noexcept = default;

        explicit TaskContext(NGIN::Execution::ExecutorRef executor, CancellationToken cancellation = {}) noexcept
            : m_executor(executor)
            , m_cancellation(std::move(cancellation))
        {
        }

        template<typename TScheduler>
        explicit TaskContext(TScheduler& scheduler, CancellationToken cancellation = {}) noexcept
            : m_executor(NGIN::Execution::ExecutorRef::From(scheduler))
            , m_cancellation(std::move(cancellation))
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

        void BindCancellation(CancellationToken cancellation) noexcept
        {
            m_cancellation = std::move(cancellation);
        }

        [[nodiscard]] TaskContext WithCancellation(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindCancellation(std::move(cancellation));
            return copy;
        }

        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() const noexcept
        {
            return m_executor;
        }

        [[nodiscard]] CancellationToken GetCancellationToken() const noexcept
        {
            return m_cancellation;
        }

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_cancellation.IsCancellationRequested();
        }

        void ThrowIfCancellationRequested() const
        {
            if (m_cancellation.IsCancellationRequested())
            {
                throw TaskCanceled();
            }
        }

        auto Yield() const noexcept
        {
            struct Awaiter
            {
                NGIN::Execution::ExecutorRef exec;
                CancellationToken            cancellation;
                bool                         await_ready() const noexcept
                {
                    return cancellation.IsCancellationRequested();
                }
                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    exec.Schedule(h);
                }
                void await_resume() const
                {
                    if (cancellation.IsCancellationRequested())
                    {
                        throw TaskCanceled();
                    }
                }
            };
            return Awaiter {m_executor, m_cancellation};
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        auto Delay(const TUnit& dur) const noexcept
        {
            struct DelayAwaiter
            {
                NGIN::Execution::ExecutorRef           exec;
                CancellationToken                      cancellation;
                mutable CancellationRegistration        cancellationRegistration {};
                TUnit                                  dur;
                NGIN::Time::TimePoint                  until;

                DelayAwaiter(NGIN::Execution::ExecutorRef e, CancellationToken c, const TUnit& d)
                    : exec(e)
                    , cancellation(std::move(c))
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
                    return cancellation.IsCancellationRequested() ||
                           NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(dur).GetValue() <= 0.0;
                }
                void await_suspend(std::coroutine_handle<> handle) const
                {
                    cancellation.Register(cancellationRegistration, exec, handle);
                    exec.ScheduleAt(handle, until);
                }
                void await_resume() const
                {
                    if (cancellation.IsCancellationRequested())
                    {
                        throw TaskCanceled();
                    }
                }
            };
            return DelayAwaiter {m_executor, m_cancellation, dur};
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
        CancellationToken            m_cancellation {};
    };
}// namespace NGIN::Async
