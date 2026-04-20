/// <summary>
/// Core coroutine types: Task<T, E>/Task<void, E>, completion/result views, and TaskContext integration.
/// </summary>
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
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

                if (m_continuation)
                {
                    if (m_completionHandler)
                    {
                        m_completionHandler(std::coroutine_handle<>::from_address(self.address()), m_continuation);
                    }
                    else
                    {
                        ResumeOnExecutor(m_executor, m_continuation);
                    }
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

            void SetCompletion(Completion<T, E> completion) noexcept
            {
                if (!m_completion.has_value())
                {
                    m_completion.emplace(std::move(completion));
                }
            }

            void SetDomainError(E error) noexcept
            {
                SetCompletion(Completion<T, E>::DomainFailure(std::move(error)));
            }

            void SetCanceled() noexcept
            {
                SetCompletion(Completion<T, E>::Canceled());
            }

            void SetFault(AsyncFault fault) noexcept
            {
                SetCompletion(Completion<T, E>::Faulted(std::move(fault)));
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

            void SetCompletion(Completion<void, E> completion) noexcept
            {
                if (!m_completion.has_value())
                {
                    m_completion.emplace(std::move(completion));
                }
            }

            void SetDomainError(E error) noexcept
            {
                SetCompletion(Completion<void, E>::DomainFailure(std::move(error)));
            }

            void SetCanceled() noexcept
            {
                SetCompletion(Completion<void, E>::Canceled());
            }

            void SetFault(AsyncFault fault) noexcept
            {
                SetCompletion(Completion<void, E>::Faulted(std::move(fault)));
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
                    h.promise().MarkFinishedAndResume(h);
                }

                void await_resume() noexcept {}
            };

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_value(T value) noexcept
            {
                SetCompletion(Completion<T, E>::Success(std::move(value)));
            }

            void return_value(Completion<T, E> completion) noexcept
            {
                SetCompletion(std::move(completion));
            }

            void return_value(NGIN::Utilities::Expected<T, E> result) noexcept
            {
                if (!result)
                {
                    SetDomainError(std::move(result).TakeError());
                    return;
                }

                SetCompletion(Completion<T, E>::Success(std::move(result).TakeValue()));
            }

            void return_value(NGIN::Utilities::Unexpected<E> error) noexcept
            {
                SetDomainError(error.Error());
            }

            void return_value(E error) noexcept
                requires(!std::is_same_v<T, E>)
            {
                SetDomainError(std::move(error));
            }

            [[nodiscard]] TaskResult<T, E> MakeResult() const noexcept
            {
                return TaskResult<T, E> {this->m_completion ? &*this->m_completion : nullptr};
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire)), m_invalidCompletion(std::move(other.m_invalidCompletion))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
            other.m_invalidCompletion.reset();
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (m_handle)
                {
                    m_handle.destroy();
                }

                m_handle   = other.m_handle;
                m_executor = other.m_executor;
                m_started.store(other.m_started.load(std::memory_order_acquire), std::memory_order_release);
                m_invalidCompletion = std::move(other.m_invalidCompletion);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
                other.m_invalidCompletion.reset();
            }
            return *this;
        }

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            if (m_handle)
            {
                m_handle.destroy();
            }
        }

        [[nodiscard]] bool Start(TaskContext& ctx) noexcept
        {
            return TrySchedule(ctx);
        }

        [[nodiscard]] bool TrySchedule(TaskContext& ctx) noexcept
        {
            if (!m_handle)
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
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

        void Schedule(TaskContext& ctx) noexcept
        {
            (void) TrySchedule(ctx);
        }

        void Wait()
        {
            if (!m_handle)
            {
                return;
            }

            if (!m_started.load(std::memory_order_acquire) && !m_handle.promise().m_finished.load(std::memory_order_acquire))
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
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

        [[nodiscard]] TaskResult<T, E> Get()
        {
            if (!m_handle)
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
                return TaskResult<T, E> {&*m_invalidCompletion};
            }

            Wait();

            if (m_invalidCompletion.has_value())
            {
                return TaskResult<T, E> {&*m_invalidCompletion};
            }

            return m_handle.promise().MakeResult();
        }

        [[nodiscard]] const Completion<T, E>& GetCompletion()
        {
            return Get().CompletionRef();
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return m_handle && !IsCompleted();
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

        [[nodiscard]] handle_type Handle() const noexcept
        {
            return m_handle;
        }

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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
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

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
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

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (m_ctx == nullptr || m_ctx->IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                m_awaiting = awaiting;

                auto& child = m_task.m_handle.promise();
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

                m_ctx->GetCancellationToken().Register(
                        m_cancellationRegistration,
                        {},
                        {},
                        &Task::template CancelAwaitingContinuation<ParentPromise, CancellablePropagationAwaiter>,
                        this);

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
            TaskContext*             m_ctx;
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

        class CompletionAwaiter final
        {
        public:
            explicit CompletionAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_task.m_handle || m_task.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_task.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            [[nodiscard]] TaskResult<T, E> await_resume() noexcept
            {
                return m_task.Get();
            }

        private:
            Task& m_task;
        };

        [[nodiscard]] CompletionAwaiter AsCompletion() noexcept
        {
            return CompletionAwaiter {*this};
        }

#if NGIN_ASYNC_HAS_EXCEPTIONS
        class ThrowingAwaiter final
        {
        public:
            explicit ThrowingAwaiter(Task& task) noexcept
                : m_completion(task.AsCompletion())
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_completion.await_ready();
            }

            template<typename ParentPromise>
            auto await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_completion.await_suspend(awaiting);
            }

            [[nodiscard]] T await_resume()
            {
                return Task::TakeOrThrow(m_completion.await_resume());
            }

        private:
            CompletionAwaiter m_completion;
        };

        class ThrowingTaskView final
        {
        public:
            explicit ThrowingTaskView(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] ThrowingAwaiter operator co_await() noexcept
            {
                return ThrowingAwaiter {m_task};
            }

            [[nodiscard]] T Get()
            {
                return Task::TakeOrThrow(m_task.Get());
            }

        private:
            Task& m_task;
        };

        class OwnedThrowingAwaiter final
        {
        public:
            explicit OwnedThrowingAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_task.m_handle || m_task.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_task.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            [[nodiscard]] T await_resume()
            {
                return Task::TakeOrThrow(m_task.Get());
            }

        private:
            Task m_task;
        };

        class OwnedThrowingTaskView final
        {
        public:
            explicit OwnedThrowingTaskView(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] OwnedThrowingAwaiter operator co_await() noexcept
            {
                return OwnedThrowingAwaiter {std::move(m_task)};
            }

            [[nodiscard]] T Get()
            {
                return Task::TakeOrThrow(m_task.Get());
            }

        private:
            Task m_task;
        };

        [[nodiscard]] ThrowingTaskView AsThrowing() & noexcept
        {
            return ThrowingTaskView {*this};
        }

        [[nodiscard]] OwnedThrowingTaskView AsThrowing() && noexcept
        {
            return OwnedThrowingTaskView {std::move(*this)};
        }
#endif

        template<typename F>
        auto MapError(F&& func) &;

        template<typename F>
        auto MapValue(F&& func) &;

        template<typename F>
        auto MapCompletion(F&& func) &;

        template<typename E2, typename F>
        auto As(F&& func) &;

        template<typename F>
        auto ContinueWith(TaskContext& ctx, F&& func);

    private:
#if NGIN_ASYNC_HAS_EXCEPTIONS
        [[noreturn]] static void ThrowFromResult(TaskResult<T, E> result)
        {
            if (result.IsDomainError())
            {
                AsyncExceptionTraits<E>::Throw(result.DomainError());
            }

            if (result.IsCanceled())
            {
                throw AsyncCanceledException();
            }

            assert(result.IsFault());
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            if (result.Fault().capturedException)
            {
                std::rethrow_exception(result.Fault().capturedException);
            }
#endif
            throw AsyncFaultException(result.Fault());
        }

        [[nodiscard]] static T TakeOrThrow(TaskResult<T, E> result)
        {
            if (result.Succeeded())
            {
                return std::move(result.Value());
            }

            ThrowFromResult(result);
        }
#endif

        void SetInvalidCompletion(AsyncFaultCode code) noexcept
        {
            m_invalidCompletion = Completion<T, E>::Faulted(MakeAsyncFault(code));
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

        handle_type                     m_handle {};
        NGIN::Execution::ExecutorRef    m_executor {};
        std::atomic_bool                m_started {false};
        std::optional<Completion<T, E>> m_invalidCompletion {};
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
                    h.promise().MarkFinishedAndResume(h);
                }

                void await_resume() noexcept {}
            };

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept
            {
                SetCompletion(Completion<void, E>::Success());
            }

            [[nodiscard]] TaskResult<void, E> MakeResult() const noexcept
            {
                return TaskResult<void, E> {this->m_completion ? &*this->m_completion : nullptr};
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle), m_executor(other.m_executor), m_started(other.m_started.load(std::memory_order_acquire)), m_invalidCompletion(std::move(other.m_invalidCompletion))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
            other.m_invalidCompletion.reset();
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (m_handle)
                {
                    m_handle.destroy();
                }

                m_handle   = other.m_handle;
                m_executor = other.m_executor;
                m_started.store(other.m_started.load(std::memory_order_acquire), std::memory_order_release);
                m_invalidCompletion = std::move(other.m_invalidCompletion);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
                other.m_invalidCompletion.reset();
            }
            return *this;
        }

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            if (m_handle)
            {
                m_handle.destroy();
            }
        }

        [[nodiscard]] bool Start(TaskContext& ctx) noexcept
        {
            return TrySchedule(ctx);
        }

        [[nodiscard]] bool TrySchedule(TaskContext& ctx) noexcept
        {
            if (!m_handle)
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
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

        void Schedule(TaskContext& ctx) noexcept
        {
            (void) TrySchedule(ctx);
        }

        void Wait()
        {
            if (!m_handle)
            {
                return;
            }

            if (!m_started.load(std::memory_order_acquire) && !m_handle.promise().m_finished.load(std::memory_order_acquire))
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
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

        [[nodiscard]] TaskResult<void, E> Get()
        {
            if (!m_handle)
            {
                SetInvalidCompletion(AsyncFaultCode::InvalidTaskUsage);
                return TaskResult<void, E> {&*m_invalidCompletion};
            }

            Wait();

            if (m_invalidCompletion.has_value())
            {
                return TaskResult<void, E> {&*m_invalidCompletion};
            }

            return m_handle.promise().MakeResult();
        }

        [[nodiscard]] const Completion<void, E>& GetCompletion()
        {
            return Get().CompletionRef();
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            return m_handle && m_handle.promise().m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return m_handle && !IsCompleted();
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

        [[nodiscard]] handle_type Handle() const noexcept
        {
            return m_handle;
        }

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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
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

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
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

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v2.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (m_ctx == nullptr || m_ctx->IsCancellationRequested())
                {
                    awaiting.promise().SetCanceled();
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                m_awaiting = awaiting;

                auto& child = m_task.m_handle.promise();
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

                m_ctx->GetCancellationToken().Register(
                        m_cancellationRegistration,
                        {},
                        {},
                        &Task::template CancelAwaitingContinuation<ParentPromise, CancellablePropagationAwaiter>,
                        this);

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
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
            TaskContext*             m_ctx;
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

        class CompletionAwaiter final
        {
        public:
            explicit CompletionAwaiter(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_task.m_handle || m_task.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_task.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            [[nodiscard]] TaskResult<void, E> await_resume() noexcept
            {
                return m_task.Get();
            }

        private:
            Task& m_task;
        };

        [[nodiscard]] CompletionAwaiter AsCompletion() noexcept
        {
            return CompletionAwaiter {*this};
        }

#if NGIN_ASYNC_HAS_EXCEPTIONS
        class ThrowingAwaiter final
        {
        public:
            explicit ThrowingAwaiter(Task& task) noexcept
                : m_completion(task.AsCompletion())
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_completion.await_ready();
            }

            template<typename ParentPromise>
            auto await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                return m_completion.await_suspend(awaiting);
            }

            void await_resume()
            {
                Task::HandleThrowingResult(m_completion.await_resume());
            }

        private:
            CompletionAwaiter m_completion;
        };

        class ThrowingTaskView final
        {
        public:
            explicit ThrowingTaskView(Task& task) noexcept
                : m_task(task)
            {
            }

            [[nodiscard]] ThrowingAwaiter operator co_await() noexcept
            {
                return ThrowingAwaiter {m_task};
            }

            void Get()
            {
                Task::HandleThrowingResult(m_task.Get());
            }

        private:
            Task& m_task;
        };

        class OwnedThrowingAwaiter final
        {
        public:
            explicit OwnedThrowingAwaiter(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !m_task.m_handle || m_task.m_handle.promise().m_finished.load(std::memory_order_acquire);
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                if (!m_task.m_handle)
                {
                    return awaiting;
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    return awaiting;
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation      = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            void await_resume()
            {
                Task::HandleThrowingResult(m_task.Get());
            }

        private:
            Task m_task;
        };

        class OwnedThrowingTaskView final
        {
        public:
            explicit OwnedThrowingTaskView(Task&& task) noexcept
                : m_task(std::move(task))
            {
            }

            [[nodiscard]] OwnedThrowingAwaiter operator co_await() noexcept
            {
                return OwnedThrowingAwaiter {std::move(m_task)};
            }

            void Get()
            {
                Task::HandleThrowingResult(m_task.Get());
            }

        private:
            Task m_task;
        };

        [[nodiscard]] ThrowingTaskView AsThrowing() & noexcept
        {
            return ThrowingTaskView {*this};
        }

        [[nodiscard]] OwnedThrowingTaskView AsThrowing() && noexcept
        {
            return OwnedThrowingTaskView {std::move(*this)};
        }
#endif

        template<typename F>
        auto MapError(F&& func) &;

        template<typename F>
        auto MapValue(F&& func) &;

        template<typename F>
        auto MapCompletion(F&& func) &;

        template<typename E2, typename F>
        auto As(F&& func) &;

        template<typename F>
        auto ContinueWith(TaskContext& ctx, F&& func);

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        static Task<void, E> Delay(TaskContext& ctx, const TUnit& duration)
        {
            co_await ctx.Delay(duration);
            co_return;
        }

    private:
#if NGIN_ASYNC_HAS_EXCEPTIONS
        static void HandleThrowingResult(TaskResult<void, E> result)
        {
            if (result.Succeeded())
            {
                return;
            }

            if (result.IsDomainError())
            {
                AsyncExceptionTraits<E>::Throw(result.DomainError());
            }

            if (result.IsCanceled())
            {
                throw AsyncCanceledException();
            }

            assert(result.IsFault());
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            if (result.Fault().capturedException)
            {
                std::rethrow_exception(result.Fault().capturedException);
            }
#endif
            throw AsyncFaultException(result.Fault());
        }
#endif

        void SetInvalidCompletion(AsyncFaultCode code) noexcept
        {
            m_invalidCompletion = Completion<void, E>::Faulted(MakeAsyncFault(code));
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

        handle_type                        m_handle {};
        NGIN::Execution::ExecutorRef       m_executor {};
        std::atomic_bool                   m_started {false};
        std::optional<Completion<void, E>> m_invalidCompletion {};
    };
}// namespace NGIN::Async

#include <NGIN/Async/WhenAny.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        template<typename T, typename E, typename Func>
        auto ContinueWithImpl(Task<T, E>* parent, TaskContext* context, Func func) -> std::invoke_result_t<Func, T>
        {
            using NextTask       = std::invoke_result_t<Func, T>;
            using NextValue      = TaskValueType<NextTask>;
            using NextError      = typename NextTask::ErrorType;
            using NextCompletion = Completion<NextValue, NextError>;

            parent->Schedule(*context);

            const auto parentIndex = co_await WhenAny(*context, *parent);
            if (context->IsCancellationRequested() || parentIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }

            auto parentCompletion = parent->Get();
            if (parentCompletion.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::DomainFailure(parentCompletion.DomainError());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::DomainFailure(parentCompletion.DomainError());
                }
            }
            if (parentCompletion.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }
            if (parentCompletion.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Faulted(parentCompletion.Fault());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Faulted(parentCompletion.Fault());
                }
            }

            auto       next      = std::invoke(std::move(func), std::move(parentCompletion.Value()));
            const auto nextIndex = co_await WhenAny(*context, next);
            if (context->IsCancellationRequested() || nextIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }

            auto nextCompletion = next.Get();
            if (nextCompletion.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::DomainFailure(nextCompletion.DomainError());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::DomainFailure(nextCompletion.DomainError());
                }
            }
            if (nextCompletion.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }
            if (nextCompletion.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Faulted(nextCompletion.Fault());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Faulted(nextCompletion.Fault());
                }
            }

            if constexpr (std::is_void_v<NextValue>)
            {
                co_return;
            }
            else
            {
                co_return std::move(nextCompletion.Value());
            }
        }

        template<typename E, typename Func>
        auto ContinueWithImpl(Task<void, E>* parent, TaskContext* context, Func func) -> std::invoke_result_t<Func>
        {
            using NextTask       = std::invoke_result_t<Func>;
            using NextValue      = TaskValueType<NextTask>;
            using NextError      = typename NextTask::ErrorType;
            using NextCompletion = Completion<NextValue, NextError>;

            parent->Schedule(*context);

            const auto parentIndex = co_await WhenAny(*context, *parent);
            if (context->IsCancellationRequested() || parentIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }

            auto parentCompletion = parent->Get();
            if (parentCompletion.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::DomainFailure(parentCompletion.DomainError());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::DomainFailure(parentCompletion.DomainError());
                }
            }
            if (parentCompletion.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }
            if (parentCompletion.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Faulted(parentCompletion.Fault());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Faulted(parentCompletion.Fault());
                }
            }

            auto       next      = std::invoke(std::move(func));
            const auto nextIndex = co_await WhenAny(*context, next);
            if (context->IsCancellationRequested() || nextIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }

            auto nextCompletion = next.Get();
            if (nextCompletion.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::DomainFailure(nextCompletion.DomainError());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::DomainFailure(nextCompletion.DomainError());
                }
            }
            if (nextCompletion.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Canceled();
                }
            }
            if (nextCompletion.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NGIN::Async::Faulted(nextCompletion.Fault());
                    co_return;
                }
                else
                {
                    co_return NextCompletion::Faulted(nextCompletion.Fault());
                }
            }

            if constexpr (std::is_void_v<NextValue>)
            {
                co_return;
            }
            else
            {
                co_return std::move(nextCompletion.Value());
            }
        }

        template<typename T, typename E, typename F>
        auto MapErrorImpl(Task<T, E>* task, F func) -> Task<T, std::invoke_result_t<F, const E&>>
        {
            using E2            = std::invoke_result_t<F, const E&>;
            using OutCompletion = Completion<T, E2>;

            auto completion = co_await task->AsCompletion();
            if (completion.Succeeded())
            {
                co_return std::move(completion.Value());
            }
            if (completion.IsDomainError())
            {
                co_return OutCompletion::DomainFailure(std::invoke(func, completion.DomainError()));
            }
            if (completion.IsCanceled())
            {
                co_return OutCompletion::Canceled();
            }

            co_return OutCompletion::Faulted(completion.Fault());
        }

        template<typename E, typename F>
        auto MapErrorImpl(Task<void, E>* task, F func) -> Task<void, std::invoke_result_t<F, const E&>>
        {
            auto completion = co_await task->AsCompletion();
            if (completion.Succeeded())
            {
                co_return;
            }
            if (completion.IsDomainError())
            {
                co_await NGIN::Async::DomainFailure(std::invoke(func, completion.DomainError()));
                co_return;
            }
            if (completion.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }

            co_await NGIN::Async::Faulted(completion.Fault());
            co_return;
        }

        template<typename T, typename E, typename F>
        auto MapValueImpl(Task<T, E>* task, F func) -> Task<std::invoke_result_t<F, T>, E>
        {
            using U             = std::invoke_result_t<F, T>;
            using OutCompletion = Completion<U, E>;

            auto completion = co_await task->AsCompletion();
            if (completion.IsDomainError())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::DomainFailure(completion.DomainError());
                    co_return;
                }
                else
                {
                    co_return OutCompletion::DomainFailure(completion.DomainError());
                }
            }
            if (completion.IsCanceled())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return OutCompletion::Canceled();
                }
            }
            if (completion.IsFault())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::Faulted(completion.Fault());
                    co_return;
                }
                else
                {
                    co_return OutCompletion::Faulted(completion.Fault());
                }
            }

            if constexpr (std::is_void_v<U>)
            {
                std::invoke(func, std::move(completion.Value()));
                co_return;
            }
            else
            {
                co_return std::invoke(func, std::move(completion.Value()));
            }
        }

        template<typename E, typename F>
        auto MapValueImpl(Task<void, E>* task, F func) -> Task<std::invoke_result_t<F>, E>
        {
            using U             = std::invoke_result_t<F>;
            using OutCompletion = Completion<U, E>;

            auto completion = co_await task->AsCompletion();
            if (completion.IsDomainError())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::DomainFailure(completion.DomainError());
                    co_return;
                }
                else
                {
                    co_return OutCompletion::DomainFailure(completion.DomainError());
                }
            }
            if (completion.IsCanceled())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                else
                {
                    co_return OutCompletion::Canceled();
                }
            }
            if (completion.IsFault())
            {
                if constexpr (std::is_void_v<U>)
                {
                    co_await NGIN::Async::Faulted(completion.Fault());
                    co_return;
                }
                else
                {
                    co_return OutCompletion::Faulted(completion.Fault());
                }
            }

            if constexpr (std::is_void_v<U>)
            {
                std::invoke(func);
                co_return;
            }
            else
            {
                co_return std::invoke(func);
            }
        }

        template<typename T, typename E, typename F>
        auto MapCompletionImpl(Task<T, E>* task, F func)
                -> Task<typename std::invoke_result_t<F, TaskResult<T, E>>::ValueType,
                        typename std::invoke_result_t<F, TaskResult<T, E>>::ErrorType>
        {
            using OutCompletion = std::invoke_result_t<F, TaskResult<T, E>>;
            using U             = typename OutCompletion::ValueType;

            auto completion = co_await task->AsCompletion();
            auto mapped     = std::invoke(func, completion);
            if constexpr (std::is_void_v<U>)
            {
                if (mapped.Succeeded())
                {
                    co_return;
                }
                if (mapped.IsDomainError())
                {
                    co_await NGIN::Async::DomainFailure(std::move(mapped).DomainError());
                    co_return;
                }
                if (mapped.IsCanceled())
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }

                co_await NGIN::Async::Faulted(std::move(mapped).Fault());
                co_return;
            }
            else
            {
                co_return mapped;
            }
        }

        template<typename T, typename E2, typename E, typename F>
        auto MapErrorTypeImpl(Task<T, E>* task, F func) -> Task<T, E2>
        {
            using OutCompletion = Completion<T, E2>;
            auto completion     = co_await task->AsCompletion();
            if (completion.Succeeded())
            {
                co_return std::move(completion.Value());
            }
            if (completion.IsDomainError())
            {
                co_return OutCompletion::DomainFailure(std::invoke(func, completion.DomainError()));
            }
            if (completion.IsCanceled())
            {
                co_return OutCompletion::Canceled();
            }

            co_return OutCompletion::Faulted(completion.Fault());
        }

        template<typename E2, typename E, typename F>
        auto MapErrorTypeImpl(Task<void, E>* task, F func) -> Task<void, E2>
        {
            auto completion = co_await task->AsCompletion();
            if (completion.Succeeded())
            {
                co_return;
            }
            if (completion.IsDomainError())
            {
                co_await NGIN::Async::DomainFailure(std::invoke(func, completion.DomainError()));
                co_return;
            }
            if (completion.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }

            co_await NGIN::Async::Faulted(completion.Fault());
            co_return;
        }
    }// namespace detail

    template<typename T, typename E>
    template<typename F>
    auto Task<T, E>::MapError(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapErrorImpl(this, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename F>
    auto Task<void, E>::MapError(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapErrorImpl(this, Func(std::forward<F>(func)));
    }

    template<typename T, typename E>
    template<typename F>
    auto Task<T, E>::MapValue(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapValueImpl(this, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename F>
    auto Task<void, E>::MapValue(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapValueImpl(this, Func(std::forward<F>(func)));
    }

    template<typename T, typename E>
    template<typename F>
    auto Task<T, E>::MapCompletion(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapCompletionImpl(this, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename F>
    auto Task<void, E>::MapCompletion(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapCompletionImpl(this, Func(std::forward<F>(func)));
    }

    template<typename T, typename E>
    template<typename E2, typename F>
    auto Task<T, E>::As(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapErrorTypeImpl<T, E2>(this, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename E2, typename F>
    auto Task<void, E>::As(F&& func) &
    {
        using Func = std::decay_t<F>;
        return detail::MapErrorTypeImpl<E2>(this, Func(std::forward<F>(func)));
    }

    template<typename T, typename E>
    template<typename F>
    auto Task<T, E>::ContinueWith(TaskContext& ctx, F&& func)
    {
        using Func     = std::decay_t<F>;
        using NextTask = std::invoke_result_t<Func, T>;
        static_assert(detail::IsTaskTypeV<NextTask>, "ContinueWith expects a function returning Task<...>.");
        static_assert(std::is_same_v<typename NextTask::ErrorType, E>, "ContinueWith requires identical error types.");
        return detail::ContinueWithImpl<T, E, Func>(this, &ctx, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename F>
    auto Task<void, E>::ContinueWith(TaskContext& ctx, F&& func)
    {
        using Func     = std::decay_t<F>;
        using NextTask = std::invoke_result_t<Func>;
        static_assert(detail::IsTaskTypeV<NextTask>, "ContinueWith expects a function returning Task<...>.");
        static_assert(std::is_same_v<typename NextTask::ErrorType, E>, "ContinueWith requires identical error types.");
        return detail::ContinueWithImpl<E, Func>(this, &ctx, Func(std::forward<F>(func)));
    }
}// namespace NGIN::Async
