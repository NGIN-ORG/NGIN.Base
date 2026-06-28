/// <summary>
/// Core coroutine types: cold Task<T, E>, running Operation<T, E>, and TaskContext integration.
/// </summary>
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Completion.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Async
{
    class BaseTask
    {
    };

#if NGIN_ASYNC_HAS_EXCEPTIONS
    class AsyncCanceledException : public std::exception
    {
    public:
        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task canceled";
        }
    };

    class AsyncFaultException : public std::exception
    {
    public:
        explicit AsyncFaultException(AsyncFault fault) noexcept
            : m_fault(std::move(fault))
        {
        }

        [[nodiscard]] const AsyncFault& Fault() const noexcept
        {
            return m_fault;
        }

        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task fault";
        }

    private:
        AsyncFault m_fault;
    };

    template<typename E>
    class AsyncDomainErrorException : public std::exception
    {
    public:
        explicit AsyncDomainErrorException(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
            : m_error(std::move(error))
        {
        }

        [[nodiscard]] const E& Error() const noexcept
        {
            return m_error;
        }

        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task domain error";
        }

    private:
        E m_error;
    };

    template<typename E>
    struct AsyncExceptionTraits
    {
        [[noreturn]] static void Throw(const E& error)
        {
            throw AsyncDomainErrorException<E>(error);
        }
    };

    template<typename E>
    using AsyncExceptionAdapter = AsyncExceptionTraits<E>;
#endif

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
        inline void ResumeOnExecutor(NGIN::Execution::ExecutorRef exec, std::coroutine_handle<> handle) noexcept
        {
            if (!handle)
            {
                return;
            }

            if (exec.IsValid())
            {
                exec.Execute(handle);
            }
            else
            {
                handle.resume();
            }
        }

        struct PromiseRuntimeCommon
        {
            using CompletionHandler = void (*)(std::coroutine_handle<>, std::coroutine_handle<>) noexcept;

            std::atomic<bool>            m_finished {false};
            NGIN::Sync::AtomicCondition  m_finishedCondition {};
            std::coroutine_handle<>      m_continuation {};
            CompletionHandler            m_completionHandler {};
            TaskContext*                 m_ctx {nullptr};
            NGIN::Execution::ExecutorRef m_executor {};
            std::atomic<bool>            m_detached {false};

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

            template<typename Handle>
            void MarkFinishedAndResume(Handle self) noexcept
            {
                bool expected = false;
                if (!m_finished.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return;
                }

                m_finishedCondition.NotifyAll();

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                if (m_continuation && m_setChildException && m_exception)
                {
                    m_setChildException(m_exception, m_continuation);
                }
#endif

                if (!m_continuation)
                {
                    return;
                }

                if (m_completionHandler)
                {
                    m_completionHandler(std::coroutine_handle<>::from_address(self.address()), m_continuation);
                }
                else
                {
                    ResumeOnExecutor(m_executor, m_continuation);
                }
            }

            [[nodiscard]] bool ShouldDestroyDetachedWithoutContinuation() const noexcept
            {
                return m_detached.load(std::memory_order_acquire) && !m_continuation;
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

    template<typename T, typename E>
    class Task final : public BaseTask
    {
    public:
        using ValueType = T;
        using ErrorType = E;

        struct promise_type final : detail::PromiseStorage<T, E>
        {
            using Base = detail::PromiseStorage<T, E>;
            using Base::Base;
            using Base::SetCanceled;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;

            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter final
            {
                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    const bool destroyOnCompletion = h.promise().ShouldDestroyDetachedWithoutContinuation();
                    h.promise().MarkFinishedAndResume(h);
                    if (destroyOnCompletion)
                    {
                        h.destroy();
                    }
                }

                void await_resume() noexcept {}
            };

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_value(T value)
            {
                SetCompletion(Completion<T, E>::Success(std::move(value)));
            }

            void return_value(Completion<T, E> completion)
            {
                SetCompletion(std::move(completion));
            }

            void return_value(NGIN::Utilities::Expected<T, E> result)
            {
                if (!result)
                {
                    SetDomainError(std::move(result).TakeError());
                    return;
                }

                SetCompletion(Completion<T, E>::Success(std::move(result).TakeValue()));
            }

            void return_value(NGIN::Utilities::Unexpected<E> error)
            {
                SetDomainError(error.Error());
            }

            void return_value(E error)
                requires(!std::is_same_v<T, E>)
            {
                SetDomainError(std::move(error));
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
        }

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

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            ReleaseHandle();
        }

        [[nodiscard]] bool IsStarted() const noexcept
        {
            return m_started.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!m_handle)
            {
                return {};
            }
            return m_handle.promise().m_exception;
        }
#endif

        class PropagationAwaiter final
        {
        public:
            explicit PropagationAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                auto& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            Task& m_task;
        };

        class OwnedPropagationAwaiter final
        {
        public:
            explicit OwnedPropagationAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                auto& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            Task m_task;
        };

        class CancellablePropagationAwaiter final
        {
        public:
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task), m_ctx(&ctx)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (m_ctx == nullptr || m_ctx->IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                m_awaiting = awaiting;
                m_ctx->GetCancellationToken().Register(
                        m_cancellationRegistration,
                        {},
                        {},
                        &Task::template CancelAwaitingContinuation<ParentPromise, CancellablePropagationAwaiter>,
                        this);

                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            [[nodiscard]] T await_resume() noexcept
            {
                assert(m_task.m_handle);
                auto& promise = m_task.m_handle.promise();
                assert(promise.IsSucceeded());
                return std::move(promise.m_completion->Value());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&                    m_task;
            TaskContext*             m_ctx {};
            CancellationRegistration m_cancellationRegistration {};
            std::coroutine_handle<>  m_awaiting {};
        };

        [[nodiscard]] PropagationAwaiter operator co_await() & noexcept
        {
            return PropagationAwaiter {*this};
        }

        [[nodiscard]] OwnedPropagationAwaiter operator co_await() && noexcept
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx) noexcept
        {
            return CancellablePropagationAwaiter {*this, ctx};
        }

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

        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            auto handle = m_handle;
            m_handle    = {};
            m_started.store(true, std::memory_order_release);
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

            m_executor         = ctx.GetExecutor();
            auto& promise      = m_handle.promise();
            promise.m_ctx      = &ctx;
            promise.m_executor = m_executor;

            if (!m_executor.IsValid())
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                promise.MarkFinishedAndResume(m_handle);
                return false;
            }

            m_executor.Execute(m_handle);
            return true;
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

            auto& child = m_handle.promise();
            if (child.m_finished.load(std::memory_order_acquire))
            {
                if (child.IsSucceeded())
                {
                    return awaiting;
                }

                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (child.m_continuation)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            child.m_continuation      = awaiting;
            child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

            if (!IsStarted())
            {
                if (!detail::InheritChildExecutionContext(m_executor, child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                bool expected = false;
                if (m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    m_executor.Execute(m_handle);
                }
            }

            return std::noop_coroutine();
        }

        void ReleaseHandle() noexcept
        {
            if (!m_handle)
            {
                return;
            }

            auto handle = m_handle;
            m_handle    = {};

            auto&      promise  = handle.promise();
            const bool started  = m_started.load(std::memory_order_acquire);
            const bool finished = promise.m_finished.load(std::memory_order_acquire);
            if (started && !finished)
            {
                promise.m_detached.store(true, std::memory_order_release);
                return;
            }

            handle.destroy();
        }

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            auto  childHandle  = handle_type::from_address(self.address());
            auto  parentHandle = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto& child        = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return;
            }

            if (child.IsSucceeded())
            {
                detail::ResumeOnExecutor(child.m_executor, continuation);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
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
                auto typed = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
                typed.promise().SetChildException(ex);
            }
        }
#endif

        template<typename ParentPromise, typename TAwaiter>
        static bool CancelAwaitingContinuation(void* rawAwaiter) noexcept
        {
            auto* awaiter = static_cast<TAwaiter*>(rawAwaiter);
            if (awaiter == nullptr || !awaiter->m_awaiting)
            {
                return false;
            }

            auto parentHandle = std::coroutine_handle<ParentPromise>::from_address(awaiter->m_awaiting.address());
            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return false;
            }

            parentHandle.promise().SetCanceled();
            parentHandle.promise().MarkFinishedAndResume(parentHandle);
            return false;
        }

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        std::atomic_bool             m_started {false};
    };

    template<typename E>
    class Task<void, E> final : public BaseTask
    {
    public:
        using ValueType = void;
        using ErrorType = E;

        struct promise_type final : detail::PromiseStorage<void, E>
        {
            using Base = detail::PromiseStorage<void, E>;
            using Base::Base;
            using Base::SetCanceled;
            using Base::SetCompletion;
            using Base::SetDomainError;
            using Base::SetFault;

            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter final
            {
                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    const bool destroyOnCompletion = h.promise().ShouldDestroyDetachedWithoutContinuation();
                    h.promise().MarkFinishedAndResume(h);
                    if (destroyOnCompletion)
                    {
                        h.destroy();
                    }
                }

                void await_resume() noexcept {}
            };

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_void()
            {
                SetCompletion(Completion<void, E>::Success());
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
        }

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

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            ReleaseHandle();
        }

        [[nodiscard]] bool IsStarted() const noexcept
        {
            return m_started.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        [[nodiscard]] std::exception_ptr GetException() const noexcept
        {
            if (!m_handle)
            {
                return {};
            }
            return m_handle.promise().m_exception;
        }
#endif

        class PropagationAwaiter final
        {
        public:
            explicit PropagationAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            void await_resume() noexcept
            {
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            Task& m_task;
        };

        class OwnedPropagationAwaiter final
        {
        public:
            explicit OwnedPropagationAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            void await_resume() noexcept
            {
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            Task m_task;
        };

        class CancellablePropagationAwaiter final
        {
        public:
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task), m_ctx(&ctx)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().IsSucceeded();
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (m_ctx == nullptr || m_ctx->IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                m_awaiting = awaiting;
                m_ctx->GetCancellationToken().Register(
                        m_cancellationRegistration,
                        {},
                        {},
                        &Task::template CancelAwaitingContinuation<ParentPromise, CancellablePropagationAwaiter>,
                        this);

                return m_task.template AwaitSuspend<ParentPromise>(awaiting);
            }

            void await_resume() noexcept
            {
                assert(m_task.m_handle);
                assert(m_task.m_handle.promise().IsSucceeded());
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&                    m_task;
            TaskContext*             m_ctx {};
            CancellationRegistration m_cancellationRegistration {};
            std::coroutine_handle<>  m_awaiting {};
        };

        [[nodiscard]] PropagationAwaiter operator co_await() & noexcept
        {
            return PropagationAwaiter {*this};
        }

        [[nodiscard]] OwnedPropagationAwaiter operator co_await() && noexcept
        {
            return OwnedPropagationAwaiter {std::move(*this)};
        }

        [[nodiscard]] CancellablePropagationAwaiter WithCancellation(TaskContext& ctx) noexcept
        {
            return CancellablePropagationAwaiter {*this, ctx};
        }

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

        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        [[nodiscard]] handle_type ReleaseForOperation() noexcept
        {
            auto handle = m_handle;
            m_handle    = {};
            m_started.store(true, std::memory_order_release);
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

            auto& child = m_handle.promise();
            if (child.m_finished.load(std::memory_order_acquire))
            {
                if (child.IsSucceeded())
                {
                    return awaiting;
                }

                awaiting.promise().PropagateFromChild(child);
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            if (child.m_continuation)
            {
                awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                awaiting.promise().MarkFinishedAndResume(awaiting);
                return std::noop_coroutine();
            }

            child.m_continuation      = awaiting;
            child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

            if (!IsStarted())
            {
                if (!detail::InheritChildExecutionContext(m_executor, child, awaiting.promise()))
                {
                    child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    child.MarkFinishedAndResume(m_handle);
                    return std::noop_coroutine();
                }

                bool expected = false;
                if (m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    m_executor.Execute(m_handle);
                }
            }

            return std::noop_coroutine();
        }

        void ReleaseHandle() noexcept
        {
            if (!m_handle)
            {
                return;
            }

            auto handle = m_handle;
            m_handle    = {};

            auto&      promise  = handle.promise();
            const bool started  = m_started.load(std::memory_order_acquire);
            const bool finished = promise.m_finished.load(std::memory_order_acquire);
            if (started && !finished)
            {
                promise.m_detached.store(true, std::memory_order_release);
                return;
            }

            handle.destroy();
        }

        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            auto  childHandle  = handle_type::from_address(self.address());
            auto  parentHandle = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto& child        = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return;
            }

            if (child.IsSucceeded())
            {
                detail::ResumeOnExecutor(child.m_executor, continuation);
                return;
            }

            parentHandle.promise().PropagateFromChild(child);
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
                auto typed = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
                typed.promise().SetChildException(ex);
            }
        }
#endif

        template<typename ParentPromise, typename TAwaiter>
        static bool CancelAwaitingContinuation(void* rawAwaiter) noexcept
        {
            auto* awaiter = static_cast<TAwaiter*>(rawAwaiter);
            if (awaiter == nullptr || !awaiter->m_awaiting)
            {
                return false;
            }

            auto parentHandle = std::coroutine_handle<ParentPromise>::from_address(awaiter->m_awaiting.address());
            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return false;
            }

            parentHandle.promise().SetCanceled();
            parentHandle.promise().MarkFinishedAndResume(parentHandle);
            return false;
        }

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        std::atomic_bool             m_started {false};
    };

    template<typename T, typename E>
    class Operation final
    {
    public:
        using ValueType   = T;
        using ErrorType   = E;
        using Completion  = NGIN::Async::Completion<T, E>;
        using handle_type = typename Task<T, E>::handle_type;

        Operation() noexcept = default;

        Operation(handle_type handle, NGIN::Execution::ExecutorRef executor) noexcept
            : m_handle(handle), m_executor(executor)
        {
        }

        Operation(Operation&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_resultTaken(other.m_resultTaken)
        {
            other.m_handle      = {};
            other.m_executor    = {};
            other.m_resultTaken = false;
        }

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

        Operation(const Operation&)            = delete;
        Operation& operator=(const Operation&) = delete;

        ~Operation()
        {
            ReleaseHandle();
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_handle);
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

        [[nodiscard]] std::optional<Completion> TryTakeResult()
        {
            if (!IsCompleted() || m_resultTaken)
            {
                return {};
            }

            m_resultTaken = true;
            return m_handle.promise().TakeCompletion();
        }

        [[nodiscard]] Completion TakeResult()
        {
            if (auto result = TryTakeResult())
            {
                return std::move(*result);
            }

            return Completion::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
        }

        class Awaiter final
        {
        public:
            explicit Awaiter(Operation& operation) noexcept
                : m_operation(operation)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_operation.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_operation.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                if (child.m_continuation)
                {
                    if constexpr (requires { awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)); })
                    {
                        awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                        awaiting.promise().MarkFinishedAndResume(awaiting);
                        return std::noop_coroutine();
                    }
                    return awaiting;
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;
                return std::noop_coroutine();
            }

            [[nodiscard]] Completion await_resume()
            {
                return m_operation.TakeResult();
            }

        private:
            Operation& m_operation;
        };

        class OwnedAwaiter final
        {
        public:
            explicit OwnedAwaiter(Operation&& operation) noexcept
                : m_operation(std::move(operation))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return Awaiter {m_operation}.await_suspend(awaiting);
            }

            [[nodiscard]] Completion await_resume()
            {
                return m_operation.TakeResult();
            }

        private:
            Operation m_operation;
        };

        [[nodiscard]] Awaiter operator co_await() & noexcept
        {
            return Awaiter {*this};
        }

        [[nodiscard]] OwnedAwaiter operator co_await() && noexcept
        {
            return OwnedAwaiter {std::move(*this)};
        }

    private:
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);

        void WaitUntilComplete()
        {
            if (!m_handle)
            {
                return;
            }

            auto& promise = m_handle.promise();
            while (!promise.m_finished.load(std::memory_order_acquire))
            {
                const auto generation = promise.m_finishedCondition.Load();
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

            auto handle = m_handle;
            m_handle    = {};

            auto& promise = handle.promise();
            if (!promise.m_finished.load(std::memory_order_acquire))
            {
                promise.m_detached.store(true, std::memory_order_release);
                return;
            }

            handle.destroy();
        }

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        bool                         m_resultTaken {false};
    };

    template<typename E>
    class Operation<void, E> final
    {
    public:
        using ValueType   = void;
        using ErrorType   = E;
        using Completion  = NGIN::Async::Completion<void, E>;
        using handle_type = typename Task<void, E>::handle_type;

        Operation() noexcept = default;

        Operation(handle_type handle, NGIN::Execution::ExecutorRef executor) noexcept
            : m_handle(handle), m_executor(executor)
        {
        }

        Operation(Operation&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_resultTaken(other.m_resultTaken)
        {
            other.m_handle      = {};
            other.m_executor    = {};
            other.m_resultTaken = false;
        }

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

        Operation(const Operation&)            = delete;
        Operation& operator=(const Operation&) = delete;

        ~Operation()
        {
            ReleaseHandle();
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_handle);
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsFault();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_completion.has_value() && m_handle.promise().m_completion->IsCanceled();
        }

        [[nodiscard]] std::optional<Completion> TryTakeResult()
        {
            if (!IsCompleted() || m_resultTaken)
            {
                return {};
            }

            m_resultTaken = true;
            return m_handle.promise().TakeCompletion();
        }

        [[nodiscard]] Completion TakeResult()
        {
            if (auto result = TryTakeResult())
            {
                return std::move(*result);
            }

            return Completion::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
        }

        class Awaiter final
        {
        public:
            explicit Awaiter(Operation& operation) noexcept
                : m_operation(operation)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_operation.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_operation.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                if (child.m_continuation)
                {
                    if constexpr (requires { awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)); })
                    {
                        awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                        awaiting.promise().MarkFinishedAndResume(awaiting);
                        return std::noop_coroutine();
                    }
                    return awaiting;
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;
                return std::noop_coroutine();
            }

            [[nodiscard]] Completion await_resume()
            {
                return m_operation.TakeResult();
            }

        private:
            Operation& m_operation;
        };

        class OwnedAwaiter final
        {
        public:
            explicit OwnedAwaiter(Operation&& operation) noexcept
                : m_operation(std::move(operation))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_operation.m_handle ||
                       m_operation.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return Awaiter {m_operation}.await_suspend(awaiting);
            }

            [[nodiscard]] Completion await_resume()
            {
                return m_operation.TakeResult();
            }

        private:
            Operation m_operation;
        };

        [[nodiscard]] Awaiter operator co_await() & noexcept
        {
            return Awaiter {*this};
        }

        [[nodiscard]] OwnedAwaiter operator co_await() && noexcept
        {
            return OwnedAwaiter {std::move(*this)};
        }

    private:
        template<typename TValue, typename TError>
        friend Operation<TValue, TError> Spawn(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend void Detach(TaskContext&, Task<TValue, TError>&&) noexcept;

        template<typename TValue, typename TError>
        friend NGIN::Async::Completion<TValue, TError> SyncWait(TaskContext&, Task<TValue, TError>&&);

        void WaitUntilComplete()
        {
            if (!m_handle)
            {
                return;
            }

            auto& promise = m_handle.promise();
            while (!promise.m_finished.load(std::memory_order_acquire))
            {
                const auto generation = promise.m_finishedCondition.Load();
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

            auto handle = m_handle;
            m_handle    = {};

            auto& promise = handle.promise();
            if (!promise.m_finished.load(std::memory_order_acquire))
            {
                promise.m_detached.store(true, std::memory_order_release);
                return;
            }

            handle.destroy();
        }

        handle_type                  m_handle {};
        NGIN::Execution::ExecutorRef m_executor {};
        bool                         m_resultTaken {false};
    };

    template<typename T, typename E>
    [[nodiscard]] Operation<T, E> Spawn(TaskContext& ctx, Task<T, E>&& task) noexcept
    {
        auto            handle = task.ReleaseForOperation();
        Operation<T, E> operation {handle, ctx.GetExecutor()};
        if (!handle)
        {
            return operation;
        }

        auto& promise      = handle.promise();
        promise.m_ctx      = &ctx;
        promise.m_executor = ctx.GetExecutor();
        if (!promise.m_executor.IsValid())
        {
            promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
            promise.MarkFinishedAndResume(handle);
            return operation;
        }

        promise.m_executor.Execute(handle);
        return operation;
    }

    template<typename T, typename E>
    void Detach(TaskContext& ctx, Task<T, E>&& task) noexcept
    {
        auto operation = Spawn(ctx, std::move(task));
        if (operation.m_handle && !operation.IsCompleted())
        {
            operation.m_handle.promise().m_detached.store(true, std::memory_order_release);
            operation.m_handle = {};
        }
    }

    template<typename T, typename E>
    [[nodiscard]] Completion<T, E> SyncWait(TaskContext& ctx, Task<T, E>&& task)
    {
        auto operation = Spawn(ctx, std::move(task));
        operation.WaitUntilComplete();
        return operation.TakeResult();
    }
}// namespace NGIN::Async
