/// <summary>
/// Execution context binding tasks to a specific scheduler.
/// </summary>
#pragma once

#include <atomic>
#include <cmath>
#include <coroutine>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include <NGIN/Async/AsyncEnvironment.hpp>
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

        template<typename Promise>
        struct PendingResume final
        {
            enum class Outcome
            {
                Ready,
                Canceled,
                Fault
            };
            explicit PendingResume(std::coroutine_handle<Promise> handle, bool removable,
                                   NGIN::Execution::ExecutorRef selected, bool allowInline) noexcept
                : awaiting(handle), lease(handle.promise(), handle), executor(selected),
                  inlineDelivery(allowInline), removableTimer(removable) {}

            std::coroutine_handle<Promise>         awaiting;
            PromiseFrameLease<Promise>             lease;
            CancellationRegistration               registration;
            NGIN::Execution::CompletionReservation delivery;
            NGIN::Execution::ExecutorRef           executor;
            const bool                             inlineDelivery;
            std::atomic<unsigned>                  publication {0};
            std::atomic<bool>                      claimed {false};
            std::atomic<unsigned>                  submission {0};
            Outcome                                outcome {Outcome::Ready};
            AsyncFault                             fault;
            const bool                             removableTimer;
            std::mutex                             timerMutex;
            NGIN::Execution::TimerRegistration     timer;

            void InstallTimer(NGIN::Execution::TimerRegistration incoming) noexcept
            {
                std::lock_guard lock(timerMutex);
                // Completion can race the scheduler returning its registration.
                // In that case the incoming owner removes the record on return.
                if (!claimed.load(std::memory_order_acquire))
                    timer = std::move(incoming);
            }

            void ArmSubmission() noexcept
            {
                if (submission.fetch_or(1U, std::memory_order_acq_rel) & 2U)
                    CompleteStopped();
            }

            void DiscardSubmission() noexcept
            {
                if (submission.fetch_or(2U, std::memory_order_acq_rel) & 1U)
                    CompleteStopped();
            }

            void CompleteStopped() noexcept
            {
                Complete(Outcome::Fault, MakeAsyncFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                        static_cast<int>(NGIN::Execution::ScheduleError::Stopped)));
            }

            void Complete(Outcome result, AsyncFault failure = {}) noexcept
            {
                if (claimed.exchange(true, std::memory_order_acq_rel))
                    return;
                NGIN::Execution::TimerRegistration retired;
                if (removableTimer)
                {
                    std::lock_guard lock(timerMutex);
                    retired = std::move(timer);
                }
                retired.Cancel();
                outcome = result;
                fault   = std::move(failure);
                Publish(2U);
            }

            void Publish(unsigned flag) noexcept
            {
                const unsigned previous = publication.fetch_or(flag, std::memory_order_acq_rel);
                if ((previous | flag) != 3U || (previous & flag))
                    return;
                auto                      ticket = std::move(delivery);
                NGIN::Execution::WorkItem work([this] { Resume(); });
                if (!inlineDelivery || !executor.TryDispatchInline(work))
                    ticket.Dispatch();
            }

            CancellationToken cancellation;

            void Resume() noexcept
            {
                registration.Reset();
                const auto handle  = awaiting;
                Promise&   promise = handle.promise();
                if (TaskContext::PromiseAlreadyCompleted(promise))
                {
                    lease.Reset();
                    return;
                }
                // Started execution owns the frame until terminal publication.
                // End this delivery's borrowed-frame access before resuming or
                // publishing failure: a parent may immediately destroy locals
                // borrowed by this child once it observes completion.
                lease.Reset();
                if (outcome == Outcome::Ready)
                    handle.resume();
                else
                {
                    if (outcome == Outcome::Canceled)
                        promise.SetStopped(cancellation.GetStopReason());
                    else
                        promise.SetFault(std::move(fault));
                    promise.MarkFinishedAndResume(handle);
                }
            }
        };

        template<typename Promise>
        static std::coroutine_handle<> ScheduleAwait(
                std::coroutine_handle<Promise> awaiting, NGIN::Execution::ExecutorRef exec,
                CancellationToken cancellation, bool timed, NGIN::Time::TimePoint until = {}) noexcept
        {
            Promise&                   promise = awaiting.promise();
            PromiseFrameLease<Promise> setupLease(promise, awaiting);
            if constexpr (requires { promise.RebindExecutor(exec); })
            {
                auto rebound = promise.RebindExecutor(exec);
                if (!rebound)
                {
                    setupLease.Reset();
                    CompleteSchedulingFailure(promise, awaiting, rebound.error());
                    return std::noop_coroutine();
                }
            }
            if (cancellation.IsCancellationRequested())
            {
                promise.SetStopped(cancellation.GetStopReason());
                setupLease.Reset();
                promise.MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }
            if (!exec.IsValid())
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                setupLease.Reset();
                promise.MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            const bool removableTimer = timed && exec.SupportsTimerRemoval();
            if (!cancellation.HasState() && !removableTimer)
            {
                if constexpr (requires { promise.SubmitTracked(awaiting, timed, until); promise.m_environment.executor; })
                {
                    // Only the same dispatch binding can reuse this reservation.
                    // A context selecting another executor reserves its own path below.
                    if (promise.HasContinuation() && exec == promise.m_environment.executor)
                    {
                        setupLease.Reset();
                        (void) promise.SubmitTracked(awaiting, timed, until);
                        return std::noop_coroutine();
                    }
                }
            }

            using State = PendingResume<Promise>;
            std::shared_ptr<State> state;
            try
            {
                auto* resource = std::pmr::get_default_resource();
                if constexpr (requires { promise.m_environment.resource; })
                    resource = promise.m_environment.resource;
                state = std::allocate_shared<State>(std::pmr::polymorphic_allocator<State>(resource), awaiting, removableTimer, exec, timed);
            } catch (const std::bad_alloc&)
            {
                setupLease.Reset();
                CompleteSchedulingFailure(promise, awaiting, NGIN::Execution::ScheduleError::ResourceExhausted);
                return std::noop_coroutine();
            }
            state->cancellation = cancellation;
            auto reservation    = exec.ReserveCompletion(NGIN::Execution::WorkItem([state] { state->Resume(); }));
            if (!reservation)
            {
                state->lease.Reset();
                setupLease.Reset();
                CompleteSchedulingFailure(promise, awaiting, reservation.error());
                return std::noop_coroutine();
            }
            state->delivery = std::move(*reservation);
            struct CommitSetup final
            {
                std::shared_ptr<State> state;
                ~CommitSetup() { state->Publish(1U); }
            } commit {state};
            setupLease.Reset();
            const auto registered = cancellation.Register(state->registration, {}, {}, +[](void* rawState) noexcept {
                static_cast<State*>(rawState)->Complete(State::Outcome::Canceled);
                return false; }, state.get());
            if (!registered)
            {
                state->Complete(State::Outcome::Fault, MakeAsyncFault(
                                                               AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(registered.error())));
                return std::noop_coroutine();
            }
            struct ReadySignal
            {
                explicit ReadySignal(std::shared_ptr<State> owner) noexcept : state(std::move(owner)) {}
                ReadySignal(ReadySignal&& other) noexcept : state(std::move(other.state)), invoked(other.invoked) {}
                ReadySignal(const ReadySignal&) = delete;
                ~ReadySignal()
                {
                    if (state && !invoked)
                        state->DiscardSubmission();
                }
                void operator()() noexcept
                {
                    invoked = true;
                    state->Complete(State::Outcome::Ready);
                }
                std::shared_ptr<State> state;
                bool                   invoked {false};
            } ready(state);
            NGIN::Execution::ScheduleResult result;
            if (removableTimer)
            {
                auto timer = exec.ScheduleTimer(NGIN::Execution::WorkItem(std::move(ready)), until);
                if (timer)
                    state->InstallTimer(std::move(*timer));
                else
                    result = std::unexpected(timer.error());
            }
            else
                result = timed ? exec.ExecuteAt(std::move(ready), until) : exec.Execute(std::move(ready));
            if (!result)
                state->Complete(State::Outcome::Fault, MakeAsyncFault(
                                                               AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(result.error())));
            state->ArmSubmission();
            return std::noop_coroutine();
        }

        struct YieldAwaiter final
        {
            NGIN::Execution::ExecutorRef exec {};
            CancellationToken            cancellation {};
            bool                         await_ready() const noexcept { return false; }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                return TaskContext::ScheduleAwait(awaiting, exec, cancellation, false);
            }
            void await_resume() const noexcept {}
        };

        template<typename TUnit>
        struct DelayAwaiter final
        {
            NGIN::Execution::ExecutorRef exec {};
            CancellationToken            cancellation {};
            TUnit                        duration;
            NGIN::Time::TimePoint        until;

            DelayAwaiter(NGIN::Execution::ExecutorRef executor, CancellationToken token, const TUnit& dur)
                : exec(executor), cancellation(std::move(token)), duration(dur), until([&] {
                      const auto now = NGIN::Time::MonotonicClock::Now();
                      const auto ns  = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(dur).GetValue();
                      if (!std::isfinite(ns) || ns <= 0.0)
                      {
                          return now;
                      }
                      const NGIN::UInt64 maximum = (std::numeric_limits<NGIN::UInt64>::max)();
                      if (ns >= static_cast<double>(maximum))
                          return NGIN::Time::TimePoint::FromNanoseconds(maximum);
                      auto add = static_cast<NGIN::UInt64>(ns);
                      if (static_cast<double>(add) < ns)
                      {
                          ++add;
                      }
                      return NGIN::Time::TimePoint::FromNanoseconds(
                              add > maximum - now.ToNanoseconds() ? maximum : now.ToNanoseconds() + add);
                  }())
            {
            }

            bool await_ready() const noexcept
            {
                return exec.IsCurrent() &&
                       !cancellation.IsCancellationRequested() &&
                       std::isfinite(NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue()) &&
                       NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue() <= 0.0;
            }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                if (!std::isfinite(NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue()))
                {
                    TaskContext::CompleteSchedulingFailure(awaiting.promise(), awaiting,
                                                           NGIN::Execution::ScheduleError::Rejected);
                    return std::noop_coroutine();
                }
                return TaskContext::ScheduleAwait(awaiting, exec, cancellation, true, until);
            }

            void await_resume() const noexcept {}
        };

    public:
        /// @brief Validates and snapshots the starter's logical environment.
        explicit TaskContext(AsyncEnvironment environment)
            : m_environment(std::move(environment))
        {
            m_environment.Validate();
        }

        /// @brief Returns an allocation-free snapshot of the bound environment.
        [[nodiscard]] AsyncEnvironment GetEnvironment() const noexcept { return m_environment; }

        /// @brief Constructs a context from an executor reference and optional cancellation token.
        explicit TaskContext(NGIN::Execution::ExecutorRef executor, CancellationToken cancellation = {}) noexcept
            : m_environment {.executor = executor, .cancellation = std::move(cancellation)}
        {
        }

        /// @brief Constructs a context that borrows a compatible scheduler.
        template<typename TScheduler>
        explicit TaskContext(TScheduler& scheduler, CancellationToken cancellation = {}) noexcept
            : TaskContext(NGIN::Execution::ExecutorRef::From(scheduler), std::move(cancellation))
        {
        }

        /// @brief Returns whether this context has a usable executor.
        [[nodiscard]] bool HasExecutor() const noexcept
        {
            return m_environment.executor.IsValid();
        }

        /// @brief Rebinds this context to an executor reference.
        void BindExecutor(NGIN::Execution::ExecutorRef executor) noexcept
        {
            m_environment.executor = executor;
        }

        /// @brief Rebinds this context to a borrowed compatible scheduler.
        template<typename TScheduler>
        void BindExecutor(TScheduler& scheduler) noexcept
        {
            m_environment.executor = NGIN::Execution::ExecutorRef::From(scheduler);
        }

        /// @brief Replaces cancellation state and releases any linked-token ownership.
        void BindCancellationToken(CancellationToken cancellation) noexcept
        {
            m_cancellationOwner.reset();
            m_environment.cancellation = std::move(cancellation);
        }

        /// @brief Returns a copy of this context with replacement cancellation state.
        [[nodiscard]] TaskContext WithCancellationToken(CancellationToken cancellation) const noexcept
        {
            TaskContext copy = *this;
            copy.BindCancellationToken(std::move(cancellation));
            return copy;
        }

        /// @brief Rebinds cancellation to a token canceled when either current or supplied state is canceled.
        /// @throws std::bad_alloc if linked-token ownership cannot be allocated; leaves this context unchanged.
        void BindLinkedCancellationToken(CancellationToken cancellation)
        {
            if (!m_environment.cancellation.HasState())
            {
                BindCancellationToken(std::move(cancellation));
                return;
            }

            if (!cancellation.HasState() || cancellation == m_environment.cancellation)
            {
                return;
            }

            std::shared_ptr<detail::LinkedCancellationState> linked =
                    std::allocate_shared<detail::LinkedCancellationState>(
                            std::pmr::polymorphic_allocator<detail::LinkedCancellationState>(m_environment.resource),
                            m_environment.resource);
            linked->Link({m_environment.cancellation, cancellation});

            if (m_cancellationOwner)
            {
                struct OwnerChain final
                {
                    std::shared_ptr<void> previous {};
                    std::shared_ptr<void> current {};
                };

                std::shared_ptr<OwnerChain> chain = std::allocate_shared<OwnerChain>(std::pmr::polymorphic_allocator<OwnerChain>(m_environment.resource));
                chain->previous                   = m_cancellationOwner;
                chain->current                    = linked;
                m_cancellationOwner               = std::move(chain);
            }
            else
            {
                m_cancellationOwner = linked;
            }

            m_environment.cancellation = linked->source.GetToken();
        }

        /// @brief Returns a copy whose cancellation observes both the current and supplied tokens.
        /// @throws std::bad_alloc if linked-token ownership cannot be allocated.
        [[nodiscard]] TaskContext WithLinkedCancellationToken(CancellationToken cancellation) const
        {
            TaskContext copy = *this;
            copy.BindLinkedCancellationToken(std::move(cancellation));
            return copy;
        }

        /// @brief Returns the bound executor reference.
        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() const noexcept
        {
            return m_environment.executor;
        }

        /// @brief Returns the bound cancellation token.
        [[nodiscard]] CancellationToken GetCancellationToken() const noexcept
        {
            return m_environment.cancellation;
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_environment.cancellation.IsCancellationRequested();
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool CheckCancellation() const noexcept
        {
            return m_environment.cancellation.IsCancellationRequested();
        }

        /// @brief Creates an awaiter that reschedules the current task through the bound executor.
        [[nodiscard]] auto YieldNow() const noexcept
        {
            return YieldAwaiter {m_environment.executor, m_environment.cancellation};
        }

        /// @brief Creates a cancellation-aware awaiter for a time-quantity delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] auto Delay(const TUnit& duration) const noexcept
        {
            return DelayAwaiter<TUnit> {m_environment.executor, m_environment.cancellation, duration};
        }

    private:
        AsyncEnvironment      m_environment {};
        std::shared_ptr<void> m_cancellationOwner {};
    };
}// namespace NGIN::Async
