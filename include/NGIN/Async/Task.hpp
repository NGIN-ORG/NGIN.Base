/// @file Task.hpp
/// @brief Cold Task coroutines and their running Operation handles.
#pragma once

#include <NGIN/Execution/detail/WorkBudget.hpp>
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncConfig.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Completion.hpp>
#include <NGIN/Async/CurrentEnvironment.hpp>
#include <NGIN/Async/NoError.hpp>
#include <NGIN/Async/OperationRef.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Async/detail/TaskExecutionState.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Async
{
    /// @brief Common marker base for Task specializations.
    class BaseTask
    {
    };

    template<typename T, typename E = NoError>
    class Task;

    template<typename E>
    class Task<void, E>;

    template<typename T, typename E = NoError>
    class Operation;

    namespace detail
    {
        struct OperationAccess;
        template<typename T, typename E>
        class ProducerOperation;
        [[nodiscard]] inline AsyncFault MakeSchedulingFault(
                const AsyncFaultCode                 code,
                const NGIN::Execution::ScheduleError error) noexcept
        {
            AsyncFault fault;
            fault.code   = code;
            fault.native = static_cast<int>(error);
            return fault;
        }

        struct PromiseRuntimeCommon
        {
            using CompletionHandler = void (*)(std::coroutine_handle<>, std::coroutine_handle<>) noexcept;

            using ContinuationState = TaskContinuationState;

            enum class ContinuationInstallResult : std::uint8_t
            {
                Installed,
                AlreadyCompleted,
                AlreadyInstalled,
            };

            static_assert(std::atomic<ContinuationState>::is_always_lock_free,
                          "Task continuation state must remain lock-free on supported platforms");
            static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                          "Task frame-reference state must remain lock-free on supported platforms");

            using ExecutionState = TaskExecutionState;

            TaskExecutionStateOwner                 m_execution {};
            std::atomic<bool>                       m_finished {false};
            std::coroutine_handle<>                 m_continuation {};
            CompletionHandler                       m_completionHandler {};
            ContinuationState                       m_sequentialContinuation {ContinuationState::Empty};
            TaskContext*                            m_ctx {nullptr};
            CancellationToken                       m_waitCancellation {};
            AsyncEnvironment                        m_environment {};
            std::atomic<std::uint32_t>              m_frameReferences {1};
            std::atomic<bool>                       m_executionReferenceActive {false};
            PromiseRuntimeCommon*                   m_continuationTarget {};
            NGIN::Execution::CompletionReservation* m_retirementObserver {};

            // Sequential children borrow the root's admitted delivery slot. Only
            // one suspension in a chain can publish its next continuation.
            PromiseRuntimeCommon*     m_chainOwner {};
            void*                     m_sequentialTaskOwner {};
            static constexpr unsigned CHAIN_WORK_BUDGET = 256;

            [[nodiscard]] PromiseRuntimeCommon& ChainOwner() noexcept
            {
                return m_chainOwner ? *m_chainOwner : *this;
            }

            [[nodiscard]] bool HasContinuation() const noexcept
            {
                const auto* owner = m_chainOwner ? m_chainOwner : this;
                return owner->m_execution && owner->m_execution->m_taskContinuation.IsValid();
            }

            [[nodiscard]] NGIN::Execution::CompletionReservation& ContinuationSlot() noexcept
            {
                return ChainState().m_taskContinuation;
            }

            [[nodiscard]] ExecutionState& ChainState() noexcept
            {
                auto& owner = ChainOwner();
                assert(owner.m_execution);
                return *owner.m_execution;
            }

            [[nodiscard]] NGIN::Execution::ScheduleResult RebindExecutor(NGIN::Execution::ExecutorRef executor) noexcept
            {
                auto result = ChainState().Rebind(executor);
                if (result)
                    m_environment.executor = executor;
                return result;
            }

            [[nodiscard]] bool CanTransfer() noexcept
            {
                auto& state = ChainState();
                return m_environment.executor.IsCurrent() && ++state.m_chainWork <= CHAIN_WORK_BUDGET &&
                       NGIN::Execution::detail::WorkBudget::TryConsume();
            }

            template<typename Handle>
            std::coroutine_handle<> StartSequential(Handle self, PromiseRuntimeCommon& parent) noexcept
            {
                m_chainOwner = &parent.ChainOwner();
                if (CanTransfer())
                    return self;
                QueueContinuation(NGIN::Execution::WorkItem(self));
                return std::noop_coroutine();
            }

            // A successful sequential final suspension returns the parent handle
            // instead of invoking resume recursively or allocating another ticket.
            template<typename Handle>
            std::coroutine_handle<> Finish(Handle self) noexcept
            {
                if (!m_chainOwner || !self.promise().IsSucceeded() || m_waitCancellation.IsCancellationRequested() ||
                    m_frameReferences.load(std::memory_order_acquire) != 2)
                {
                    MarkFinishedAndResume(self);
                    return std::noop_coroutine();
                }
                bool expected = false;
                if (!m_finished.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    return std::noop_coroutine();
                const auto continuation = CompleteContinuation();
                auto*      target       = m_continuationTarget;
                assert(target && continuation);
                target->m_environment.executor = m_environment.executor;
                const bool transfer            = CanTransfer();
                ReleaseExecutionReference(self);
                target->ReleaseFrameReference(continuation);
                if (transfer)
                    return continuation;
                target->QueueContinuation(NGIN::Execution::WorkItem(continuation));
                return std::noop_coroutine();
            }

            /// Queues one continuation using the task's pre-admitted storage.
            void QueueContinuation(NGIN::Execution::WorkItem work) noexcept
            {
                auto& state = ChainState();
                assert(state.m_taskContinuation.IsValid());
                state.m_pendingContinuation = std::move(work);
                state.m_taskContinuation.Schedule();
            }

            /// Ends a queued continuation's retention before entering user code.
            /// Active execution keeps a pending task alive until it completes.
            template<typename Handle>
            static void ResumeRetained(Handle awaiting) noexcept
            {
                auto&      promise = awaiting.promise();
                const bool pending = !promise.m_finished.load(std::memory_order_acquire);
                assert(!pending || promise.m_executionReferenceActive.load(std::memory_order_acquire));
                promise.ReleaseFrameReference(awaiting);
                if (pending)
                    awaiting.resume();
            }

            /// Reserves one reusable continuation and lifetime slot for a started task.
            /// Unsupported executors may run standalone tasks, but cannot suspend a
            /// parent waiting for a child without reserving its terminal delivery.
            [[nodiscard]] NGIN::Execution::ScheduleResult ReserveExecution() noexcept
            {
                if (!m_execution)
                {
                    m_execution = ExecutionState::Create(m_sequentialContinuation, m_environment.resource);
                    if (!m_execution)
                        return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
                }
                return m_execution->Reserve(m_environment.executor);
            }

            /// Submits one token-free step using the active task's reserved completion.
            /// Execution owns the frame until submission and dispatch/discard have both
            /// finished. Neither side accesses the frame after publishing its last event.
            template<typename Handle>
            [[nodiscard]] NGIN::Execution::ScheduleResult SubmitTracked(
                    Handle self, bool timed = false, NGIN::Time::TimePoint until = {}) noexcept
            {
                assert(HasContinuation());
                assert(m_executionReferenceActive.load(std::memory_order_acquire));
                ChainState().m_submissionFlags.store(0, std::memory_order_relaxed);
                NGIN::Execution::WorkItem work {SubmissionWork<Handle>(self)};
                const auto                result = timed ? m_environment.executor.ExecuteAt(std::move(work), until) : m_environment.executor.Execute(std::move(work));
                if (!result)
                {
                    // Rejection destroys the submitted item before returning. No
                    // dispatch can finish before admission arms the handshake.
                    CompleteSubmissionFailure(self, result.error());
                    return result;
                }
                const unsigned observed = ChainState().m_submissionFlags.fetch_or(SubmissionArmed, std::memory_order_acq_rel);
                if (observed & (SubmissionReady | SubmissionDiscarded))
                    QueueSubmissionResult(self, (observed & SubmissionReady) != 0);
                return result;
            }

            /// Admits and starts a task. Untracked executors retain standalone support.
            template<typename Handle>
            [[nodiscard]] NGIN::Execution::ScheduleResult StartExecution(Handle self) noexcept
            {
                auto result = ReserveExecution();
                if (result && HasContinuation())
                    return SubmitTracked(self);
                if (result)
                    result = m_environment.executor.Execute(self);
                if (!result)
                    CompleteSubmissionFailure(self, result.error());
                return result;
            }

            template<typename Handle>
            static void DeliverChild(std::coroutine_handle<> child) noexcept
            {
                Handle     self              = Handle::from_address(child.address());
                auto&      promise           = self.promise();
                const auto continuation      = promise.m_continuation;
                const auto completionHandler = promise.m_completionHandler;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                const auto propagateException = promise.m_setChildException;
                const auto exception          = promise.m_exception;
#endif
                // The awaiting Task/Operation owner retains the child while its
                // handler consumes the result. End delivery's extra hold first,
                // so unwinding the parent can destroy child locals before the
                // parent locals they borrow. Never access the child afterward.
                promise.ReleaseFrameReference(self);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                if (propagateException && exception)
                    propagateException(exception, continuation);
#endif
                if (completionHandler)
                    completionHandler(child, continuation);
                else
                    continuation.resume();
            }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr m_exception {};
            using ExceptionPropagator = void (*)(std::exception_ptr, std::coroutine_handle<>) noexcept;
            ExceptionPropagator m_setChildException {};

            void SetChildException(std::exception_ptr ex) noexcept
            {
                if (!m_exception)
                {
                    m_exception = ex;
                }
            }
#endif

            PromiseRuntimeCommon() = default;

            explicit PromiseRuntimeCommon(TaskContext& ctx) noexcept
                : m_ctx(&ctx), m_environment(ctx.GetEnvironment())
            {
            }

            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit PromiseRuntimeCommon(TaskContext& ctx, Args&&...) noexcept
                : PromiseRuntimeCommon(ctx)
            {
            }

            /// @brief Adds the reference held by started coroutine execution.
            /// @details The owning Task or Operation contributes the initial reference. Started execution and each
            /// child continuation retaining this frame contribute another reference. Destruction is therefore
            /// linearized by the final `fetch_sub` rather than by a detached/completed check-then-act race.
            void AcquireExecutionReference() noexcept
            {
                [[maybe_unused]] const std::uint32_t previous =
                        m_frameReferences.fetch_add(1, std::memory_order_relaxed);
                assert(previous != std::numeric_limits<std::uint32_t>::max());
                [[maybe_unused]] const bool wasActive =
                        m_executionReferenceActive.exchange(true, std::memory_order_release);
                assert(!wasActive);
            }

            /// @brief Retains a frame while another coroutine stores a continuation into it.
            void RetainFrameReference() noexcept
            {
                [[maybe_unused]] const std::uint32_t previous =
                        m_frameReferences.fetch_add(1, std::memory_order_relaxed);
                assert(previous != 0 && previous != std::numeric_limits<std::uint32_t>::max());
            }

            /// @brief Releases one frame reference and destroys the suspended frame when it was the final reference.
            /// @note Callers must not access the promise or coroutine frame after this function.
            template<typename Handle>
            void ReleaseFrameReference(Handle self) noexcept
            {
                const std::uint32_t previous = m_frameReferences.fetch_sub(1, std::memory_order_acq_rel);
                assert(previous != 0);
                if (previous == 1)
                {
                    auto* retired = m_retirementObserver;
                    self.destroy();
                    if (retired)
                        retired->Schedule();
                }
            }

            /// @brief Atomically publishes one continuation and its handlers.
            /// @details The lock-free transition table is `Empty -> Installing -> Installed -> Completed`, or
            /// `Empty -> Completed` when completion wins. `Installing` prevents completion from observing partially
            /// published fields; release publication and acquire completion make the handler and payload visible.
            [[nodiscard]] ContinuationInstallResult TryInstallContinuation(
                    const std::coroutine_handle<> continuation,
                    const CompletionHandler       completionHandler
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    ,
                    const ExceptionPropagator exceptionPropagator
#endif
                    ,
                    PromiseRuntimeCommon*                   target           = nullptr,
                    NGIN::Execution::CompletionReservation* delivery         = nullptr,
                    NGIN::Execution::CompletionReservation* reusableObserver = nullptr) noexcept
            {
                if (m_execution)
                {
                    ContinuationState expected = ContinuationState::Empty;
                    if (!m_execution->m_continuationState.compare_exchange_strong(
                                expected, ContinuationState::Installing,
                                std::memory_order_acquire, std::memory_order_acquire))
                        return expected == ContinuationState::Completed
                                       ? ContinuationInstallResult::AlreadyCompleted
                                       : ContinuationInstallResult::AlreadyInstalled;
                }
                else
                {
                    if (m_sequentialContinuation == ContinuationState::Completed)
                        return ContinuationInstallResult::AlreadyCompleted;
                    if (m_sequentialContinuation != ContinuationState::Empty)
                        return ContinuationInstallResult::AlreadyInstalled;
                    m_sequentialContinuation = ContinuationState::Installing;
                }

                m_continuation       = continuation;
                m_completionHandler  = completionHandler;
                m_continuationTarget = target;
                if (reusableObserver || delivery)
                {
                    assert(m_execution);
                    m_execution->m_reusableObserver = reusableObserver;
                    if (delivery)
                        m_execution->m_foreignContinuation = std::move(*delivery);
                }
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_setChildException = exceptionPropagator;
#endif
                if (m_execution)
                    m_execution->m_continuationState.store(ContinuationState::Installed, std::memory_order_release);
                else
                    m_sequentialContinuation = ContinuationState::Installed;
                return ContinuationInstallResult::Installed;
            }

            /// @brief Claims the installed continuation while publishing terminal completion.
            [[nodiscard]] std::coroutine_handle<> CompleteContinuation() noexcept
            {
                if (!m_execution)
                {
                    const auto state = std::exchange(m_sequentialContinuation, ContinuationState::Completed);
                    assert(state != ContinuationState::Installing);
                    return state == ContinuationState::Installed ? m_continuation : std::coroutine_handle<> {};
                }
                ContinuationState state = m_execution->m_continuationState.load(std::memory_order_acquire);
                for (;;)
                {
                    if (state == ContinuationState::Installing)
                    {
                        state = m_execution->m_continuationState.load(std::memory_order_acquire);
                        continue;
                    }
                    if (state == ContinuationState::Completed)
                        return {};
                    if (m_execution->m_continuationState.compare_exchange_weak(
                                state,
                                ContinuationState::Completed,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire))
                    {
                        return state == ContinuationState::Installed ? m_continuation : std::coroutine_handle<> {};
                    }
                }
            }

            [[nodiscard]] bool HasInstalledContinuation() const noexcept
            {
                return (m_execution ? m_execution->m_continuationState.load(std::memory_order_acquire)
                                    : m_sequentialContinuation) == ContinuationState::Installed;
            }

            template<typename Handle>
            bool MarkFinishedAndResume(Handle self) noexcept
            {
                bool expected = false;
                if (!m_finished.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return false;
                }

                if (m_execution)
                {
                    if (m_execution->m_terminalObserver)
                        m_execution->m_terminalObserver(m_execution->m_terminalContext);
                    m_execution->m_finishedCondition.NotifyAll();
                }

                const std::coroutine_handle<> continuation = CompleteContinuation();

                PromiseRuntimeCommon* target           = m_continuationTarget;
                auto                  delivery         = m_execution ? std::move(m_execution->m_foreignContinuation) : NGIN::Execution::CompletionReservation {};
                auto*                 reusableObserver = m_execution ? m_execution->m_reusableObserver : nullptr;
                if (continuation)
                    RetainFrameReference();
                // Release execution before publishing the observer. Observers can
                // then destroy a completed Operation and its frame during joining.
                auto execution = m_execution ? std::move(m_execution->m_taskContinuation) : NGIN::Execution::CompletionReservation {};
                ReleaseExecutionReference(self);
                if (continuation && target)
                {
                    NGIN::Execution::WorkItem work([self] { DeliverChild<Handle>(self); });
                    const auto                executor = target->ChainState().m_executor;
                    // Completion and ownership are published; the handler performs any
                    // remaining child retirement before entering the parent's body.
                    if (!executor.TryDispatchInline(work))
                        target->QueueContinuation(std::move(work));
                }
                else if (delivery.IsValid())
                    delivery.Dispatch();
                else if (reusableObserver)
                    reusableObserver->Schedule();
                return true;
            }

        private:
            enum SubmissionFlag : unsigned
            {
                SubmissionArmed     = 1,
                SubmissionReady     = 2,
                SubmissionDiscarded = 4,
            };

            template<typename Handle>
            struct SubmissionWork final
            {
                explicit SubmissionWork(Handle incoming) noexcept : handle(incoming) {}
                SubmissionWork(SubmissionWork&& other) noexcept : handle(std::exchange(other.handle, {})) {}
                SubmissionWork(const SubmissionWork&) = delete;
                ~SubmissionWork()
                {
                    if (Handle self = std::exchange(handle, {}))
                        self.promise().SignalSubmission(self, SubmissionDiscarded);
                }
                void operator()() noexcept
                {
                    if (Handle self = std::exchange(handle, {}))
                        self.promise().SignalSubmission(self, SubmissionReady);
                }
                Handle handle;
            };

            template<typename Handle>
            void SignalSubmission(Handle self, unsigned event) noexcept
            {
                const unsigned observed = ChainState().m_submissionFlags.fetch_or(event, std::memory_order_acq_rel);
                if (observed & SubmissionArmed)
                {
                    if (event == SubmissionReady)
                        self.resume();
                    else
                        QueueSubmissionResult(self, false);
                }
            }

            template<typename Handle>
            void QueueSubmissionResult(Handle self, bool ready) noexcept
            {
                QueueContinuation(NGIN::Execution::WorkItem([self, ready] {
                    if (ready)
                        self.resume();
                    else
                        CompleteSubmissionFailure(self, NGIN::Execution::ScheduleError::Stopped);
                }));
            }

            template<typename Handle>
            static void CompleteSubmissionFailure(Handle self, NGIN::Execution::ScheduleError error) noexcept
            {
                self.promise().SetFault(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, error));
                self.promise().MarkFinishedAndResume(self);
            }

            template<typename Handle>
            void ReleaseExecutionReference(Handle self) noexcept
            {
                if (m_executionReferenceActive.exchange(false, std::memory_order_acq_rel))
                {
                    ReleaseFrameReference(self);
                }
            }
        };

        template<typename T, typename E>
        struct PromiseStorage : PromiseRuntimeCommon
        {
            using DomainErrorType = E;

            std::optional<Completion<T, E>> m_completion {};

            using Base = PromiseRuntimeCommon;
            using Base::Base;

            [[nodiscard]] bool HasCompletion() const noexcept
            {
                return m_completion.has_value();
            }

            [[nodiscard]] bool IsSucceeded() const noexcept
            {
                return m_completion.has_value() && m_completion->Succeeded();
            }

            void SetCompletion(Completion<T, E> completion)
            {
                if (!m_completion.has_value())
                {
                    m_completion.emplace(std::move(completion));
                }
            }

            void SetDomainError(E error)
            {
                SetCompletion(Completion<T, E>::DomainFailure(std::move(error)));
            }

            void SetStopped(NGIN::Async::StopReason reason = NGIN::Async::StopReason::Requested)
            {
                SetCompletion(Completion<T, E>::Stopped(reason));
            }

            void SetError(AsyncError<E> error)
            {
                SetCompletion(Completion<T, E>::Failure(std::move(error)));
            }

            void SetFault(AsyncFault fault)
            {
                SetCompletion(Completion<T, E>::Faulted(std::move(fault)));
            }

            [[nodiscard]] Completion<T, E> TakeCompletion()
            {
                if (!m_completion.has_value())
                {
                    return Completion<T, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                }

                return std::move(*m_completion);
            }

            template<typename ChildPromise>
            void PropagateFromChild(ChildPromise& child) noexcept
            {
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                if (child.m_exception)
                    this->SetChildException(child.m_exception);
#endif
                if (!child.m_completion.has_value() || child.m_completion->Succeeded())
                {
                    return;
                }

                if (child.m_completion->IsStopped())
                    SetStopped(child.m_completion->StopReason());
                else
                    SetError(std::move(*child.m_completion).Error());
            }

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                this->m_exception       = std::current_exception();
                fault.capturedException = this->m_exception;
#endif
                SetFault(std::move(fault));
#else
                std::terminate();
#endif
            }
        };

        template<typename E>
        struct PromiseStorage<void, E> : PromiseRuntimeCommon
        {
            using DomainErrorType = E;

            std::optional<Completion<void, E>> m_completion {};

            using Base = PromiseRuntimeCommon;
            using Base::Base;

            [[nodiscard]] bool HasCompletion() const noexcept
            {
                return m_completion.has_value();
            }

            [[nodiscard]] bool IsSucceeded() const noexcept
            {
                return m_completion.has_value() && m_completion->Succeeded();
            }

            void SetCompletion(Completion<void, E> completion)
            {
                if (!m_completion.has_value())
                {
                    m_completion.emplace(std::move(completion));
                }
            }

            void SetDomainError(E error)
            {
                SetCompletion(Completion<void, E>::DomainFailure(std::move(error)));
            }

            void SetStopped(NGIN::Async::StopReason reason = NGIN::Async::StopReason::Requested)
            {
                SetCompletion(Completion<void, E>::Stopped(reason));
            }

            void SetError(AsyncError<E> error)
            {
                SetCompletion(Completion<void, E>::Failure(std::move(error)));
            }

            void SetFault(AsyncFault fault)
            {
                SetCompletion(Completion<void, E>::Faulted(std::move(fault)));
            }

            [[nodiscard]] Completion<void, E> TakeCompletion()
            {
                if (!m_completion.has_value())
                {
                    return Completion<void, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                }

                return std::move(*m_completion);
            }

            template<typename ChildPromise>
            void PropagateFromChild(ChildPromise& child) noexcept
            {
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                if (child.m_exception)
                    this->SetChildException(child.m_exception);
#endif
                if (!child.m_completion.has_value() || child.m_completion->Succeeded())
                {
                    return;
                }

                if (child.m_completion->IsStopped())
                    SetStopped(child.m_completion->StopReason());
                else
                    SetError(std::move(*child.m_completion).Error());
            }

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                this->m_exception       = std::current_exception();
                fault.capturedException = this->m_exception;
#endif
                SetFault(std::move(fault));
#else
                std::terminate();
#endif
            }
        };

        template<typename TTask>
        struct IsTaskType final : std::false_type
        {
        };

        template<typename TValue, typename TError>
        struct IsTaskType<Task<TValue, TError>> final : std::true_type
        {
        };

        template<typename TTask>
        inline constexpr bool IsTaskTypeV = IsTaskType<std::remove_cvref_t<TTask>>::value;

        template<typename TTask>
        using TaskValueType = typename std::remove_reference_t<TTask>::ValueType;

        template<typename ParentPromise, typename ChildPromise>
        bool InheritChildExecutionContext(ChildPromise& child, ParentPromise& parent) noexcept
        {
            child.m_environment = parent.m_environment;
            if (child.m_ctx == nullptr)
                child.m_ctx = parent.m_ctx;
            return child.m_environment.executor.IsValid();
        }
    }// namespace detail

    /// @brief Cold single-result coroutine that starts only when spawned, awaited, or synchronously run.
    /// @tparam T Successful result type.
    /// @tparam E Recoverable domain-error type.
    template<typename T, typename E>
    class Task final : public BaseTask
    {
    public:
        using ValueType = T;
        using ErrorType = E;

        /// @brief Coroutine promise that stores a typed task completion and continuation.
        struct promise_type final : detail::PromiseStorage<T, E>
        {
            using Base = detail::PromiseStorage<T, E>;
            using Base::Base;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;
            using Base::SetStopped;

            /// @brief Returns the task that owns this coroutine frame.
            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            /// @brief Keeps the task cold until explicitly started or awaited.
            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter final
            {
                /// @brief Always enters final suspension.
                bool await_ready() noexcept { return false; }

                /// @brief Publishes completion and releases the active-execution frame reference.
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    return h.promise().Finish(h);
                }

                /// @brief Performs no resume-time work.
                void await_resume() noexcept {}
            };

            /// @brief Returns the final awaiter that publishes task completion.
            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            /// @brief Completes the task successfully with a value.
            void return_value(T value)
            {
                SetCompletion(Completion<T, E>::Success(std::move(value)));
            }

            /// @brief Completes the task from an explicit asynchronous outcome.
            void return_value(Completion<T, E> completion)
            {
                SetCompletion(std::move(completion));
            }

            /// @brief Completes the task from an `Expected`, mapping errors to domain errors.
            void return_value(NGIN::Utilities::Expected<T, E> result)
            {
                if (!result)
                {
                    SetDomainError(std::move(result).error());
                    return;
                }

                SetCompletion(Completion<T, E>::Success(std::move(result).value()));
            }

            /// @brief Completes the task with an unexpected/domain error wrapper.
            void return_value(NGIN::Utilities::Unexpected<E> error)
            {
                SetDomainError(error.error());
            }

            /// @brief Completes the task with a domain error when value and error types differ.
            void return_value(E error)
                requires(!std::is_same_v<T, E>)
            {
                SetDomainError(std::move(error));
            }
        };

        /// @brief Coroutine handle type owned by this task.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty task.
        Task() noexcept = default;

        /// @brief Constructs an inline ready value; only payload construction may allocate.
        [[nodiscard]] static Task FromValue(T value)
        {
            return FromCompletion(Completion<T, E>::Success(std::move(value)));
        }

        /// @brief Constructs an already-complete task without a coroutine frame or scheduling.
        [[nodiscard]] static Task FromCompletion(Completion<ValueType, E> completion)
        {
            Task task;
            task.m_ready.emplace(std::move(completion));
            return task;
        }

        /// @brief Takes ownership of a cold task coroutine handle.
        explicit Task(handle_type h) noexcept
            : m_handle(h)
        {
        }

        /// @brief Transfers ownership and start state from another task.
        Task(Task&& other) noexcept(std::is_nothrow_move_constructible_v<Completion<ValueType, E>>)
            : m_ready(std::move(other.m_ready)), m_handle(other.m_handle)
        {
            other.m_ready.reset();
            other.m_handle = nullptr;
        }

        /// @brief Releases this frame and transfers ownership from another task.
        Task& operator=(Task&& other) noexcept(std::is_nothrow_move_constructible_v<Completion<ValueType, E>>)
        {
            if (this != &other)
            {
                ReleaseHandle();

                m_ready.reset();
                if (other.m_ready)
                    m_ready.emplace(std::move(*other.m_ready));
                other.m_ready.reset();
                m_handle = other.m_handle;

                other.m_handle = nullptr;
            }
            return *this;
        }

        /// @brief Tasks are non-copyable because they uniquely own coroutine frames.
        Task(const Task&) = delete;
        /// @brief Tasks are non-copy-assignable because they uniquely own coroutine frames.
        Task& operator=(const Task&) = delete;

        /// @brief Releases an unstarted frame or detaches ownership from running work.
        ~Task()
        {
            ReleaseHandle();
        }

        /// @brief Returns whether execution has been started.
        [[nodiscard]] bool IsStarted() const noexcept
        {
            return m_handle && (m_handle.promise().m_executionReferenceActive.load(std::memory_order_acquire) ||
                                m_handle.promise().m_finished.load(std::memory_order_acquire));
        }

        /// @brief Returns whether the coroutine has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_ready.has_value() || (m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire));
        }

        /// @brief Returns whether the task completed in the error channel.
        [[nodiscard]] bool HasError() const noexcept
        {
            return m_ready ? m_ready->HasError() : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->HasError();
        }

        /// @brief Returns whether the task completed in the stopped channel.
        [[nodiscard]] bool IsStopped() const noexcept
        {
            return m_ready ? m_ready->IsStopped() : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsStopped();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        /// @brief Returns the exception captured from an unhandled coroutine exception, if any.
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!IsCompleted())
            {
                return {};
            }
            if (m_ready)
                return (m_ready->HasError() && m_ready->Error().IsFault()) ? m_ready->Error().Fault().capturedException : std::exception_ptr {};
            return m_handle.promise().m_exception;
        }
#endif

        /// @brief Consumes the child and returns its full outcome without propagating failure.
        class CompletionAwaiter final
        {
        public:
            explicit CompletionAwaiter(Task&& task) : m_task(std::move(task)) {}
            bool await_ready() const noexcept
            {
                return m_task.m_ready || !m_task.m_handle || m_task.IsCompleted();
            }
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                return m_task.AwaitCompletionSuspend(awaiting);
            }
            Completion<ValueType, E> await_resume()
            {
                if (m_task.m_ready)
                    return std::move(*m_task.m_ready);
                if (m_task.m_handle)
                    return m_task.m_handle.promise().TakeCompletion();
                return Completion<ValueType, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
            }

        private:
            Task m_task;
        };

        /// @brief Consumes this task; the awaiting body receives value, error, or cancellation.
        [[nodiscard]] CompletionAwaiter AsCompletion() { return CompletionAwaiter {std::move(*this)}; }

        /// @brief Owns the consumed child and propagates failures to its parent.
        class OwnedPropagationAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited task.
            explicit OwnedPropagationAwaiter(Task&& task)
                : m_task(std::move(task))
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return (m_task.m_ready && m_task.m_ready->Succeeded()) || (m_task.m_handle &&
                                                                           m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                                                                           m_task.m_handle.promise().IsSucceeded());
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Moves the successful child value into the parent coroutine.
            [[nodiscard]] T await_resume()
            {
                if (m_task.m_ready)
                    return std::move(m_task.m_ready->Value());
                assert(m_task.m_handle);
                promise_type& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            Task m_task;
        };

        /// @brief Owns the consumed child and observes a task context's cancellation token.
        class CancellablePropagationAwaiter final
        {
        public:
            /// @brief Consumes the task and borrows the cancellation context.
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx)
                : m_task(std::move(task)), m_ctx(&ctx)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return (m_task.m_ready && m_task.m_ready->Succeeded()) || (!m_ctx->IsCancellationRequested() && m_task.m_handle &&
                                                                           m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                                                                           m_task.m_handle.promise().IsSucceeded());
            }

            /// @brief Starts the child or cancels the compatible parent when the context is canceled.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                if (m_task.m_ready || !m_task.m_handle)
                    return m_task.template AwaitSuspend<ParentPromise>(awaiting);

                if (m_ctx == nullptr || (!m_task.IsStarted() && m_ctx->IsCancellationRequested()))
                {
                    awaiting.promise().SetStopped(m_ctx ? m_ctx->GetCancellationToken().GetStopReason() : StopReason::Requested);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (m_task.m_handle)
                    m_task.m_handle.promise().m_waitCancellation = m_ctx->GetCancellationToken();

                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Moves the successful child value into the parent coroutine.
            [[nodiscard]] T await_resume()
            {
                if (m_task.m_ready)
                    return std::move(m_task.m_ready->Value());
                assert(m_task.m_handle);
                promise_type& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task         m_task;
            TaskContext* m_ctx {};
        };

        /// @brief Consumes an lvalue task; subsequent awaits report InvalidTaskUsage.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() &
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Creates an owning failure-propagating awaiter for an rvalue task.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() &&
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Consumes this child and observes cancellation while joining its terminal result.
        /// @details Canceling the wait does not end backend access or detach the child.
        /// The child's own context controls cancellation of its operations.
        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx)
        {
            return CancellablePropagationAwaiter {*this, ctx};
        }

        /// @brief Creates a value-less task that completes after a cancellation-aware delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        static Task<void, E> Delay(TaskContext& ctx, const TUnit& duration)
        {
            co_await ctx.Delay(duration);
            co_return;
        }

    private:
        friend struct detail::OperationAccess;
        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);
        template<typename, typename>
        friend class detail::ProducerOperation;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            handle_type handle = m_handle;
            m_handle           = {};
            if (handle)
            {
                handle.promise().AcquireExecutionReference();
            }
            return handle;
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitSuspend(std::coroutine_handle<ParentPromise> awaiting)
        {
            static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                          "Await propagation requires identical Task error types.");

            if (m_ready)
            {
                if (m_ready->Succeeded())
                    return awaiting;
                if (m_ready->IsStopped())
                    awaiting.promise().SetStopped(m_ready->StopReason());
                else
                    awaiting.promise().SetError(std::move(*m_ready).Error());
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!m_handle)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            promise_type& child = m_handle.promise();
            if (awaiting.promise().m_finished.load(std::memory_order_acquire))
            {
                return std::noop_coroutine();
            }
            if (child.m_finished.load(std::memory_order_acquire))
            {
                if (child.m_waitCancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }
                if (child.IsSucceeded())
                {
                    return awaiting;
                }

                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!awaiting.promise().HasContinuation())
            {
                awaiting.promise().SetFault(detail::MakeSchedulingFault(
                        AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }
            awaiting.promise().RetainFrameReference();
            const detail::PromiseRuntimeCommon::ContinuationInstallResult installResult = child.TryInstallContinuation(
                    awaiting,
                    &Task::template PropagateChildCompletion<ParentPromise>
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    ,
                    &Task::template PropagateChildException<ParentPromise>
#endif
                    ,
                    &awaiting.promise());
            if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyCompleted)
            {
                if (child.m_waitCancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                    awaiting.promise().ReleaseFrameReference(awaiting);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }
                if (child.IsSucceeded())
                {
                    awaiting.promise().ReleaseFrameReference(awaiting);
                    return awaiting;
                }
                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().ReleaseFrameReference(awaiting);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }
            if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                awaiting.promise().ReleaseFrameReference(awaiting);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!IsStarted())
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                child.m_sequentialTaskOwner = this;
                if (child.m_environment.executor == awaiting.promise().m_environment.executor)
                    return child.StartSequential(m_handle, awaiting.promise());
                (void) child.StartExecution(m_handle);
            }

            return std::noop_coroutine();
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitCompletionSuspend(std::coroutine_handle<ParentPromise> awaiting)
        {
            auto& parent = awaiting.promise();
            if (parent.m_finished.load(std::memory_order_acquire))
                return std::noop_coroutine();
            if (!parent.HasContinuation())
            {
                m_ready.emplace(Completion<ValueType, E>::Faulted(detail::MakeSchedulingFault(
                        AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected)));
                return awaiting;
            }
            auto& child = m_handle.promise();
            parent.RetainFrameReference();
            const auto installed = child.TryInstallContinuation(awaiting,
                                                                &Task::template ObserveChildCompletion<ParentPromise>
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                                                                ,
                                                                nullptr
#endif
                                                                ,
                                                                &parent);
            if (installed != detail::PromiseRuntimeCommon::ContinuationInstallResult::Installed)
            {
                parent.ReleaseFrameReference(awaiting);
                if (installed == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
                    m_ready.emplace(Completion<ValueType, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)));
                return awaiting;
            }
            if (!IsStarted())
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(child, parent))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }
                child.m_sequentialTaskOwner = this;
                if (child.m_environment.executor == parent.m_environment.executor)
                    return child.StartSequential(m_handle, parent);
                (void) child.StartExecution(m_handle);
            }
            return std::noop_coroutine();
        }

        template<typename ParentPromise>
        static void ObserveChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            const auto childHandle                        = handle_type::from_address(self.address());
            const auto parentHandle                       = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto&      child                              = childHandle.promise();
            parentHandle.promise().m_environment.executor = child.m_environment.executor;
            if (child.m_sequentialTaskOwner && child.m_frameReferences.load(std::memory_order_acquire) > 1)
            {
                auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
#if NGIN_ASYNC_HAS_EXCEPTIONS
                try
                {
#endif
                    owner->m_ready.emplace(std::move(*child.m_completion));
#if NGIN_ASYNC_HAS_EXCEPTIONS
                } catch (...)
                {
                    auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    fault.capturedException = std::current_exception();
#endif
                    owner->m_ready.emplace(Completion<ValueType, E>::Faulted(std::move(fault)));
                }
#endif
                CompleteParentAfterRetirement(childHandle, parentHandle, true);
                return;
            }
            detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
        }

        void ReleaseHandle() noexcept
        {
            if (!m_handle)
            {
                return;
            }

            handle_type handle = m_handle;
            m_handle           = {};

            handle.promise().ReleaseFrameReference(handle);
        }

        // Failure skips the parent's body, so its suspended child awaiter would
        // otherwise retain an arbitrarily deep tree until root destruction. Retire
        // the child first and resume failure publication using the same chain slot.
        template<typename ParentPromise>
        static void CompleteParentAfterRetirement(handle_type                          childHandle,
                                                  std::coroutine_handle<ParentPromise> parentHandle,
                                                  bool                                 resume = false) noexcept
        {
            auto& child  = childHandle.promise();
            auto& parent = parentHandle.promise();
            if (child.m_sequentialTaskOwner)
            {
                auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
                assert(!child.m_retirementObserver);
                parent.ChainState().m_pendingContinuation = NGIN::Execution::WorkItem([parentHandle, resume] {
                    if (resume)
                        detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                    else
                    {
                        parentHandle.promise().ReleaseFrameReference(parentHandle);
                        parentHandle.promise().MarkFinishedAndResume(parentHandle);
                    }
                });
                child.m_retirementObserver                = &parent.ContinuationSlot();
                owner->ReleaseHandle();
                return;
            }
            parent.ReleaseFrameReference(parentHandle);
            parent.MarkFinishedAndResume(parentHandle);
        }

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            handle_type                          childHandle = handle_type::from_address(self.address());
            std::coroutine_handle<ParentPromise> parentHandle =
                    std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            promise_type& child                           = childHandle.promise();
            parentHandle.promise().m_environment.executor = child.m_environment.executor;

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                return;
            }

            if (child.m_waitCancellation.IsCancellationRequested())
            {
                parentHandle.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                CompleteParentAfterRetirement(childHandle, parentHandle);
                return;
            }
            if (child.IsSucceeded())
            {
                if (child.m_sequentialTaskOwner && child.m_frameReferences.load(std::memory_order_acquire) > 1)
                {
                    // Preserve the result outside the child frame so retirement
                    // can end all remaining borrows before the parent resumes.
                    auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    try
                    {
#endif
                        owner->m_ready.emplace(std::move(*child.m_completion));
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    } catch (...)
                    {
                        auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                        fault.capturedException = std::current_exception();
#endif
                        parentHandle.promise().SetFault(std::move(fault));
                        CompleteParentAfterRetirement(childHandle, parentHandle);
                        return;
                    }
#endif
                    CompleteParentAfterRetirement(childHandle, parentHandle, true);
                    return;
                }
                detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
            CompleteParentAfterRetirement(childHandle, parentHandle);
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        template<typename ParentPromise>
        static void PropagateChildException(std::exception_ptr ex, std::coroutine_handle<> continuation) noexcept
        {
            if (!ex || !continuation)
            {
                return;
            }

            if constexpr (requires(ParentPromise& p) { p.SetChildException(ex); })
            {
                std::coroutine_handle<ParentPromise> typed =
                        std::coroutine_handle<ParentPromise>::from_address(continuation.address());
                typed.promise().SetChildException(ex);
            }
        }
#endif

        std::optional<Completion<ValueType, E>> m_ready {};
        handle_type                             m_handle {};
    };

    /// @brief Cold coroutine specialization for operations that succeed without a value.
    /// @tparam E Recoverable domain-error type.
    template<typename E>
    class Task<void, E> final : public BaseTask
    {
    public:
        using ValueType = void;
        using ErrorType = E;

        /// @brief Coroutine promise that stores a value-less task completion and continuation.
        struct promise_type final : detail::PromiseStorage<void, E>
        {
            using Base = detail::PromiseStorage<void, E>;
            using Base::Base;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;
            using Base::SetStopped;

            /// @brief Returns the task that owns this coroutine frame.
            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            /// @brief Keeps the task cold until explicitly started or awaited.
            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter final
            {
                /// @brief Always enters final suspension.
                bool await_ready() noexcept { return false; }

                /// @brief Publishes completion and releases the active-execution frame reference.
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    return h.promise().Finish(h);
                }

                /// @brief Performs no resume-time work.
                void await_resume() noexcept {}
            };

            /// @brief Returns the final awaiter that publishes task completion.
            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            /// @brief Completes the task successfully without a value.
            void return_void()
            {
                SetCompletion(Completion<void, E>::Success());
            }
        };

        /// @brief Coroutine handle type owned by this task.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty task.
        Task() noexcept = default;

        /// @brief Constructs inline successful void completion without allocating.
        [[nodiscard]] static Task FromValue()
        {
            return FromCompletion(Completion<void, E>::Success());
        }

        /// @brief Constructs an already-complete task without a coroutine frame or scheduling.
        [[nodiscard]] static Task FromCompletion(Completion<ValueType, E> completion)
        {
            Task task;
            task.m_ready.emplace(std::move(completion));
            return task;
        }

        /// @brief Takes ownership of a cold task coroutine handle.
        explicit Task(handle_type h) noexcept
            : m_handle(h)
        {
        }

        /// @brief Transfers ownership and start state from another task.
        Task(Task&& other) noexcept(std::is_nothrow_move_constructible_v<Completion<ValueType, E>>)
            : m_ready(std::move(other.m_ready)), m_handle(other.m_handle)
        {
            other.m_ready.reset();
            other.m_handle = nullptr;
        }

        /// @brief Releases this frame and transfers ownership from another task.
        Task& operator=(Task&& other) noexcept(std::is_nothrow_move_constructible_v<Completion<ValueType, E>>)
        {
            if (this != &other)
            {
                ReleaseHandle();

                m_ready.reset();
                if (other.m_ready)
                    m_ready.emplace(std::move(*other.m_ready));
                other.m_ready.reset();
                m_handle = other.m_handle;

                other.m_handle = nullptr;
            }
            return *this;
        }

        /// @brief Tasks are non-copyable because they uniquely own coroutine frames.
        Task(const Task&) = delete;
        /// @brief Tasks are non-copy-assignable because they uniquely own coroutine frames.
        Task& operator=(const Task&) = delete;

        /// @brief Releases an unstarted frame or detaches ownership from running work.
        ~Task()
        {
            ReleaseHandle();
        }

        /// @brief Returns whether execution has been started.
        [[nodiscard]] bool IsStarted() const noexcept
        {
            return m_handle && (m_handle.promise().m_executionReferenceActive.load(std::memory_order_acquire) ||
                                m_handle.promise().m_finished.load(std::memory_order_acquire));
        }

        /// @brief Returns whether the coroutine has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_ready.has_value() || (m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire));
        }

        /// @brief Returns whether the task completed in the error channel.
        [[nodiscard]] bool HasError() const noexcept
        {
            return m_ready ? m_ready->HasError() : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->HasError();
        }

        /// @brief Returns whether the task completed in the stopped channel.
        [[nodiscard]] bool IsStopped() const noexcept
        {
            return m_ready ? m_ready->IsStopped() : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsStopped();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        /// @brief Returns the exception captured from an unhandled coroutine exception, if any.
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!IsCompleted())
            {
                return {};
            }
            if (m_ready)
                return (m_ready->HasError() && m_ready->Error().IsFault()) ? m_ready->Error().Fault().capturedException : std::exception_ptr {};
            return m_handle.promise().m_exception;
        }
#endif

        /// @brief Consumes the child and returns its full outcome without propagating failure.
        class CompletionAwaiter final
        {
        public:
            explicit CompletionAwaiter(Task&& task) : m_task(std::move(task)) {}
            bool await_ready() const noexcept
            {
                return m_task.m_ready || !m_task.m_handle || m_task.IsCompleted();
            }
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                return m_task.AwaitCompletionSuspend(awaiting);
            }
            Completion<ValueType, E> await_resume()
            {
                if (m_task.m_ready)
                    return std::move(*m_task.m_ready);
                if (m_task.m_handle)
                    return m_task.m_handle.promise().TakeCompletion();
                return Completion<ValueType, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
            }

        private:
            Task m_task;
        };

        /// @brief Consumes this task; the awaiting body receives value, error, or cancellation.
        [[nodiscard]] CompletionAwaiter AsCompletion() { return CompletionAwaiter {std::move(*this)}; }

        /// @brief Owns the consumed child and propagates failures to its parent.
        class OwnedPropagationAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited task.
            explicit OwnedPropagationAwaiter(Task&& task)
                : m_task(std::move(task))
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return (m_task.m_ready && m_task.m_ready->Succeeded()) || (m_task.m_handle &&
                                                                           m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                                                                           m_task.m_handle.promise().IsSucceeded());
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Verifies that the child completed successfully.
            void await_resume() noexcept
            {
                if (m_task.m_ready)
                {
                    assert(m_task.m_ready->Succeeded());
                    return;
                }
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            Task m_task;
        };

        /// @brief Owns the consumed child and observes a task context's cancellation token.
        class CancellablePropagationAwaiter final
        {
        public:
            /// @brief Consumes the task and borrows the cancellation context.
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx)
                : m_task(std::move(task)), m_ctx(&ctx)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return (m_task.m_ready && m_task.m_ready->Succeeded()) || (!m_ctx->IsCancellationRequested() && m_task.m_handle &&
                                                                           m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                                                                           m_task.m_handle.promise().IsSucceeded());
            }

            /// @brief Starts the child or cancels the compatible parent when the context is canceled.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting)
            {
                if (m_task.m_ready || !m_task.m_handle)
                    return m_task.template AwaitSuspend<ParentPromise>(awaiting);

                if (m_ctx == nullptr || (!m_task.IsStarted() && m_ctx->IsCancellationRequested()))
                {
                    awaiting.promise().SetStopped(m_ctx ? m_ctx->GetCancellationToken().GetStopReason() : StopReason::Requested);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (m_task.m_handle)
                    m_task.m_handle.promise().m_waitCancellation = m_ctx->GetCancellationToken();

                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Verifies that the child completed successfully.
            void await_resume() noexcept
            {
                if (m_task.m_ready)
                {
                    assert(m_task.m_ready->Succeeded());
                    return;
                }
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task         m_task;
            TaskContext* m_ctx {};
        };

        /// @brief Consumes an lvalue task; subsequent awaits report InvalidTaskUsage.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() &
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Creates an owning failure-propagating awaiter for an rvalue task.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() &&
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Consumes this child and observes cancellation while joining its terminal result.
        /// @details Canceling the wait does not end backend access or detach the child.
        /// The child's own context controls cancellation of its operations.
        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx)
        {
            return CancellablePropagationAwaiter {*this, ctx};
        }

        /// @brief Creates a value-less task that completes after a cancellation-aware delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        static Task<void, E> Delay(TaskContext& ctx, const TUnit& duration)
        {
            co_await ctx.Delay(duration);
            co_return;
        }

    private:
        friend struct detail::OperationAccess;
        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);
        template<typename, typename>
        friend class detail::ProducerOperation;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            handle_type handle = m_handle;
            m_handle           = {};
            if (handle)
            {
                handle.promise().AcquireExecutionReference();
            }
            return handle;
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitSuspend(std::coroutine_handle<ParentPromise> awaiting)
        {
            static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                          "Await propagation requires identical Task error types.");

            if (m_ready)
            {
                if (m_ready->Succeeded())
                    return awaiting;
                if (m_ready->IsStopped())
                    awaiting.promise().SetStopped(m_ready->StopReason());
                else
                    awaiting.promise().SetError(std::move(*m_ready).Error());
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!m_handle)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            promise_type& child = m_handle.promise();
            if (awaiting.promise().m_finished.load(std::memory_order_acquire))
            {
                return std::noop_coroutine();
            }
            if (child.m_finished.load(std::memory_order_acquire))
            {
                if (child.m_waitCancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }
                if (child.IsSucceeded())
                {
                    return awaiting;
                }

                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!awaiting.promise().HasContinuation())
            {
                awaiting.promise().SetFault(detail::MakeSchedulingFault(
                        AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }
            awaiting.promise().RetainFrameReference();
            const detail::PromiseRuntimeCommon::ContinuationInstallResult installResult = child.TryInstallContinuation(
                    awaiting,
                    &Task::template PropagateChildCompletion<ParentPromise>
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    ,
                    &Task::template PropagateChildException<ParentPromise>
#endif
                    ,
                    &awaiting.promise());
            if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyCompleted)
            {
                if (child.m_waitCancellation.IsCancellationRequested())
                {
                    awaiting.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                    awaiting.promise().ReleaseFrameReference(awaiting);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }
                if (child.IsSucceeded())
                {
                    awaiting.promise().ReleaseFrameReference(awaiting);
                    return awaiting;
                }
                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().ReleaseFrameReference(awaiting);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }
            if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                awaiting.promise().ReleaseFrameReference(awaiting);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (!IsStarted())
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                child.m_sequentialTaskOwner = this;
                if (child.m_environment.executor == awaiting.promise().m_environment.executor)
                    return child.StartSequential(m_handle, awaiting.promise());
                (void) child.StartExecution(m_handle);
            }

            return std::noop_coroutine();
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitCompletionSuspend(std::coroutine_handle<ParentPromise> awaiting)
        {
            auto& parent = awaiting.promise();
            if (parent.m_finished.load(std::memory_order_acquire))
                return std::noop_coroutine();
            if (!parent.HasContinuation())
            {
                m_ready.emplace(Completion<ValueType, E>::Faulted(detail::MakeSchedulingFault(
                        AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected)));
                return awaiting;
            }
            auto& child = m_handle.promise();
            parent.RetainFrameReference();
            const auto installed = child.TryInstallContinuation(awaiting,
                                                                &Task::template ObserveChildCompletion<ParentPromise>
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                                                                ,
                                                                nullptr
#endif
                                                                ,
                                                                &parent);
            if (installed != detail::PromiseRuntimeCommon::ContinuationInstallResult::Installed)
            {
                parent.ReleaseFrameReference(awaiting);
                if (installed == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
                    m_ready.emplace(Completion<ValueType, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)));
                return awaiting;
            }
            if (!IsStarted())
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(child, parent))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }
                child.m_sequentialTaskOwner = this;
                if (child.m_environment.executor == parent.m_environment.executor)
                    return child.StartSequential(m_handle, parent);
                (void) child.StartExecution(m_handle);
            }
            return std::noop_coroutine();
        }

        template<typename ParentPromise>
        static void ObserveChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            const auto childHandle                        = handle_type::from_address(self.address());
            const auto parentHandle                       = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto&      child                              = childHandle.promise();
            parentHandle.promise().m_environment.executor = child.m_environment.executor;
            if (child.m_sequentialTaskOwner && child.m_frameReferences.load(std::memory_order_acquire) > 1)
            {
                auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
#if NGIN_ASYNC_HAS_EXCEPTIONS
                try
                {
#endif
                    owner->m_ready.emplace(std::move(*child.m_completion));
#if NGIN_ASYNC_HAS_EXCEPTIONS
                } catch (...)
                {
                    auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    fault.capturedException = std::current_exception();
#endif
                    owner->m_ready.emplace(Completion<ValueType, E>::Faulted(std::move(fault)));
                }
#endif
                CompleteParentAfterRetirement(childHandle, parentHandle, true);
                return;
            }
            detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
        }

        void ReleaseHandle() noexcept
        {
            if (!m_handle)
            {
                return;
            }

            handle_type handle = m_handle;
            m_handle           = {};

            handle.promise().ReleaseFrameReference(handle);
        }

        // Failure skips the parent's body, so its suspended child awaiter would
        // otherwise retain an arbitrarily deep tree until root destruction. Retire
        // the child first and resume failure publication using the same chain slot.
        template<typename ParentPromise>
        static void CompleteParentAfterRetirement(handle_type                          childHandle,
                                                  std::coroutine_handle<ParentPromise> parentHandle,
                                                  bool                                 resume = false) noexcept
        {
            auto& child  = childHandle.promise();
            auto& parent = parentHandle.promise();
            if (child.m_sequentialTaskOwner)
            {
                auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
                assert(!child.m_retirementObserver);
                parent.ChainState().m_pendingContinuation = NGIN::Execution::WorkItem([parentHandle, resume] {
                    if (resume)
                        detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                    else
                    {
                        parentHandle.promise().ReleaseFrameReference(parentHandle);
                        parentHandle.promise().MarkFinishedAndResume(parentHandle);
                    }
                });
                child.m_retirementObserver                = &parent.ContinuationSlot();
                owner->ReleaseHandle();
                return;
            }
            parent.ReleaseFrameReference(parentHandle);
            parent.MarkFinishedAndResume(parentHandle);
        }

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            handle_type                          childHandle = handle_type::from_address(self.address());
            std::coroutine_handle<ParentPromise> parentHandle =
                    std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            promise_type& child                           = childHandle.promise();
            parentHandle.promise().m_environment.executor = child.m_environment.executor;

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                return;
            }

            if (child.m_waitCancellation.IsCancellationRequested())
            {
                parentHandle.promise().SetStopped(child.m_waitCancellation.GetStopReason());
                CompleteParentAfterRetirement(childHandle, parentHandle);
                return;
            }
            if (child.IsSucceeded())
            {
                if (child.m_sequentialTaskOwner && child.m_frameReferences.load(std::memory_order_acquire) > 1)
                {
                    // Preserve the result outside the child frame so retirement
                    // can end all remaining borrows before the parent resumes.
                    auto* owner = static_cast<Task*>(child.m_sequentialTaskOwner);
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    try
                    {
#endif
                        owner->m_ready.emplace(std::move(*child.m_completion));
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    } catch (...)
                    {
                        auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                        fault.capturedException = std::current_exception();
#endif
                        parentHandle.promise().SetFault(std::move(fault));
                        CompleteParentAfterRetirement(childHandle, parentHandle);
                        return;
                    }
#endif
                    CompleteParentAfterRetirement(childHandle, parentHandle, true);
                    return;
                }
                detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
            CompleteParentAfterRetirement(childHandle, parentHandle);
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        template<typename ParentPromise>
        static void PropagateChildException(std::exception_ptr ex, std::coroutine_handle<> continuation) noexcept
        {
            if (!ex || !continuation)
            {
                return;
            }

            if constexpr (requires(ParentPromise& p) { p.SetChildException(ex); })
            {
                std::coroutine_handle<ParentPromise> typed =
                        std::coroutine_handle<ParentPromise>::from_address(continuation.address());
                typed.promise().SetChildException(ex);
            }
        }
#endif

        std::optional<Completion<ValueType, E>> m_ready {};
        handle_type                             m_handle {};
    };

    namespace detail
    {
        /// Extracted before dispatch so reentrant consumers may release their owner.
        struct OwnedOperationContinuation final
        {
            PromiseRuntimeCommon*     target {};
            NGIN::Execution::WorkItem resume;

            void Dispatch() noexcept
            {
                if (!target)
                    return;
                const auto executor = target->ChainState().m_executor;
                if (!executor.TryDispatchInline(resume))
                    target->QueueContinuation(std::move(resume));
            }
        };

        /// A scope owns execution independently of this singly consumed result.
        /// Publication follows final producer frame retirement. Errors remain immutable
        /// so the consumer and the scope report can retain the same move-only payload.
        template<typename T, typename E>
        class OwnedOperationState final : public std::enable_shared_from_this<OwnedOperationState<T, E>>
        {
            using Result = NGIN::Async::Completion<T, E>;

        public:
            void Store(Result result)
            {
                assert(!m_result);
                m_result.emplace(std::move(result));
            }

            void StoreFault(AsyncFault fault) noexcept
            {
                m_result.reset();
                m_result.emplace(Result::Faulted(std::move(fault)));
            }

            Completion<void, E> Report()
            {
                assert(m_result);
                if (m_result->HasError())
                    return Completion<void, E>::Failure(RetainError());
                if (m_result->IsStopped())
                    return Completion<void, E>::Stopped(m_result->StopReason());
                return Completion<void, E>::Success();
            }

            OwnedOperationContinuation Publish() noexcept
            {
                std::lock_guard lock(m_mutex);
                assert(m_result && !m_published);
                m_published = true;
                m_observation.Publish(m_result->Kind());
                return std::move(m_continuation);
            }

            CompletionObservation& Observation() noexcept { return m_observation; }

            bool IsCompleted() const noexcept
            {
                std::lock_guard lock(m_mutex);
                return m_published;
            }

            bool HasError() const noexcept
            {
                std::lock_guard lock(m_mutex);
                return m_published && m_result->HasError();
            }

            bool IsStopped() const noexcept
            {
                std::lock_guard lock(m_mutex);
                return m_published && m_result->IsStopped();
            }

            std::optional<Result> TryTakeResult()
            {
                std::lock_guard lock(m_mutex);
                if (!m_published || m_taken)
                    return {};
                m_taken = true;
                if (m_result->HasError())
                    return Result::Failure(RetainError());
                return std::move(*m_result);
            }

            template<typename Promise>
            bool Install(std::coroutine_handle<Promise> awaiting, std::optional<AsyncFault>& fault) noexcept
            {
                std::lock_guard lock(m_mutex);
                if (m_published)
                    return false;
                if (m_continuation.target)
                {
                    fault = MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage);
                    return false;
                }
                if constexpr (std::derived_from<Promise, PromiseRuntimeCommon>)
                {
                    if (awaiting.promise().HasContinuation())
                    {
                        awaiting.promise().RetainFrameReference();
                        m_continuation.target = &awaiting.promise();
                        m_continuation.resume = NGIN::Execution::WorkItem([awaiting] {
                            PromiseRuntimeCommon::ResumeRetained(awaiting);
                        });
                        return true;
                    }
                }
                fault = MakeAsyncFault(AsyncFaultCode::InvalidContinuationState);
                return false;
            }

        private:
            AsyncError<E> RetainError()
            {
                return AsyncError<E>::Retain(std::shared_ptr<const AsyncError<E>>(
                        this->shared_from_this(), &m_result->Error()));
            }

            mutable std::mutex         m_mutex;
            std::optional<Result>      m_result;
            bool                       m_published {};
            bool                       m_taken {};
            OwnedOperationContinuation m_continuation;
            CompletionObservation      m_observation;
        };
    }// namespace detail

    /// @brief Move-only consumer of a spawned task completion. Scoped execution remains owned by its scope.
    /// @tparam T Successful result type.
    /// @tparam E Recoverable domain-error type.
    template<typename T, typename E>
    class Operation final
    {
    public:
        using ValueType  = T;
        using ErrorType  = E;
        using Completion = NGIN::Async::Completion<T, E>;

        /// @brief Constructs an invalid operation with no running task.
        Operation() noexcept = default;

        Operation(Operation&&) noexcept            = default;
        Operation& operator=(Operation&&) noexcept = default;

        /// @brief Operations are non-copyable because results are singly consumed.
        Operation(const Operation&) = delete;
        /// @brief Operations are non-copy-assignable because results are singly consumed.
        Operation& operator=(const Operation&) = delete;

        /// @brief Releases only result interest; the scope retains execution and diagnostics.
        ~Operation() = default;

        /// @brief Returns whether this object holds a result or a running operation.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_owned);
        }

        /// @brief Retains notification interest in a scoped operation without consuming it.
        /// @details An invalid operation returns an invalid reference.
        [[nodiscard]] OperationRef Observe() const noexcept
        {
            return m_owned ? OperationRef(std::shared_ptr<detail::CompletionObservation>(m_owned, &m_owned->Observation()))
                           : OperationRef {};
        }

        /// @brief Returns whether the operation has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_owned && m_owned->IsCompleted();
        }

        /// @brief Returns whether the operation completed in the error channel.
        [[nodiscard]] bool HasError() const noexcept
        {
            return m_owned && m_owned->HasError();
        }

        /// @brief Returns whether the operation completed in the stopped channel.
        [[nodiscard]] bool IsStopped() const noexcept
        {
            return m_owned && m_owned->IsStopped();
        }

        /// @brief Consumes and returns the completion when available and not previously taken.
        /// @return The completion, or an empty optional while unfinished or after consumption.
        [[nodiscard]] std::optional<Completion> TryTakeResult()
        {
            return m_owned ? m_owned->TryTakeResult() : std::nullopt;
        }

        /// @brief Consumes the completion or returns an invalid-usage fault.
        [[nodiscard]] Completion TakeResult()
        {
            if (std::optional<Completion> result = TryTakeResult())
            {
                return std::move(*result);
            }

            return Completion::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
        }

        /// @brief Non-owning awaiter that returns the operation's complete outcome.
        class Awaiter final
        {
        public:
            /// @brief Constructs an awaiter borrowing an operation.
            explicit Awaiter(Operation& operation) noexcept
                : m_operation(operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.IsValid() || m_operation.IsCompleted();
            }

            /// @brief Registers the awaiting coroutine as the operation continuation.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (m_operation.m_owned)
                    return m_operation.m_owned->Install(awaiting, m_fault)
                                   ? std::coroutine_handle<>(std::noop_coroutine())
                                   : std::coroutine_handle<>(awaiting);
                return awaiting;
            }

            /// @brief Consumes and returns the operation's completion.
            [[nodiscard]] Completion await_resume()
            {
                if (m_fault)
                    return Completion::Faulted(std::move(*m_fault));
                return m_operation.TakeResult();
            }

        private:
            Operation&                m_operation;
            std::optional<AsyncFault> m_fault;
        };

        /// @brief Owning awaiter for an rvalue operation.
        class OwnedAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited operation.
            explicit OwnedAwaiter(Operation&& operation)
                : m_operation(std::move(operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Transfers an awaiter before suspension and rebinds its borrowed observer.
            OwnedAwaiter(OwnedAwaiter&& other) noexcept(std::is_nothrow_move_constructible_v<Operation>)
                : m_operation(std::move(other.m_operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.IsValid() || m_operation.IsCompleted();
            }

            /// @brief Registers the awaiting coroutine as the operation continuation.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_awaiter.await_suspend(awaiting);
            }

            /// @brief Consumes and returns the operation's completion.
            [[nodiscard]] Completion await_resume()
            {
                return m_awaiter.await_resume();
            }

        private:
            Operation m_operation;
            Awaiter   m_awaiter;
        };

        /// @brief Creates a non-owning awaiter for an lvalue operation.
        [[nodiscard]] Awaiter operator co_await() & noexcept
        {
            return Awaiter {*this};
        }

        /// @brief Creates an owning awaiter for an rvalue operation.
        [[nodiscard]] OwnedAwaiter operator co_await() &&
        {
            return OwnedAwaiter {std::move(*this)};
        }

    private:
        friend struct detail::OperationAccess;
        explicit Operation(std::shared_ptr<detail::OwnedOperationState<ValueType, E>> state) noexcept
            : m_owned(std::move(state)) {}


        std::shared_ptr<detail::OwnedOperationState<ValueType, E>> m_owned;
    };

    namespace detail
    {
        /// Internal frame owner, retained by a scope or standalone runtime root through retirement.
        template<typename T, typename E>
        class ProducerOperation final
        {
        public:
            using ValueType   = T;
            using ErrorType   = E;
            using Completion  = NGIN::Async::Completion<T, E>;
            using handle_type = typename Task<T, E>::handle_type;

            /// @brief Constructs an invalid operation with no running task.
            ProducerOperation() noexcept = default;

            /// @brief Owns an inline terminal result without a coroutine or scheduling.
            explicit ProducerOperation(Completion completion)
                : m_ready(std::move(completion))
            {
            }

            /// @brief Takes ownership of an already-started task coroutine.
            ProducerOperation(handle_type handle, NGIN::Execution::ExecutorRef executor) noexcept
                : m_handle(handle), m_executor(executor)
            {
            }

            /// @brief Transfers ownership and result-consumption state from another operation.
            ProducerOperation(ProducerOperation&& other) noexcept(std::is_nothrow_move_constructible_v<Completion>)
                : m_ready(std::move(other.m_ready)), m_handle(other.m_handle), m_executor(other.m_executor), m_resultTaken(other.m_resultTaken)
            {
                other.m_ready.reset();
                other.m_handle      = {};
                other.m_executor    = {};
                other.m_resultTaken = false;
            }

            /// @brief Releases this operation and transfers ownership from another operation.
            ProducerOperation& operator=(ProducerOperation&& other) noexcept(std::is_nothrow_move_constructible_v<Completion>)
            {
                if (this != &other)
                {
                    ReleaseHandle();
                    m_ready.reset();
                    if (other.m_ready)
                        m_ready.emplace(std::move(*other.m_ready));
                    other.m_ready.reset();
                    m_handle      = other.m_handle;
                    m_executor    = other.m_executor;
                    m_resultTaken = other.m_resultTaken;

                    other.m_handle      = {};
                    other.m_executor    = {};
                    other.m_resultTaken = false;
                }
                return *this;
            }

            /// @brief ProducerOperations are non-copyable because results are singly consumed.
            ProducerOperation(const ProducerOperation&) = delete;
            /// @brief ProducerOperations are non-copy-assignable because results are singly consumed.
            ProducerOperation& operator=(const ProducerOperation&) = delete;

            /// @brief Releases the producer frame reference after its owning boundary finishes.
            ~ProducerOperation()
            {
                ReleaseHandle();
            }

            /// @brief Returns whether this object holds a result or a running operation.
            [[nodiscard]] bool IsValid() const noexcept
            {
                return m_ready.has_value() || static_cast<bool>(m_handle);
            }

            /// @brief Returns whether the operation has published a terminal completion.
            [[nodiscard]] bool IsCompleted() const noexcept
            {
                return m_ready.has_value() || (m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire));
            }

            /// @brief Returns whether the operation completed in the error channel.
            [[nodiscard]] bool HasError() const noexcept
            {
                return m_ready ? m_ready->HasError()
                               : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->HasError();
            }

            /// @brief Returns whether the operation completed in the stopped channel.
            [[nodiscard]] bool IsStopped() const noexcept
            {
                return m_ready ? m_ready->IsStopped()
                               : IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsStopped();
            }

            /// @brief Consumes and returns the completion when available and not previously taken.
            /// @return The completion, or an empty optional while unfinished or after consumption.
            [[nodiscard]] std::optional<Completion> TryTakeResult()
            {
                if (!IsCompleted() || m_resultTaken)
                {
                    return {};
                }

                m_resultTaken = true;
                if (m_ready)
                    return std::move(*m_ready);
                return m_handle.promise().TakeCompletion();
            }

            /// @brief Consumes the completion or returns an invalid-usage fault.
            [[nodiscard]] Completion TakeResult()
            {
                if (std::optional<Completion> result = TryTakeResult())
                {
                    return std::move(*result);
                }

                return Completion::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
            }

            /// @brief Non-owning awaiter that returns the operation's complete outcome.
            class Awaiter final
            {
            public:
                /// @brief Constructs an awaiter borrowing an operation.
                explicit Awaiter(ProducerOperation& operation) noexcept
                    : m_operation(operation)
                {
                }

                /// @brief Returns whether the operation is absent or already complete.
                [[nodiscard]] bool await_ready() const noexcept
                {
                    return !m_operation.IsValid() || m_operation.IsCompleted();
                }

                /// @brief Registers the awaiting coroutine as the operation continuation.
                template<typename ParentPromise>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
                {
                    if (!m_operation.m_handle)
                    {
                        return awaiting;
                    }

                    typename Task<T, E>::promise_type& child = m_operation.m_handle.promise();
                    if (child.m_finished.load(std::memory_order_acquire))
                    {
                        return awaiting;
                    }

                    detail::PromiseRuntimeCommon*          target = nullptr;
                    NGIN::Execution::CompletionReservation delivery;
                    if constexpr (std::derived_from<ParentPromise, detail::PromiseRuntimeCommon>)
                        target = &awaiting.promise();
                    if (!target || !target->HasContinuation())
                    {
                        target        = nullptr;
                        auto executor = child.m_environment.executor;
                        if constexpr (requires { awaiting.promise().m_environment.executor; })
                            executor = awaiting.promise().m_environment.executor;
                        const auto childHandle = m_operation.m_handle;
                        auto       reserved    = executor.ReserveCompletion(NGIN::Execution::WorkItem([childHandle] {
                            detail::PromiseRuntimeCommon::DeliverChild<decltype(childHandle)>(childHandle);
                        }));
                        if (!reserved)
                        {
                            m_fault = detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, reserved.error());
                            return awaiting;
                        }
                        delivery = std::move(*reserved);
                    }
                    constexpr bool retainsParent = requires(ParentPromise& promise, std::coroutine_handle<ParentPromise> handle) {
                        promise.RetainFrameReference();
                        promise.ReleaseFrameReference(handle);
                    };
                    if constexpr (retainsParent)
                    {
                        awaiting.promise().RetainFrameReference();
                    }
                    detail::PromiseRuntimeCommon::CompletionHandler completionHandler = nullptr;
                    if constexpr (retainsParent)
                    {
                        completionHandler = &ProducerOperation::template ResumeRetainedContinuation<ParentPromise>;
                    }
                    const detail::PromiseRuntimeCommon::ContinuationInstallResult installResult = child.TryInstallContinuation(
                            awaiting,
                            completionHandler
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                            ,
                            nullptr
#endif
                            ,
                            target, &delivery);
                    if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyCompleted)
                    {
                        if constexpr (retainsParent)
                        {
                            awaiting.promise().ReleaseFrameReference(awaiting);
                        }
                        return awaiting;
                    }
                    if (installResult == detail::PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
                    {
                        if constexpr (requires { awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)); })
                        {
                            awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                            if constexpr (std::derived_from<ParentPromise, detail::PromiseRuntimeCommon>)
                            {
                                awaiting.promise().ReleaseFrameReference(awaiting);
                                awaiting.promise().MarkFinishedAndResume(awaiting);
                            }
                            else
                            {
                                awaiting.promise().MarkFinishedAndResume(awaiting);
                                if constexpr (retainsParent)
                                    awaiting.promise().ReleaseFrameReference(awaiting);
                            }
                            return std::noop_coroutine();
                        }
                        if constexpr (retainsParent)
                        {
                            awaiting.promise().ReleaseFrameReference(awaiting);
                        }
                        return awaiting;
                    }

                    return std::noop_coroutine();
                }

                /// @brief Consumes and returns the operation's completion.
                [[nodiscard]] Completion await_resume()
                {
                    if (m_fault)
                        return Completion::Faulted(std::move(*m_fault));
                    return m_operation.TakeResult();
                }

            private:
                ProducerOperation&        m_operation;
                std::optional<AsyncFault> m_fault;
            };

            /// @brief Owning awaiter for an rvalue operation.
            class OwnedAwaiter final
            {
            public:
                /// @brief Takes ownership of the awaited operation.
                explicit OwnedAwaiter(ProducerOperation&& operation)
                    : m_operation(std::move(operation)), m_awaiter(m_operation)
                {
                }

                /// @brief Transfers an awaiter before suspension and rebinds its borrowed observer.
                OwnedAwaiter(OwnedAwaiter&& other) noexcept(std::is_nothrow_move_constructible_v<ProducerOperation>)
                    : m_operation(std::move(other.m_operation)), m_awaiter(m_operation)
                {
                }

                /// @brief Returns whether the operation is absent or already complete.
                [[nodiscard]] bool await_ready() const noexcept
                {
                    return !m_operation.IsValid() || m_operation.IsCompleted();
                }

                /// @brief Registers the awaiting coroutine as the operation continuation.
                template<typename ParentPromise>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
                {
                    return m_awaiter.await_suspend(awaiting);
                }

                /// @brief Consumes and returns the operation's completion.
                [[nodiscard]] Completion await_resume()
                {
                    return m_awaiter.await_resume();
                }

            private:
                ProducerOperation m_operation;
                Awaiter           m_awaiter;
            };

            /// @brief Creates a non-owning awaiter for an lvalue operation.
            [[nodiscard]] Awaiter operator co_await() & noexcept
            {
                return Awaiter {*this};
            }

            /// @brief Creates an owning awaiter for an rvalue operation.
            [[nodiscard]] OwnedAwaiter operator co_await() &&
            {
                return OwnedAwaiter {std::move(*this)};
            }

        private:
            friend struct detail::OperationAccess;
            template<typename ParentPromise>
            static void ResumeRetainedContinuation(std::coroutine_handle<>,
                                                   std::coroutine_handle<> continuation) noexcept
            {
                std::coroutine_handle<ParentPromise> parentHandle =
                        std::coroutine_handle<ParentPromise>::from_address(continuation.address());
                if constexpr (std::derived_from<ParentPromise, detail::PromiseRuntimeCommon>)
                    detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                else
                {
                    continuation.resume();
                    parentHandle.promise().ReleaseFrameReference(parentHandle);
                }
            }

            void ReleaseHandle() noexcept
            {
                if (!m_handle)
                {
                    return;
                }

                handle_type handle = m_handle;
                m_handle           = {};

                handle.promise().ReleaseFrameReference(handle);
            }

            std::optional<Completion>    m_ready {};
            handle_type                  m_handle {};
            NGIN::Execution::ExecutorRef m_executor {};
            bool                         m_resultTaken {false};
        };

    }// namespace detail

    namespace detail
    {
        /// Internal terminal observation consumes the Operation's single await slot.
        /// The caller owns the Operation through the queued notification callback.
        /// These notifications deliberately enqueue: reusable tickets serialize terminal
        /// observation and frame retirement, including releases on foreign threads.
        /// The owning boundary may inline its final user continuation after joining.
        struct OperationAccess final
        {
            /// Starts a cold input using an optional pre-admitted execution slot.
            /// Scoped admission uses reserved delivery directly, with no second queue admission.
            template<typename T, typename E>
            static ProducerOperation<T, E> Start(TaskContext& ctx, Task<T, E>&& task,
                                                 TaskExecutionStateOwner execution = {}) noexcept
            {
                if (task.m_ready)
                {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    try
                    {
#endif
                        auto completion = std::move(*task.m_ready);
                        task.m_ready.reset();
                        return ProducerOperation<T, E> {std::move(completion)};
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    } catch (...)
                    {
                        task.m_ready.reset();
                        auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                        fault.capturedException = std::current_exception();
#endif
                        return ProducerOperation<T, E> {Completion<T, E>::Faulted(std::move(fault))};
                    }
#endif
                }
                typename Task<T, E>::handle_type handle = task.ReleaseForOperation();
                ProducerOperation<T, E>          operation {handle, ctx.GetExecutor()};
                if (!handle)
                {
                    return operation;
                }

                typename Task<T, E>::promise_type& promise = handle.promise();
                promise.m_ctx                              = &ctx;
                promise.m_environment                      = ctx.GetEnvironment();
                if (!promise.m_environment.executor.IsValid())
                {
                    promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    promise.MarkFinishedAndResume(handle);
                    return operation;
                }

                if (execution)
                {
                    assert(execution->m_taskContinuation.IsValid());
                    promise.m_execution = std::move(execution);
                    promise.QueueContinuation(NGIN::Execution::WorkItem(handle));
                }
                else
                    (void) promise.StartExecution(handle);
                return operation;
            }

            template<typename T, typename E>
            static Operation<T, E> ResultHandle(std::shared_ptr<OwnedOperationState<T, E>> state) noexcept
            {
                return Operation<T, E>(std::move(state));
            }

            /// Borrows a stable reusable ticket until terminal observation. The
            /// same ticket can then report frame retirement without new storage.
            template<typename T, typename E>
            [[nodiscard]] static bool ObserveReusable(ProducerOperation<T, E>&                operation,
                                                      NGIN::Execution::CompletionReservation& notification) noexcept
            {
                if (!notification.IsValid())
                    return false;
                if (!operation.m_handle)
                {
                    notification.Schedule();
                    return true;
                }
                auto&      promise   = operation.m_handle.promise();
                const auto installed = promise.TryInstallContinuation({}, nullptr
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                                                                      ,
                                                                      nullptr
#endif
                                                                      ,
                                                                      nullptr, nullptr, &notification);
                if (installed == PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
                    return false;
                if (installed == PromiseRuntimeCommon::ContinuationInstallResult::AlreadyCompleted)
                    notification.Schedule();
                return true;
            }

            /// Releases a completed operation and schedules notification only
            /// after its last frame lease and all suspended locals are gone.
            /// The borrowed ticket must remain valid until that notification.
            template<typename T, typename E>
            static void Retire(ProducerOperation<T, E>&                operation,
                               NGIN::Execution::CompletionReservation& notification) noexcept
            {
                assert(notification.IsValid());
                if (!operation.m_handle)
                {
                    operation.m_ready.reset();
                    notification.Schedule();
                    return;
                }
                assert(operation.IsCompleted());
                auto& promise = operation.m_handle.promise();
                assert(!promise.m_retirementObserver);
                promise.m_retirementObserver = &notification;
                operation.ReleaseHandle();
            }

            template<typename T, typename E>
            struct RetirementAwaiter final
            {
                ProducerOperation<T, E>& operation;
                bool                     await_ready() const noexcept
                {
                    if (operation.m_handle)
                        return false;
                    operation.m_ready.reset();
                    return true;
                }
                template<typename Promise>
                    requires std::derived_from<Promise, PromiseRuntimeCommon>
                bool await_suspend(std::coroutine_handle<Promise> awaiting)
                {
                    auto& parent = awaiting.promise();
                    if (!parent.HasContinuation())
                        throw std::logic_error("Joining frame retirement requires a tracked parent task");
                    parent.RetainFrameReference();
                    parent.ChainState().m_pendingContinuation = NGIN::Execution::WorkItem([awaiting] {
                        PromiseRuntimeCommon::ResumeRetained(awaiting);
                    });
                    OperationAccess::Retire(operation, parent.ContinuationSlot());
                    return true;
                }
                void await_resume() const noexcept {}
            };

            template<typename T, typename E>
            [[nodiscard]] static RetirementAwaiter<T, E> JoinRetirement(ProducerOperation<T, E>& operation) noexcept
            {
                return {operation};
            }

            template<typename T, typename E>
            [[nodiscard]] static bool Observe(ProducerOperation<T, E>&               operation,
                                              NGIN::Execution::CompletionReservation notification) noexcept
            {
                if (!notification.IsValid())
                    return false;
                if (!operation.m_handle)
                {
                    notification.Dispatch();
                    return true;
                }
                auto&      promise   = operation.m_handle.promise();
                const auto installed = promise.TryInstallContinuation({}, nullptr
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                                                                      ,
                                                                      nullptr
#endif
                                                                      ,
                                                                      nullptr, &notification);
                if (installed == PromiseRuntimeCommon::ContinuationInstallResult::AlreadyInstalled)
                    return false;
                if (installed == PromiseRuntimeCommon::ContinuationInstallResult::AlreadyCompleted)
                    notification.Dispatch();
                return true;
            }
        };
    }// namespace detail

    /// @brief Starts a cold task, blocks until completion, and consumes its complete outcome.
    /// @warning The caller must avoid blocking an executor thread required by the task.
    template<typename T, typename E>
    [[nodiscard]] Completion<T, E> SyncWait(TaskContext& ctx, Task<T, E>&& task);

}// namespace NGIN::Async

#include <NGIN/Async/TaskScope.hpp>
