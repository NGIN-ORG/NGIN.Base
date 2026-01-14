/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <coroutine>
#include <utility>
#include <memory>

#include <NGIN/Async/AsyncError.hpp>
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
            m_cancellationOwner.reset();
            m_cancellation = std::move(cancellation);
        }

        [[nodiscard]] TaskContext WithCancellation(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindCancellation(std::move(cancellation));
            return copy;
        }

        void BindLinkedCancellation(CancellationToken cancellation) noexcept
        {
            if (!m_cancellation.HasState())
            {
                BindCancellation(std::move(cancellation));
                return;
            }

            if (!cancellation.HasState())
            {
                return;
            }

            auto linked = std::make_shared<detail::LinkedCancellationState>();
            linked->Link({m_cancellation, cancellation});

            if (m_cancellationOwner)
            {
                struct OwnerChain final
                {
                    std::shared_ptr<void> previous {};
                    std::shared_ptr<void> current {};
                };

                auto chain     = std::make_shared<OwnerChain>();
                chain->previous = m_cancellationOwner;
                chain->current  = linked;
                m_cancellationOwner = std::move(chain);
            }
            else
            {
                m_cancellationOwner = linked;
            }

            m_cancellation = linked->source.GetToken();
        }

        [[nodiscard]] TaskContext WithLinkedCancellation(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindLinkedCancellation(std::move(cancellation));
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

        [[nodiscard]] AsyncExpected<void> CheckCancellation() const noexcept
        {
            if (m_cancellation.IsCancellationRequested())
            {
                return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
            }
            return {};
        }

        auto YieldNow() const noexcept
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
                AsyncExpected<void> await_resume() const noexcept
                {
                    if (cancellation.IsCancellationRequested())
                    {
                        return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                    }
                    return {};
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
                AsyncExpected<void> await_resume() const noexcept
                {
                    if (cancellation.IsCancellationRequested())
                    {
                        return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                    }
                    return {};
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
        std::shared_ptr<void>        m_cancellationOwner {};
    };
}// namespace NGIN::Async
