/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <coroutine>
#include <memory>
#include <utility>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class TaskContext
    {
        template<typename Promise>
        [[nodiscard]] static bool PromiseAlreadyCompleted(const Promise& promise) noexcept
        {
            if constexpr (requires { promise.m_finished.load(std::memory_order_acquire); })
            {
                return promise.m_finished.load(std::memory_order_acquire);
            }
            else if constexpr (requires { promise.completed; })
            {
                return promise.completed;
            }
            else
            {
                return false;
            }
        }

        struct YieldAwaiter final
        {
            NGIN::Execution::ExecutorRef exec {};
            CancellationToken            cancellation {};
            mutable CancellationRegistration cancellationRegistration {};

            bool await_ready() const noexcept
            {
                return false;
            }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                if (cancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (!exec.IsValid())
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                cancellation.Register(
                        cancellationRegistration,
                        {},
                        {},
                        +[](void* rawPromise) noexcept -> bool {
                            auto* promise = static_cast<Promise*>(rawPromise);
                            if (!promise)
                            {
                                return false;
                            }

                            auto handle = std::coroutine_handle<Promise>::from_promise(*promise);
                            promise->SetCanceled();
                            promise->MarkFinishedAndResume(handle);
                            return false;
                        },
                        &awaiting.promise());

                auto* promise = &awaiting.promise();
                exec.Execute([promise, awaiting]() mutable noexcept {
                    if (TaskContext::PromiseAlreadyCompleted(*promise))
                    {
                        return;
                    }
                    awaiting.resume();
                });
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        template<typename TUnit>
        struct DelayAwaiter final
        {
            NGIN::Execution::ExecutorRef     exec {};
            CancellationToken                cancellation {};
            mutable CancellationRegistration cancellationRegistration {};
            TUnit                            duration;
            NGIN::Time::TimePoint            until;

            DelayAwaiter(NGIN::Execution::ExecutorRef executor, CancellationToken token, const TUnit& dur)
                : exec(executor)
                , cancellation(std::move(token))
                , duration(dur)
                , until([&] {
                    const auto now = NGIN::Time::MonotonicClock::Now();
                    const auto ns  = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(dur).GetValue();
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
                return exec.IsValid() &&
                       !cancellation.IsCancellationRequested() &&
                       NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue() <= 0.0;
            }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                if (cancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (!exec.IsValid())
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                cancellation.Register(
                        cancellationRegistration,
                        {},
                        {},
                        +[](void* rawPromise) noexcept -> bool {
                            auto* promise = static_cast<Promise*>(rawPromise);
                            if (!promise)
                            {
                                return false;
                            }

                            auto handle = std::coroutine_handle<Promise>::from_promise(*promise);
                            promise->SetCanceled();
                            promise->MarkFinishedAndResume(handle);
                            return false;
                        },
                        &awaiting.promise());

                auto* promise = &awaiting.promise();
                exec.ExecuteAt(
                        [promise, awaiting]() mutable noexcept {
                            if (TaskContext::PromiseAlreadyCompleted(*promise))
                            {
                                return;
                            }
                            awaiting.resume();
                        },
                        until);
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

    public:
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

        [[nodiscard]] bool HasExecutor() const noexcept
        {
            return m_executor.IsValid();
        }

        void BindExecutor(NGIN::Execution::ExecutorRef executor) noexcept
        {
            m_executor = executor;
        }

        template<typename TScheduler>
        void BindExecutor(TScheduler& scheduler) noexcept
        {
            m_executor = NGIN::Execution::ExecutorRef::From(scheduler);
        }

        void BindCancellationToken(CancellationToken cancellation) noexcept
        {
            m_cancellationOwner.reset();
            m_cancellation = std::move(cancellation);
        }

        [[nodiscard]] TaskContext WithCancellationToken(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindCancellationToken(std::move(cancellation));
            return copy;
        }

        void BindLinkedCancellationToken(CancellationToken cancellation) noexcept
        {
            if (!m_cancellation.HasState())
            {
                BindCancellationToken(std::move(cancellation));
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

                auto chain = std::make_shared<OwnerChain>();
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

        [[nodiscard]] TaskContext WithLinkedCancellationToken(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindLinkedCancellationToken(std::move(cancellation));
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

        [[nodiscard]] bool CheckCancellation() const noexcept
        {
            return m_cancellation.IsCancellationRequested();
        }

        [[nodiscard]] auto YieldNow() const noexcept
        {
            return YieldAwaiter {m_executor, m_cancellation};
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] auto Delay(const TUnit& duration) const noexcept
        {
            return DelayAwaiter<TUnit> {m_executor, m_cancellation, duration};
        }

    private:
        NGIN::Execution::ExecutorRef m_executor {};
        CancellationToken            m_cancellation {};
        std::shared_ptr<void>        m_cancellationOwner {};
    };
}// namespace NGIN::Async
