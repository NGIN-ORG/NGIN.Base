/// @file Task.hpp
/// @brief Cold Task coroutines and their running Operation handles.
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncConfig.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Completion.hpp>
#include <NGIN/Async/NoError.hpp>
#include <NGIN/Async/TaskContext.hpp>
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

    template<typename E>
    class Operation<void, E>;

    namespace detail
    {
        struct OperationAccess;
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

            enum class ContinuationState : std::uint8_t
            {
                Empty,
                Installing,
                Installed,
                Completed,
            };

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

            std::atomic<bool>                       m_finished {false};
            NGIN::Sync::AtomicCondition             m_finishedCondition {};
            std::coroutine_handle<>                 m_continuation {};
            CompletionHandler                       m_completionHandler {};
            std::atomic<ContinuationState>          m_continuationState {ContinuationState::Empty};
            TaskContext*                            m_ctx {nullptr};
            CancellationToken                       m_waitCancellation {};
            NGIN::Execution::ExecutorRef            m_executor {};
            std::atomic<std::uint32_t>              m_frameReferences {1};
            std::atomic<bool>                       m_executionReferenceActive {false};
            NGIN::Execution::CompletionReservation  m_taskContinuation {};
            NGIN::Execution::CompletionReservation  m_foreignContinuation {};
            PromiseRuntimeCommon*                   m_continuationTarget {};
            NGIN::Execution::WorkItem               m_pendingContinuation {};
            NGIN::Execution::CompletionReservation* m_reusableObserver {};
            NGIN::Execution::CompletionReservation* m_retirementObserver {};
            std::atomic<unsigned>                   m_submissionFlags {0};

            /// Queues one continuation using the task's pre-admitted storage.
            void QueueContinuation(NGIN::Execution::WorkItem work) noexcept
            {
                assert(m_taskContinuation.IsValid());
                m_pendingContinuation = std::move(work);
                m_taskContinuation.Schedule();
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
                if (!m_executor.SupportsCompletionReservations())
                    return {};
                auto ticket = m_executor.ReserveCompletion(NGIN::Execution::WorkItem([this] {
                    auto work = std::move(m_pendingContinuation);
                    work.Invoke();
                }));
                if (!ticket)
                    return std::unexpected(ticket.error());
                m_taskContinuation = std::move(*ticket);
                return {};
            }

            /// Submits one token-free step using the active task's reserved completion.
            /// Execution owns the frame until submission and dispatch/discard have both
            /// finished. Neither side accesses the frame after publishing its last event.
            template<typename Handle>
            [[nodiscard]] NGIN::Execution::ScheduleResult SubmitTracked(
                    Handle self, bool timed = false, NGIN::Time::TimePoint until = {}) noexcept
            {
                assert(m_taskContinuation.IsValid());
                assert(m_executionReferenceActive.load(std::memory_order_acquire));
                m_submissionFlags.store(0, std::memory_order_relaxed);
                NGIN::Execution::WorkItem work {SubmissionWork<Handle>(self)};
                const auto                result = timed ? m_executor.ExecuteAt(std::move(work), until) : m_executor.Execute(std::move(work));
                if (!result)
                {
                    // Rejection destroys the submitted item before returning. No
                    // dispatch can finish before admission arms the handshake.
                    CompleteSubmissionFailure(self, result.error());
                    return result;
                }
                const unsigned observed = m_submissionFlags.fetch_or(SubmissionArmed, std::memory_order_acq_rel);
                if (observed & (SubmissionReady | SubmissionDiscarded))
                    QueueSubmissionResult(self, (observed & SubmissionReady) != 0);
                return result;
            }

            /// Admits and starts a task. Untracked executors retain standalone support.
            template<typename Handle>
            [[nodiscard]] NGIN::Execution::ScheduleResult StartExecution(Handle self) noexcept
            {
                auto result = ReserveExecution();
                if (result && m_taskContinuation.IsValid())
                    return SubmitTracked(self);
                if (result)
                    result = m_executor.Execute(self);
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
                : m_ctx(&ctx), m_executor(ctx.GetExecutor())
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
                ContinuationState expected = ContinuationState::Empty;
                if (!m_continuationState.compare_exchange_strong(
                            expected,
                            ContinuationState::Installing,
                            std::memory_order_acquire,
                            std::memory_order_acquire))
                {
                    return expected == ContinuationState::Completed
                                   ? ContinuationInstallResult::AlreadyCompleted
                                   : ContinuationInstallResult::AlreadyInstalled;
                }

                m_continuation       = continuation;
                m_completionHandler  = completionHandler;
                m_continuationTarget = target;
                m_reusableObserver   = reusableObserver;
                if (delivery)
                    m_foreignContinuation = std::move(*delivery);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_setChildException = exceptionPropagator;
#endif
                m_continuationState.store(ContinuationState::Installed, std::memory_order_release);
                return ContinuationInstallResult::Installed;
            }

            /// @brief Claims the installed continuation while publishing terminal completion.
            [[nodiscard]] std::coroutine_handle<> CompleteContinuation() noexcept
            {
                ContinuationState state = m_continuationState.load(std::memory_order_acquire);
                for (;;)
                {
                    if (state == ContinuationState::Installing)
                    {
                        state = m_continuationState.load(std::memory_order_acquire);
                        continue;
                    }
                    if (state == ContinuationState::Completed)
                        return {};
                    if (m_continuationState.compare_exchange_weak(
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
                return m_continuationState.load(std::memory_order_acquire) == ContinuationState::Installed;
            }

            template<typename Handle>
            bool MarkFinishedAndResume(Handle self) noexcept
            {
                bool expected = false;
                if (!m_finished.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return false;
                }

                m_finishedCondition.NotifyAll();

                const std::coroutine_handle<> continuation = CompleteContinuation();

                PromiseRuntimeCommon* target           = m_continuationTarget;
                auto                  delivery         = std::move(m_foreignContinuation);
                auto*                 reusableObserver = m_reusableObserver;
                if (continuation)
                    RetainFrameReference();
                // Release execution before publishing the observer. Observers can
                // then destroy a completed Operation and its frame during joining.
                auto execution = std::move(m_taskContinuation);
                ReleaseExecutionReference(self);
                if (continuation && target)
                    target->QueueContinuation(NGIN::Execution::WorkItem([self] { DeliverChild<Handle>(self); }));
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
                const unsigned observed = m_submissionFlags.fetch_or(event, std::memory_order_acq_rel);
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

            void SetCanceled()
            {
                SetCompletion(Completion<T, E>::Canceled());
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

                if (child.m_completion->IsDomainError())
                {
                    SetDomainError(std::move(*child.m_completion).DomainError());
                    return;
                }

                if (child.m_completion->IsCanceled())
                {
                    SetCanceled();
                    return;
                }

                SetFault(std::move(*child.m_completion).Fault());
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

            void SetCanceled()
            {
                SetCompletion(Completion<void, E>::Canceled());
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

                if (child.m_completion->IsDomainError())
                {
                    SetDomainError(std::move(*child.m_completion).DomainError());
                    return;
                }

                if (child.m_completion->IsCanceled())
                {
                    SetCanceled();
                    return;
                }

                SetFault(std::move(*child.m_completion).Fault());
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
        bool InheritChildExecutionContext(NGIN::Execution::ExecutorRef& taskExecutor,
                                          ChildPromise&                 child,
                                          ParentPromise&                parent) noexcept
        {
            if (!taskExecutor.IsValid())
            {
                taskExecutor = child.m_executor;
            }

            if constexpr (requires { parent.m_executor; })
            {
                if (!taskExecutor.IsValid())
                {
                    taskExecutor = parent.m_executor;
                }
            }

            if constexpr (requires { parent.m_ctx; })
            {
                if (!taskExecutor.IsValid() && parent.m_ctx != nullptr)
                {
                    taskExecutor = parent.m_ctx->GetExecutor();
                }
            }

            if (!taskExecutor.IsValid())
            {
                return false;
            }

            child.m_executor = taskExecutor;
            if constexpr (requires { parent.m_ctx; })
            {
                if (child.m_ctx == nullptr)
                {
                    child.m_ctx = parent.m_ctx;
                }
            }

            return true;
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
            using Base::SetCanceled;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;

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
                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    static_cast<void>(h.promise().MarkFinishedAndResume(h));
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

        /// @brief Takes ownership of a cold task coroutine handle.
        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        /// @brief Transfers ownership and start state from another task.
        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
        }

        /// @brief Releases this frame and transfers ownership from another task.
        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                ReleaseHandle();

                m_handle   = other.m_handle;
                m_executor = other.m_executor;
                m_started.store(other.m_started.load(std::memory_order_acquire), std::memory_order_release);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
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
            return m_started.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the coroutine has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the task completed with an unexpected fault.
        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        /// @brief Returns whether the task completed through cancellation.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        /// @brief Returns the exception captured from an unhandled coroutine exception, if any.
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!IsCompleted())
            {
                return {};
            }
            return m_handle.promise().m_exception;
        }
#endif

        /// @brief Non-owning awaiter that propagates failures into the awaiting task.
        class PropagationAwaiter final
        {
        public:
            /// @brief Constructs an awaiter borrowing a task.
            explicit PropagationAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Moves the successful child value into the parent coroutine.
            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                promise_type& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            Task& m_task;
        };

        /// @brief Owning awaiter for an rvalue task that propagates failures to its parent.
        class OwnedPropagationAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited task.
            explicit OwnedPropagationAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Moves the successful child value into the parent coroutine.
            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                promise_type& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            Task m_task;
        };

        /// @brief Non-owning awaiter that also observes a task context's cancellation token.
        class CancellablePropagationAwaiter final
        {
        public:
            /// @brief Constructs a cancellation-aware awaiter borrowing a task and context.
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task), m_ctx(&ctx)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_ctx->IsCancellationRequested() && m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts the child or cancels the compatible parent when the context is canceled.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (m_ctx == nullptr || (!m_task.IsStarted() && m_ctx->IsCancellationRequested()))
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (m_task.m_handle)
                    m_task.m_handle.promise().m_waitCancellation = m_ctx->GetCancellationToken();

                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Moves the successful child value into the parent coroutine.
            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                promise_type& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&        m_task;
            TaskContext* m_ctx {};
        };

        /// @brief Creates a non-owning failure-propagating awaiter for an lvalue task.
        [[nodiscard]] PropagationAwaiter operator co_await() & noexcept
        {
            return PropagationAwaiter {*this};
        }

        /// @brief Creates an owning failure-propagating awaiter for an rvalue task.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() && noexcept
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Observes cancellation while joining this child to its terminal result.
        /// @details Canceling the wait does not end backend access or detach the child.
        /// The child's own context controls cancellation of its operations.
        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx) noexcept
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
        template<typename, typename>
        friend class Operation;

        /// @brief Declares the task-to-operation conversion helper as a friend.
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Declares the detached-task launch helper as a friend.
        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            handle_type handle = m_handle;
            m_handle           = {};
            m_started.store(true, std::memory_order_release);
            if (handle)
            {
                handle.promise().AcquireExecutionReference();
            }
            return handle;
        }

        bool StartWithExecutor(TaskContext& ctx) noexcept
        {
            if (!m_handle)
            {
                return false;
            }

            bool expected = false;
            if (!m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            m_executor            = ctx.GetExecutor();
            promise_type& promise = m_handle.promise();
            promise.AcquireExecutionReference();
            promise.m_ctx      = &ctx;
            promise.m_executor = m_executor;

            if (!m_executor.IsValid())
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                promise.MarkFinishedAndResume(m_handle);
                return false;
            }

            return static_cast<bool>(promise.StartExecution(m_handle));
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitSuspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
        {
            static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                          "Await propagation requires identical Task error types.");

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
                    awaiting.promise().SetCanceled();
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

            if (!awaiting.promise().m_taskContinuation.IsValid())
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
                    awaiting.promise().SetCanceled();
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

            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(m_executor, child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                (void) child.StartExecution(m_handle);
            }

            return std::noop_coroutine();
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

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            handle_type                          childHandle = handle_type::from_address(self.address());
            std::coroutine_handle<ParentPromise> parentHandle =
                    std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            promise_type& child = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                return;
            }

            if (child.m_waitCancellation.IsCancellationRequested())
            {
                parentHandle.promise().SetCanceled();
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                parentHandle.promise().MarkFinishedAndResume(parentHandle);
                return;
            }
            if (child.IsSucceeded())
            {
                detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
            parentHandle.promise().ReleaseFrameReference(parentHandle);
            parentHandle.promise().MarkFinishedAndResume(parentHandle);
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

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        std::atomic_bool             m_started {false};
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
            using Base::SetCanceled;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;

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
                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    static_cast<void>(h.promise().MarkFinishedAndResume(h));
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

        /// @brief Takes ownership of a cold task coroutine handle.
        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        /// @brief Transfers ownership and start state from another task.
        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
        }

        /// @brief Releases this frame and transfers ownership from another task.
        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                ReleaseHandle();

                m_handle   = other.m_handle;
                m_executor = other.m_executor;
                m_started.store(other.m_started.load(std::memory_order_acquire), std::memory_order_release);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
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
            return m_started.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the coroutine has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the task completed with an unexpected fault.
        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        /// @brief Returns whether the task completed through cancellation.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        /// @brief Returns the exception captured from an unhandled coroutine exception, if any.
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!IsCompleted())
            {
                return {};
            }
            return m_handle.promise().m_exception;
        }
#endif

        /// @brief Non-owning awaiter that propagates failures into the awaiting task.
        class PropagationAwaiter final
        {
        public:
            /// @brief Constructs an awaiter borrowing a task.
            explicit PropagationAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Verifies that the child completed successfully.
            void await_resume() noexcept
            {
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            Task& m_task;
        };

        /// @brief Owning awaiter for an rvalue task that propagates failures to its parent.
        class OwnedPropagationAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited task.
            explicit OwnedPropagationAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts or connects the child task to a compatible parent promise.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            /// @brief Verifies that the child completed successfully.
            void await_resume() noexcept
            {
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            Task m_task;
        };

        /// @brief Non-owning awaiter that also observes a task context's cancellation token.
        class CancellablePropagationAwaiter final
        {
        public:
            /// @brief Constructs a cancellation-aware awaiter borrowing a task and context.
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task), m_ctx(&ctx)
            {
            }

            /// @brief Returns whether the task already completed successfully.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_ctx->IsCancellationRequested() && m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            /// @brief Starts the child or cancels the compatible parent when the context is canceled.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (m_ctx == nullptr || (!m_task.IsStarted() && m_ctx->IsCancellationRequested()))
                {
                    awaiting.promise().SetCanceled();
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
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&        m_task;
            TaskContext* m_ctx {};
        };

        /// @brief Creates a non-owning failure-propagating awaiter for an lvalue task.
        [[nodiscard]] PropagationAwaiter operator co_await() & noexcept
        {
            return PropagationAwaiter {*this};
        }

        /// @brief Creates an owning failure-propagating awaiter for an rvalue task.
        [[nodiscard]] OwnedPropagationAwaiter operator co_await() && noexcept
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        /// @brief Observes cancellation while joining this child to its terminal result.
        /// @details Canceling the wait does not end backend access or detach the child.
        /// The child's own context controls cancellation of its operations.
        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx) noexcept
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
        template<typename, typename>
        friend class Operation;

        /// @brief Grants the task-starting helper access to the owned coroutine frame.
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Grants the detached-start helper access to the owned coroutine frame.
        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            handle_type handle = m_handle;
            m_handle           = {};
            m_started.store(true, std::memory_order_release);
            if (handle)
            {
                handle.promise().AcquireExecutionReference();
            }
            return handle;
        }

        template<typename ParentPromise>
        std::coroutine_handle<> AwaitSuspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
        {
            static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                          "Await propagation requires identical Task error types.");

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
                    awaiting.promise().SetCanceled();
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

            if (!awaiting.promise().m_taskContinuation.IsValid())
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
                    awaiting.promise().SetCanceled();
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

            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                child.AcquireExecutionReference();
                if (!detail::InheritChildExecutionContext(m_executor, child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                (void) child.StartExecution(m_handle);
            }

            return std::noop_coroutine();
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

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            handle_type                          childHandle = handle_type::from_address(self.address());
            std::coroutine_handle<ParentPromise> parentHandle =
                    std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            promise_type& child = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                return;
            }

            if (child.m_waitCancellation.IsCancellationRequested())
            {
                parentHandle.promise().SetCanceled();
                parentHandle.promise().ReleaseFrameReference(parentHandle);
                parentHandle.promise().MarkFinishedAndResume(parentHandle);
                return;
            }
            if (child.IsSucceeded())
            {
                detail::PromiseRuntimeCommon::ResumeRetained(parentHandle);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
            parentHandle.promise().ReleaseFrameReference(parentHandle);
            parentHandle.promise().MarkFinishedAndResume(parentHandle);
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

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        std::atomic_bool             m_started {false};
    };

    /// @brief Running owner of a spawned Task and its eventual completion.
    /// @tparam T Successful result type.
    /// @tparam E Recoverable domain-error type.
    template<typename T, typename E>
    class Operation final
    {
    public:
        using ValueType   = T;
        using ErrorType   = E;
        using Completion  = NGIN::Async::Completion<T, E>;
        using handle_type = typename Task<T, E>::handle_type;

        /// @brief Constructs an invalid operation with no running task.
        Operation() noexcept = default;

        /// @brief Takes ownership of an already-started task coroutine.
        Operation(handle_type handle, NGIN::Execution::ExecutorRef executor) noexcept
            : m_handle(handle), m_executor(executor)
        {
        }

        /// @brief Transfers ownership and result-consumption state from another operation.
        Operation(Operation&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_resultTaken(other.m_resultTaken)
        {
            other.m_handle      = {};
            other.m_executor    = {};
            other.m_resultTaken = false;
        }

        /// @brief Releases this operation and transfers ownership from another operation.
        Operation& operator=(Operation&& other) noexcept
        {
            if (this != &other)
            {
                ReleaseHandle();
                m_handle      = other.m_handle;
                m_executor    = other.m_executor;
                m_resultTaken = other.m_resultTaken;

                other.m_handle      = {};
                other.m_executor    = {};
                other.m_resultTaken = false;
            }
            return *this;
        }

        /// @brief Operations are non-copyable because they uniquely own coroutine frames.
        Operation(const Operation&) = delete;
        /// @brief Operations are non-copy-assignable because they uniquely own coroutine frames.
        Operation& operator=(const Operation&) = delete;

        /// @brief Detaches unfinished work or destroys a completed coroutine frame.
        ~Operation()
        {
            ReleaseHandle();
        }

        /// @brief Returns whether this object owns a coroutine frame.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_handle);
        }

        /// @brief Returns whether the operation has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the operation completed with an unexpected fault.
        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        /// @brief Returns whether the operation completed through cancellation.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
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
            explicit Awaiter(Operation& operation) noexcept
                : m_operation(operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
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
                if (!target || !target->m_taskContinuation.IsValid())
                {
                    target        = nullptr;
                    auto executor = child.m_executor;
                    if constexpr (requires { awaiting.promise().m_executor; })
                        executor = awaiting.promise().m_executor;
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
                    completionHandler = &Operation::template ResumeRetainedContinuation<ParentPromise>;
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
            Operation&                m_operation;
            std::optional<AsyncFault> m_fault;
        };

        /// @brief Owning awaiter for an rvalue operation.
        class OwnedAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited operation.
            explicit OwnedAwaiter(Operation&& operation) noexcept
                : m_operation(std::move(operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Transfers an awaiter before suspension and rebinds its borrowed observer.
            OwnedAwaiter(OwnedAwaiter&& other) noexcept
                : m_operation(std::move(other.m_operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
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
        [[nodiscard]] OwnedAwaiter operator co_await() && noexcept
        {
            return OwnedAwaiter {std::move(*this)};
        }

    private:
        friend struct detail::OperationAccess;

        /// @brief Grants the task-starting helper access to operation ownership state.
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Grants the detached-start helper access to operation ownership state.
        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Grants synchronous waiting access to the operation's completion signal.
        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);

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

        void WaitUntilComplete()
        {
            if (!m_handle)
            {
                return;
            }

            typename Task<T, E>::promise_type& promise = m_handle.promise();
            while (!promise.m_finished.load(std::memory_order_acquire))
            {
                const UInt32 generation = promise.m_finishedCondition.Load();
                if (promise.m_finished.load(std::memory_order_acquire))
                {
                    break;
                }
                promise.m_finishedCondition.Wait(generation);
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

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        bool                         m_resultTaken {false};
    };

    /// @brief Running owner of a spawned value-less Task and its eventual completion.
    /// @tparam E Recoverable domain-error type.
    template<typename E>
    class Operation<void, E> final
    {
    public:
        using ValueType   = void;
        using ErrorType   = E;
        using Completion  = NGIN::Async::Completion<void, E>;
        using handle_type = typename Task<void, E>::handle_type;

        /// @brief Constructs an invalid operation with no running task.
        Operation() noexcept = default;

        /// @brief Takes ownership of an already-started task coroutine.
        Operation(handle_type handle, NGIN::Execution::ExecutorRef executor) noexcept
            : m_handle(handle), m_executor(executor)
        {
        }

        /// @brief Transfers ownership and result-consumption state from another operation.
        Operation(Operation&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_resultTaken(other.m_resultTaken)
        {
            other.m_handle      = {};
            other.m_executor    = {};
            other.m_resultTaken = false;
        }

        /// @brief Releases this operation and transfers ownership from another operation.
        Operation& operator=(Operation&& other) noexcept
        {
            if (this != &other)
            {
                ReleaseHandle();
                m_handle      = other.m_handle;
                m_executor    = other.m_executor;
                m_resultTaken = other.m_resultTaken;

                other.m_handle      = {};
                other.m_executor    = {};
                other.m_resultTaken = false;
            }
            return *this;
        }

        /// @brief Operations are non-copyable because they uniquely own coroutine frames.
        Operation(const Operation&) = delete;
        /// @brief Operations are non-copy-assignable because they uniquely own coroutine frames.
        Operation& operator=(const Operation&) = delete;

        /// @brief Detaches unfinished work or destroys a completed coroutine frame.
        ~Operation()
        {
            ReleaseHandle();
        }

        /// @brief Returns whether this object owns a coroutine frame.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_handle);
        }

        /// @brief Returns whether the operation has published a terminal completion.
        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        /// @brief Returns whether the operation completed with an unexpected fault.
        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        /// @brief Returns whether the operation completed through cancellation.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return IsCompleted() && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
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
            explicit Awaiter(Operation& operation) noexcept
                : m_operation(operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            /// @brief Registers the awaiting coroutine as the operation continuation.
            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_operation.m_handle)
                {
                    return awaiting;
                }

                typename Task<void, E>::promise_type& child = m_operation.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                detail::PromiseRuntimeCommon*          target = nullptr;
                NGIN::Execution::CompletionReservation delivery;
                if constexpr (std::derived_from<ParentPromise, detail::PromiseRuntimeCommon>)
                    target = &awaiting.promise();
                if (!target || !target->m_taskContinuation.IsValid())
                {
                    target        = nullptr;
                    auto executor = child.m_executor;
                    if constexpr (requires { awaiting.promise().m_executor; })
                        executor = awaiting.promise().m_executor;
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
                    completionHandler = &Operation::template ResumeRetainedContinuation<ParentPromise>;
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
            Operation&                m_operation;
            std::optional<AsyncFault> m_fault;
        };

        /// @brief Owning awaiter for an rvalue operation.
        class OwnedAwaiter final
        {
        public:
            /// @brief Takes ownership of the awaited operation.
            explicit OwnedAwaiter(Operation&& operation) noexcept
                : m_operation(std::move(operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Transfers an awaiter before suspension and rebinds its borrowed observer.
            OwnedAwaiter(OwnedAwaiter&& other) noexcept
                : m_operation(std::move(other.m_operation)), m_awaiter(m_operation)
            {
            }

            /// @brief Returns whether the operation is absent or already complete.
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
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
        [[nodiscard]] OwnedAwaiter operator co_await() && noexcept
        {
            return OwnedAwaiter {std::move(*this)};
        }

    private:
        friend struct detail::OperationAccess;

        /// @brief Grants the task-starting helper access to operation ownership state.
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Grants the detached-start helper access to operation ownership state.
        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        /// @brief Grants synchronous waiting access to the operation's completion signal.
        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);

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

        void WaitUntilComplete()
        {
            if (!m_handle)
            {
                return;
            }

            typename Task<void, E>::promise_type& promise = m_handle.promise();
            while (!promise.m_finished.load(std::memory_order_acquire))
            {
                const UInt32 generation = promise.m_finishedCondition.Load();
                if (promise.m_finished.load(std::memory_order_acquire))
                {
                    break;
                }
                promise.m_finishedCondition.Wait(generation);
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

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        bool                         m_resultTaken {false};
    };

    namespace detail
    {
        /// Internal terminal observation consumes the Operation's single await slot.
        /// The caller owns the Operation through the queued notification callback.
        struct OperationAccess final
        {
            /// Borrows a stable reusable ticket until terminal observation. The
            /// same ticket can then report frame retirement without new storage.
            template<typename T, typename E>
            [[nodiscard]] static bool ObserveReusable(Operation<T, E>&                        operation,
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
            static void Retire(Operation<T, E>&                        operation,
                               NGIN::Execution::CompletionReservation& notification) noexcept
            {
                assert(notification.IsValid());
                if (!operation.m_handle)
                {
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
                Operation<T, E>& operation;
                bool             await_ready() const noexcept { return !operation.m_handle; }
                template<typename Promise>
                    requires std::derived_from<Promise, PromiseRuntimeCommon>
                bool await_suspend(std::coroutine_handle<Promise> awaiting)
                {
                    auto& parent = awaiting.promise();
                    if (!parent.m_taskContinuation.IsValid())
                        throw std::logic_error("Joining frame retirement requires a tracked parent task");
                    parent.RetainFrameReference();
                    parent.m_pendingContinuation = NGIN::Execution::WorkItem([awaiting] {
                        PromiseRuntimeCommon::ResumeRetained(awaiting);
                    });
                    OperationAccess::Retire(operation, parent.m_taskContinuation);
                    return true;
                }
                void await_resume() const noexcept {}
            };

            template<typename T, typename E>
            [[nodiscard]] static RetirementAwaiter<T, E> JoinRetirement(Operation<T, E>& operation) noexcept
            {
                return {operation};
            }

            template<typename T, typename E>
            [[nodiscard]] static bool Observe(Operation<T, E>&                       operation,
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

    /// @brief Starts a cold task on the context executor and returns its running owner.
    /// @return An invalid operation when the task is empty; otherwise the started operation.
    template<typename T, typename E>
    [[nodiscard]] Operation<T, E> Spawn(TaskContext& ctx, Task<T, E>&& task) noexcept
    {
        typename Task<T, E>::handle_type handle = task.ReleaseForOperation();
        Operation<T, E>                  operation {handle, ctx.GetExecutor()};
        if (!handle)
        {
            return operation;
        }

        typename Task<T, E>::promise_type& promise = handle.promise();
        promise.m_ctx                              = &ctx;
        promise.m_executor                         = ctx.GetExecutor();
        if (!promise.m_executor.IsValid())
        {
            promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
            promise.MarkFinishedAndResume(handle);
            return operation;
        }

        (void) promise.StartExecution(handle);
        return operation;
    }

    /// @brief Starts a cold task and relinquishes ownership of its eventual completion.
    /// @note The detached coroutine destroys its own frame after completion.
    template<typename T, typename E>
    void Detach(TaskContext& ctx, Task<T, E>&& task) noexcept
    {
        Operation<T, E> operation = Spawn(ctx, std::move(task));
        operation.ReleaseHandle();
    }

    /// @brief Starts a cold task, blocks until completion, and consumes its complete outcome.
    /// @warning The caller must avoid blocking an executor thread required by the task.
    template<typename T, typename E>
    [[nodiscard]] Completion<T, E> SyncWait(TaskContext& ctx, Task<T, E>&& task)
    {
        Operation<T, E> operation = Spawn(ctx, std::move(task));
        operation.WaitUntilComplete();
        return operation.TakeResult();
    }
}// namespace NGIN::Async
