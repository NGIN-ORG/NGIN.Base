/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <coroutine>
#include <memory>
#include <utility>

#include <NGIN/Async/AsyncFault.hpp>
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

        template<typename Promise>
        [[nodiscard]] static bool RetainPromiseFrame(Promise& promise) noexcept
        {
            if constexpr (requires { promise.RetainFrameReference(); })
            {
                promise.RetainFrameReference();
                return true;
            }
            return false;
        }

        template<typename Promise>
        static void ReleasePromiseFrame(Promise&                             promise,
                                        const std::coroutine_handle<Promise> handle,
                                        const bool                           retained) noexcept
        {
            if constexpr (requires { promise.ReleaseFrameReference(handle); })
            {
                if (retained)
                {
                    promise.ReleaseFrameReference(handle);
                }
            }
        }

        template<typename Promise>
        class PromiseFrameLease final
        {
        public:
            PromiseFrameLease(Promise& promise, const std::coroutine_handle<Promise> handle) noexcept
                : m_promise(&promise), m_handle(handle), m_retained(TaskContext::RetainPromiseFrame(promise))
            {
            }

            PromiseFrameLease(PromiseFrameLease&& other) noexcept
                : m_promise(other.m_promise), m_handle(other.m_handle), m_retained(other.m_retained)
            {
                other.m_promise  = nullptr;
                other.m_handle   = {};
                other.m_retained = false;
            }

            PromiseFrameLease& operator=(PromiseFrameLease&& other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    m_promise        = other.m_promise;
                    m_handle         = other.m_handle;
                    m_retained       = other.m_retained;
                    other.m_promise  = nullptr;
                    other.m_handle   = {};
                    other.m_retained = false;
                }
                return *this;
            }

            PromiseFrameLease(const PromiseFrameLease&)            = delete;
            PromiseFrameLease& operator=(const PromiseFrameLease&) = delete;

            ~PromiseFrameLease()
            {
                Reset();
            }

            void Reset() noexcept
            {
                if (m_promise == nullptr)
                {
                    return;
                }

                Promise* const                       promise  = std::exchange(m_promise, nullptr);
                const std::coroutine_handle<Promise> handle   = std::exchange(m_handle, {});
                const bool                           retained = std::exchange(m_retained, false);
                TaskContext::ReleasePromiseFrame(*promise, handle, retained);
            }

        private:
            Promise*                       m_promise {};
            std::coroutine_handle<Promise> m_handle {};
            bool                           m_retained {false};
        };

        template<typename Promise>
        static void CompleteSchedulingFailure(
                Promise&                             promise,
                const std::coroutine_handle<Promise> awaiting,
                const NGIN::Execution::ScheduleError error) noexcept
        {
            AsyncFault fault;
            fault.code   = AsyncFaultCode::SchedulerDispatchFailed;
            fault.native = static_cast<int>(error);
            promise.SetFault(std::move(fault));
            promise.MarkFinishedAndResume(awaiting);
        }

        struct YieldAwaiter final
        {
            NGIN::Execution::ExecutorRef     exec {};
            CancellationToken                cancellation {};
            mutable CancellationRegistration cancellationRegistration {};

            bool await_ready() const noexcept
            {
                return false;
            }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                Promise&   promise       = awaiting.promise();
                const bool setupRetained = TaskContext::RetainPromiseFrame(promise);
                if (cancellation.IsCancellationRequested())
                {
                    promise.SetCanceled();
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                if (!exec.IsValid())
                {
                    promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                const CancellationRegistrationResult registrationResult = cancellation.Register(
                        cancellationRegistration,
                        {},
                        {},
                        +[](void* rawPromise) noexcept -> bool {
                            Promise* callbackPromise = static_cast<Promise*>(rawPromise);
                            if (!callbackPromise)
                            {
                                return false;
                            }

                            std::coroutine_handle<Promise> handle = std::coroutine_handle<Promise>::from_promise(*callbackPromise);
                            callbackPromise->SetCanceled();
                            callbackPromise->MarkFinishedAndResume(handle);
                            return false;
                        },
                        &promise);
                if (!registrationResult)
                {
                    AsyncFault fault;
                    fault.code   = AsyncFaultCode::CancellationRegistrationFailed;
                    fault.native = static_cast<int>(registrationResult.error());
                    promise.SetFault(std::move(fault));
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                if (TaskContext::PromiseAlreadyCompleted(promise))
                {
                    cancellationRegistration.Reset();
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                PromiseFrameLease<Promise>            workLease(promise, awaiting);
                CancellationRegistration* const       registration   = &cancellationRegistration;
                Promise* const                        promisePointer = &promise;
                const NGIN::Execution::ScheduleResult result         = exec.Execute(
                        [promisePointer, registration, awaiting, lease = std::move(workLease)]() mutable noexcept {
                            registration->Reset();
                            if (!TaskContext::PromiseAlreadyCompleted(*promisePointer))
                            {
                                awaiting.resume();
                            }
                            lease.Reset();
                        });
                if (!result)
                {
                    cancellationRegistration.Reset();
                    TaskContext::CompleteSchedulingFailure(promise, awaiting, result.error());
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }
                TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
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
                : exec(executor), cancellation(std::move(token)), duration(dur), until([&] {
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
                Promise&   promise       = awaiting.promise();
                const bool setupRetained = TaskContext::RetainPromiseFrame(promise);
                if (cancellation.IsCancellationRequested())
                {
                    promise.SetCanceled();
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                if (!exec.IsValid())
                {
                    promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                const CancellationRegistrationResult registrationResult = cancellation.Register(
                        cancellationRegistration,
                        {},
                        {},
                        +[](void* rawPromise) noexcept -> bool {
                            Promise* callbackPromise = static_cast<Promise*>(rawPromise);
                            if (!callbackPromise)
                            {
                                return false;
                            }

                            std::coroutine_handle<Promise> handle = std::coroutine_handle<Promise>::from_promise(*callbackPromise);
                            callbackPromise->SetCanceled();
                            callbackPromise->MarkFinishedAndResume(handle);
                            return false;
                        },
                        &promise);
                if (!registrationResult)
                {
                    AsyncFault fault;
                    fault.code   = AsyncFaultCode::CancellationRegistrationFailed;
                    fault.native = static_cast<int>(registrationResult.error());
                    promise.SetFault(std::move(fault));
                    promise.MarkFinishedAndResume(awaiting);
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                if (TaskContext::PromiseAlreadyCompleted(promise))
                {
                    cancellationRegistration.Reset();
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }

                PromiseFrameLease<Promise>            workLease(promise, awaiting);
                CancellationRegistration* const       registration   = &cancellationRegistration;
                Promise* const                        promisePointer = &promise;
                const NGIN::Execution::ScheduleResult result         = exec.ExecuteAt(
                        [promisePointer, registration, awaiting, lease = std::move(workLease)]() mutable noexcept {
                            registration->Reset();
                            if (!TaskContext::PromiseAlreadyCompleted(*promisePointer))
                            {
                                awaiting.resume();
                            }
                            lease.Reset();
                        },
                        until);
                if (!result)
                {
                    cancellationRegistration.Reset();
                    TaskContext::CompleteSchedulingFailure(promise, awaiting, result.error());
                    TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                    return std::noop_coroutine();
                }
                TaskContext::ReleasePromiseFrame(promise, awaiting, setupRetained);
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

    public:
        /// @brief Constructs a context from an executor reference and optional cancellation token.
        explicit TaskContext(NGIN::Execution::ExecutorRef executor, CancellationToken cancellation = {}) noexcept
            : m_executor(executor), m_cancellation(std::move(cancellation))
        {
        }

        /// @brief Constructs a context that borrows a compatible scheduler.
        template<typename TScheduler>
        explicit TaskContext(TScheduler& scheduler, CancellationToken cancellation = {}) noexcept
            : m_executor(NGIN::Execution::ExecutorRef::From(scheduler)), m_cancellation(std::move(cancellation))
        {
        }

        /// @brief Returns whether this context has a usable executor.
        [[nodiscard]] bool HasExecutor() const noexcept
        {
            return m_executor.IsValid();
        }

        /// @brief Rebinds this context to an executor reference.
        void BindExecutor(NGIN::Execution::ExecutorRef executor) noexcept
        {
            m_executor = executor;
        }

        /// @brief Rebinds this context to a borrowed compatible scheduler.
        template<typename TScheduler>
        void BindExecutor(TScheduler& scheduler) noexcept
        {
            m_executor = NGIN::Execution::ExecutorRef::From(scheduler);
        }

        /// @brief Replaces cancellation state and releases any linked-token ownership.
        void BindCancellationToken(CancellationToken cancellation) noexcept
        {
            m_cancellationOwner.reset();
            m_cancellation = std::move(cancellation);
        }

        /// @brief Returns a copy of this context with replacement cancellation state.
        [[nodiscard]] TaskContext WithCancellationToken(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindCancellationToken(std::move(cancellation));
            return copy;
        }

        /// @brief Rebinds cancellation to a token canceled when either current or supplied state is canceled.
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

            std::shared_ptr<detail::LinkedCancellationState> linked =
                    std::make_shared<detail::LinkedCancellationState>();
            linked->Link({m_cancellation, cancellation});

            if (m_cancellationOwner)
            {
                struct OwnerChain final
                {
                    std::shared_ptr<void> previous {};
                    std::shared_ptr<void> current {};
                };

                std::shared_ptr<OwnerChain> chain = std::make_shared<OwnerChain>();
                chain->previous                   = m_cancellationOwner;
                chain->current                    = linked;
                m_cancellationOwner               = std::move(chain);
            }
            else
            {
                m_cancellationOwner = linked;
            }

            m_cancellation = linked->source.GetToken();
        }

        /// @brief Returns a copy whose cancellation observes both the current and supplied tokens.
        [[nodiscard]] TaskContext WithLinkedCancellationToken(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindLinkedCancellationToken(std::move(cancellation));
            return copy;
        }

        /// @brief Returns the bound executor reference.
        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() const noexcept
        {
            return m_executor;
        }

        /// @brief Returns the bound cancellation token.
        [[nodiscard]] CancellationToken GetCancellationToken() const noexcept
        {
            return m_cancellation;
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_cancellation.IsCancellationRequested();
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool CheckCancellation() const noexcept
        {
            return m_cancellation.IsCancellationRequested();
        }

        /// @brief Creates an awaiter that reschedules the current task through the bound executor.
        [[nodiscard]] auto YieldNow() const noexcept
        {
            return YieldAwaiter {m_executor, m_cancellation};
        }

        /// @brief Creates a cancellation-aware awaiter for a time-quantity delay.
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
