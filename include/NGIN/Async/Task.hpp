/// <summary>
/// Core coroutine types: Task<T, E>/Task<void, E>, TaskOutcome, and TaskContext integration.
/// </summary>
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Async
{
    enum class TaskStatus : NGIN::UInt8
    {
        Pending,
        Succeeded,
        DomainError,
        Canceled,
        Fault,
    };

    class BaseTask
    {
    };

    template<typename T, typename E>
    class Task;

    template<typename E>
    class Task<void, E>;

    template<typename T, typename E>
    class TaskOutcome
    {
    public:
        [[nodiscard]] TaskStatus Status() const noexcept { return m_status; }
        [[nodiscard]] bool       Succeeded() const noexcept { return m_status == TaskStatus::Succeeded; }
        [[nodiscard]] bool       HasValue() const noexcept { return Succeeded() && m_value != nullptr; }
        [[nodiscard]] bool       IsDomainError() const noexcept { return m_status == TaskStatus::DomainError; }
        [[nodiscard]] bool       IsCanceled() const noexcept { return m_status == TaskStatus::Canceled; }
        [[nodiscard]] bool       IsFault() const noexcept { return m_status == TaskStatus::Fault; }

        [[nodiscard]] T& Value() const
        {
            assert(HasValue());
            return *m_value;
        }

        [[nodiscard]] T& operator*() const
        {
            return Value();
        }

        [[nodiscard]] T* operator->() const
        {
            assert(HasValue());
            return m_value;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] E& DomainError() const
        {
            assert(IsDomainError() && m_domainError != nullptr);
            return *m_domainError;
        }

        [[nodiscard]] AsyncFault& Fault() const
        {
            assert(IsFault() && m_fault != nullptr);
            return *m_fault;
        }

    private:
        template<typename, typename>
        friend class Task;

        constexpr TaskOutcome(TaskStatus status, T* value, E* domainError, AsyncFault* fault) noexcept
            : m_status(status)
            , m_value(value)
            , m_domainError(domainError)
            , m_fault(fault)
        {
        }

        TaskStatus         m_status {TaskStatus::Pending};
        T*                 m_value {nullptr};
        E*                 m_domainError {nullptr};
        AsyncFault*        m_fault {nullptr};
    };

    template<typename E>
    class TaskOutcome<void, E>
    {
    public:
        [[nodiscard]] TaskStatus Status() const noexcept { return m_status; }
        [[nodiscard]] bool       Succeeded() const noexcept { return m_status == TaskStatus::Succeeded; }
        [[nodiscard]] bool       IsDomainError() const noexcept { return m_status == TaskStatus::DomainError; }
        [[nodiscard]] bool       IsCanceled() const noexcept { return m_status == TaskStatus::Canceled; }
        [[nodiscard]] bool       IsFault() const noexcept { return m_status == TaskStatus::Fault; }
        [[nodiscard]] explicit operator bool() const noexcept { return Succeeded(); }

        [[nodiscard]] E& DomainError() const
        {
            assert(IsDomainError() && m_domainError != nullptr);
            return *m_domainError;
        }

        [[nodiscard]] AsyncFault& Fault() const
        {
            assert(IsFault() && m_fault != nullptr);
            return *m_fault;
        }

    private:
        template<typename, typename>
        friend class Task;

        constexpr TaskOutcome(TaskStatus status, E* domainError, AsyncFault* fault) noexcept
            : m_status(status)
            , m_domainError(domainError)
            , m_fault(fault)
        {
        }

        TaskStatus        m_status {TaskStatus::Pending};
        E*                m_domainError {nullptr};
        AsyncFault*       m_fault {nullptr};
    };

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

        template<typename E>
        struct PromiseCommon
        {
            using DomainErrorType = E;
            using CompletionHandler = void (*)(std::coroutine_handle<>, std::coroutine_handle<>) noexcept;

            TaskStatus                    m_status {TaskStatus::Pending};
            std::optional<E>              m_domainError {};
            std::optional<AsyncFault>     m_fault {};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr            m_exception {};
            using ExceptionPropagator = void (*)(std::exception_ptr, std::coroutine_handle<>) noexcept;
            ExceptionPropagator           m_setChildException {};

            void SetChildException(std::exception_ptr ex) noexcept
            {
                if (!m_exception)
                {
                    m_exception = ex;
                }
            }
#endif
            std::atomic<bool>             m_finished {false};
            NGIN::Sync::AtomicCondition   m_finishedCondition {};
            std::coroutine_handle<>       m_continuation {};
            CompletionHandler             m_completionHandler {};
            TaskContext*                  m_ctx {nullptr};
            NGIN::Execution::ExecutorRef  m_executor {};

            PromiseCommon() = default;

            explicit PromiseCommon(TaskContext& ctx) noexcept
                : m_ctx(&ctx)
                , m_executor(ctx.GetExecutor())
            {
            }

            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit PromiseCommon(TaskContext& ctx, Args&&...) noexcept
                : PromiseCommon(ctx)
            {
            }

            void SetDomainError(E error) noexcept
            {
                m_status = TaskStatus::DomainError;
                m_fault.reset();
                m_domainError.emplace(std::move(error));
            }

            void SetCanceled() noexcept
            {
                m_status = TaskStatus::Canceled;
                m_domainError.reset();
                m_fault.reset();
            }

            void SetFault(AsyncFault fault) noexcept
            {
                m_status = TaskStatus::Fault;
                m_domainError.reset();
                m_fault.emplace(std::move(fault));
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

            template<typename ChildPromise>
            void PropagateFromChild(const ChildPromise& child) noexcept
            {
                switch (child.m_status)
                {
                    case TaskStatus::DomainError:
                        m_status = TaskStatus::DomainError;
                        m_fault.reset();
                        m_domainError = *child.m_domainError;
                        break;
                    case TaskStatus::Canceled:
                        SetCanceled();
                        break;
                    case TaskStatus::Fault:
                        m_status = TaskStatus::Fault;
                        m_domainError.reset();
                        m_fault = *child.m_fault;
                        break;
                    case TaskStatus::Pending:
                    case TaskStatus::Succeeded:
                        break;
                }
            }

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                SetFault(MakeAsyncFault(AsyncFaultCode::UnhandledException));
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_exception = std::current_exception();
#endif
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

    template<typename T, typename E = NoError>
    class Task final : public BaseTask
    {
    public:
        using ValueType = T;
        using ErrorType = E;

        struct promise_type final : detail::PromiseCommon<E>
        {
            std::optional<T> m_value {};

            using Base = detail::PromiseCommon<E>;
            using Base::Base;
            using Base::SetCanceled;
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
                if (this->m_status != TaskStatus::Pending)
                {
                    return;
                }

                this->m_status = TaskStatus::Succeeded;
                this->m_domainError.reset();
                this->m_fault.reset();
                m_value.emplace(std::move(value));
            }

            void return_value(NGIN::Utilities::Expected<T, E> result) noexcept
            {
                if (!result)
                {
                    SetDomainError(std::move(result).TakeError());
                    return;
                }

                return_value(std::move(result).TakeValue());
            }

            void return_value(NGIN::Utilities::Unexpected<E> error) noexcept
            {
                SetDomainError(error.Error());
            }

            void return_value(CanceledTag) noexcept
            {
                SetCanceled();
            }

            void return_value(FaultResult result) noexcept
            {
                SetFault(result.fault);
            }

            [[nodiscard]] TaskOutcome<T, E> MakeOutcome() const noexcept
            {
                T*          value       = (this->m_status == TaskStatus::Succeeded && m_value.has_value()) ? const_cast<T*>(&*m_value) : nullptr;
                E*          domainError = (this->m_status == TaskStatus::DomainError && this->m_domainError.has_value())
                                                ? const_cast<E*>(&*this->m_domainError)
                                                : nullptr;
                AsyncFault* fault       = (this->m_status == TaskStatus::Fault && this->m_fault.has_value())
                                                ? const_cast<AsyncFault*>(&*this->m_fault)
                                                : nullptr;
                return TaskOutcome<T, E> {this->m_status, value, domainError, fault};
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h)
            , m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle)
            , m_executor(other.m_executor)
            , m_started(other.m_started.load(std::memory_order_acquire))
            , m_invalidFault(std::move(other.m_invalidFault))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
            other.m_invalidFault.reset();
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
                m_invalidFault = std::move(other.m_invalidFault);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
                other.m_invalidFault.reset();
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
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
                return false;
            }

            bool expected = false;
            if (!m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            m_executor = ctx.GetExecutor();
            auto& promise = m_handle.promise();
            promise.m_ctx = &ctx;
            promise.m_executor = m_executor;

            if (!m_executor.IsValid())
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
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

        [[nodiscard]] TaskOutcome<T, E> Get()
        {
            if (!m_handle)
            {
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
                return TaskOutcome<T, E> {TaskStatus::Fault, nullptr, nullptr, &*m_invalidFault};
            }

            Wait();

            if (m_invalidFault.has_value())
            {
                return TaskOutcome<T, E> {TaskStatus::Fault, nullptr, nullptr, &*m_invalidFault};
            }

            return m_handle.promise().MakeOutcome();
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
            return m_handle && m_handle.promise().m_status == TaskStatus::Fault;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_status == TaskStatus::Canceled;
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
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation    = awaiting;
                child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(promise.m_status == TaskStatus::Succeeded);
                return std::move(*promise.m_value);
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
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation = awaiting;
                child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(promise.m_status == TaskStatus::Succeeded);
                return std::move(*promise.m_value);
            }

        private:
            Task m_task;
        };

        class CancellablePropagationAwaiter final
        {
        public:
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task)
                , m_ctx(&ctx)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation = awaiting;
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
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(promise.m_status == TaskStatus::Succeeded);
                return std::move(*promise.m_value);
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&                               m_task;
            TaskContext*                        m_ctx;
            CancellationRegistration            m_cancellationRegistration {};
            std::coroutine_handle<>             m_awaiting {};
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

        class OutcomeAwaiter final
        {
        public:
            explicit OutcomeAwaiter(Task& task) noexcept
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

                child.m_continuation = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            [[nodiscard]] TaskOutcome<T, E> await_resume() noexcept
            {
                return m_task.Get();
            }

        private:
            Task& m_task;
        };

        [[nodiscard]] OutcomeAwaiter AsOutcome() noexcept
        {
            return OutcomeAwaiter {*this};
        }

        struct ReturnErrorAwaiter final
        {
            E error {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetDomainError(error);
                return false;
            }
            void await_resume() const noexcept {}
        };

        struct ReturnCanceledAwaiter final
        {
            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetCanceled();
                return false;
            }
            void await_resume() const noexcept {}
        };

        struct ReturnFaultAwaiter final
        {
            AsyncFault fault {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetFault(fault);
                return false;
            }
            void await_resume() const noexcept {}
        };

        [[nodiscard]] static ReturnErrorAwaiter ReturnError(E error) noexcept
        {
            return ReturnErrorAwaiter {std::move(error)};
        }

        [[nodiscard]] static ReturnCanceledAwaiter ReturnCanceled() noexcept
        {
            return {};
        }

        [[nodiscard]] static ReturnFaultAwaiter ReturnFault(AsyncFault fault) noexcept
        {
            return ReturnFaultAwaiter {fault};
        }

        template<typename F>
        auto ContinueWith(TaskContext& ctx, F&& func);

    private:
        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            auto childHandle  = handle_type::from_address(self.address());
            auto parentHandle = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto& child       = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return;
            }

            if (child.m_status == TaskStatus::Succeeded)
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
        std::optional<AsyncFault>    m_invalidFault {};
    };

    template<typename E>
    class Task<void, E> final : public BaseTask
    {
    public:
        using ValueType = void;
        using ErrorType = E;

        struct promise_type final : detail::PromiseCommon<E>
        {
            using Base = detail::PromiseCommon<E>;
            using Base::Base;
            using Base::SetCanceled;
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
                if (this->m_status != TaskStatus::Pending)
                {
                    return;
                }

                this->m_status = TaskStatus::Succeeded;
                this->m_domainError.reset();
                this->m_fault.reset();
            }

            [[nodiscard]] TaskOutcome<void, E> MakeOutcome() const noexcept
            {
                E*          domainError = (this->m_status == TaskStatus::DomainError && this->m_domainError.has_value())
                                                ? const_cast<E*>(&*this->m_domainError)
                                                : nullptr;
                AsyncFault* fault       = (this->m_status == TaskStatus::Fault && this->m_fault.has_value())
                                                ? const_cast<AsyncFault*>(&*this->m_fault)
                                                : nullptr;
                return TaskOutcome<void, E> {this->m_status, domainError, fault};
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type h) noexcept
            : m_handle(h)
            , m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
        {
        }

        Task(Task&& other) noexcept
            : m_handle(other.m_handle)
            , m_executor(other.m_executor)
            , m_started(other.m_started.load(std::memory_order_acquire))
            , m_invalidFault(std::move(other.m_invalidFault))
        {
            other.m_handle   = nullptr;
            other.m_executor = {};
            other.m_started.store(false, std::memory_order_release);
            other.m_invalidFault.reset();
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
                m_invalidFault = std::move(other.m_invalidFault);

                other.m_handle   = nullptr;
                other.m_executor = {};
                other.m_started.store(false, std::memory_order_release);
                other.m_invalidFault.reset();
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
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
                return false;
            }

            bool expected = false;
            if (!m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            m_executor = ctx.GetExecutor();
            auto& promise = m_handle.promise();
            promise.m_ctx = &ctx;
            promise.m_executor = m_executor;

            if (!m_executor.IsValid())
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
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

        [[nodiscard]] TaskOutcome<void, E> Get()
        {
            if (!m_handle)
            {
                m_invalidFault = MakeAsyncFault(AsyncFaultCode::InvalidState);
                return TaskOutcome<void, E> {TaskStatus::Fault, nullptr, &*m_invalidFault};
            }

            Wait();

            if (m_invalidFault.has_value())
            {
                return TaskOutcome<void, E> {TaskStatus::Fault, nullptr, &*m_invalidFault};
            }

            return m_handle.promise().MakeOutcome();
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
            return m_handle && m_handle.promise().m_status == TaskStatus::Fault;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_handle && m_handle.promise().m_status == TaskStatus::Canceled;
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
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation = awaiting;
                child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(m_task.m_handle.promise().m_status == TaskStatus::Succeeded);
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
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                auto& child = m_task.m_handle.promise();
                if (child.m_finished.load(std::memory_order_acquire))
                {
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation = awaiting;
                child.m_completionHandler = &Task::template PropagateChildCompletion<ParentPromise>;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                child.m_setChildException = &Task::template PropagateChildException<ParentPromise>;
#endif

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(m_task.m_handle.promise().m_status == TaskStatus::Succeeded);
            }

        private:
            Task m_task;
        };

        class CancellablePropagationAwaiter final
        {
        public:
            CancellablePropagationAwaiter(Task& task, TaskContext& ctx) noexcept
                : m_task(task)
                , m_ctx(&ctx)
            {
            }

            [[nodiscard]] bool await_ready() const noexcept
            {
                return m_task.m_handle &&
                       m_task.m_handle.promise().m_finished.load(std::memory_order_acquire) &&
                       m_task.m_handle.promise().m_status == TaskStatus::Succeeded;
            }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::is_same_v<typename ParentPromise::DomainErrorType, E>,
                              "Await propagation requires identical Task error types in v1.");

                if (!m_task.m_handle)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                    if (child.m_status == TaskStatus::Succeeded)
                    {
                        return awaiting;
                    }

                    awaiting.promise().PropagateFromChild(child);
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                if (child.m_continuation)
                {
                    awaiting.promise().SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                    awaiting.promise().MarkFinishedAndResume(awaiting);
                    return std::noop_coroutine();
                }

                child.m_continuation = awaiting;
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
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
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
                assert(m_task.m_handle.promise().m_status == TaskStatus::Succeeded);
            }

        private:
            template<typename, typename>
            friend class Task;

            Task&                               m_task;
            TaskContext*                        m_ctx;
            CancellationRegistration            m_cancellationRegistration {};
            std::coroutine_handle<>             m_awaiting {};
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

        class OutcomeAwaiter final
        {
        public:
            explicit OutcomeAwaiter(Task& task) noexcept
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

                child.m_continuation = awaiting;
                child.m_completionHandler = nullptr;

                bool expected = false;
                if (m_task.m_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    if (!detail::InheritChildExecutionContext(m_task.m_executor, child, awaiting.promise()))
                    {
                        child.SetFault(MakeAsyncFault(AsyncFaultCode::InvalidState));
                        child.MarkFinishedAndResume(m_task.m_handle);
                        return std::noop_coroutine();
                    }

                    m_task.m_executor.Execute(m_task.m_handle);
                }

                return std::noop_coroutine();
            }

            [[nodiscard]] TaskOutcome<void, E> await_resume() noexcept
            {
                return m_task.Get();
            }

        private:
            Task& m_task;
        };

        [[nodiscard]] OutcomeAwaiter AsOutcome() noexcept
        {
            return OutcomeAwaiter {*this};
        }

        struct ReturnErrorAwaiter final
        {
            E error {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetDomainError(error);
                return false;
            }
            void await_resume() const noexcept {}
        };

        struct ReturnCanceledAwaiter final
        {
            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetCanceled();
                return false;
            }
            void await_resume() const noexcept {}
        };

        struct ReturnFaultAwaiter final
        {
            AsyncFault fault {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                h.promise().SetFault(fault);
                return false;
            }
            void await_resume() const noexcept {}
        };

        [[nodiscard]] static ReturnErrorAwaiter ReturnError(E error) noexcept
        {
            return ReturnErrorAwaiter {std::move(error)};
        }

        [[nodiscard]] static ReturnCanceledAwaiter ReturnCanceled() noexcept
        {
            return {};
        }

        [[nodiscard]] static ReturnFaultAwaiter ReturnFault(AsyncFault fault) noexcept
        {
            return ReturnFaultAwaiter {fault};
        }

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
        template<typename ParentPromise>
        static void PropagateChildCompletion(std::coroutine_handle<> self, std::coroutine_handle<> continuation) noexcept
        {
            auto childHandle  = handle_type::from_address(self.address());
            auto parentHandle = std::coroutine_handle<ParentPromise>::from_address(continuation.address());
            auto& child       = childHandle.promise();

            if (parentHandle.promise().m_finished.load(std::memory_order_acquire))
            {
                return;
            }

            if (child.m_status == TaskStatus::Succeeded)
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
        std::optional<AsyncFault>    m_invalidFault {};
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
            using NextTask  = std::invoke_result_t<Func, T>;
            using NextValue = TaskValueType<NextTask>;

            parent->Schedule(*context);

            const auto parentIndex = co_await WhenAny(*context, *parent);
            if (context->IsCancellationRequested() || parentIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }

            auto parentOutcome = parent->Get();
            if (parentOutcome.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnError(parentOutcome.DomainError());
                    co_return;
                }
                else
                {
                    co_return NGIN::Utilities::Unexpected<E>(parentOutcome.DomainError());
                }
            }
            if (parentOutcome.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }
            if (parentOutcome.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnFault(parentOutcome.Fault());
                    co_return;
                }
                else
                {
                    co_return Fault(parentOutcome.Fault());
                }
            }

            auto next = std::invoke(std::move(func), std::move(parentOutcome.Value()));
            const auto nextIndex = co_await WhenAny(*context, next);
            if (context->IsCancellationRequested() || nextIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }

            auto nextOutcome = next.Get();
            if (nextOutcome.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnError(nextOutcome.DomainError());
                    co_return;
                }
                else
                {
                    co_return NGIN::Utilities::Unexpected<E>(nextOutcome.DomainError());
                }
            }
            if (nextOutcome.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }
            if (nextOutcome.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnFault(nextOutcome.Fault());
                    co_return;
                }
                else
                {
                    co_return Fault(nextOutcome.Fault());
                }
            }

            if constexpr (std::is_void_v<NextValue>)
            {
                co_return;
            }
            else
            {
                co_return std::move(nextOutcome.Value());
            }
        }

        template<typename E, typename Func>
        auto ContinueWithImpl(Task<void, E>* parent, TaskContext* context, Func func) -> std::invoke_result_t<Func>
        {
            using NextTask  = std::invoke_result_t<Func>;
            using NextValue = TaskValueType<NextTask>;

            parent->Schedule(*context);

            const auto parentIndex = co_await WhenAny(*context, *parent);
            if (context->IsCancellationRequested() || parentIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }

            auto parentOutcome = parent->Get();
            if (parentOutcome.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnError(parentOutcome.DomainError());
                    co_return;
                }
                else
                {
                    co_return NGIN::Utilities::Unexpected<E>(parentOutcome.DomainError());
                }
            }
            if (parentOutcome.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }
            if (parentOutcome.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnFault(parentOutcome.Fault());
                    co_return;
                }
                else
                {
                    co_return Fault(parentOutcome.Fault());
                }
            }

            auto next = std::invoke(std::move(func));
            const auto nextIndex = co_await WhenAny(*context, next);
            if (context->IsCancellationRequested() || nextIndex == static_cast<NGIN::UIntSize>(-1))
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }

            auto nextOutcome = next.Get();
            if (nextOutcome.IsDomainError())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnError(nextOutcome.DomainError());
                    co_return;
                }
                else
                {
                    co_return NGIN::Utilities::Unexpected<E>(nextOutcome.DomainError());
                }
            }
            if (nextOutcome.IsCanceled())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnCanceled();
                    co_return;
                }
                else
                {
                    co_return Canceled;
                }
            }
            if (nextOutcome.IsFault())
            {
                if constexpr (std::is_void_v<NextValue>)
                {
                    co_await NextTask::ReturnFault(nextOutcome.Fault());
                    co_return;
                }
                else
                {
                    co_return Fault(nextOutcome.Fault());
                }
            }

            if constexpr (std::is_void_v<NextValue>)
            {
                co_return;
            }
            else
            {
                co_return std::move(nextOutcome.Value());
            }
        }
    }// namespace detail

    template<typename T, typename E>
    template<typename F>
    auto Task<T, E>::ContinueWith(TaskContext& ctx, F&& func)
    {
        using Func     = std::decay_t<F>;
        using NextTask = std::invoke_result_t<Func, T>;
        static_assert(detail::IsTaskTypeV<NextTask>, "ContinueWith expects a function returning Task<...>.");
        static_assert(std::is_same_v<typename NextTask::ErrorType, E>, "ContinueWith requires identical error types in v1.");
        return detail::ContinueWithImpl<T, E, Func>(this, &ctx, Func(std::forward<F>(func)));
    }

    template<typename E>
    template<typename F>
    auto Task<void, E>::ContinueWith(TaskContext& ctx, F&& func)
    {
        using Func     = std::decay_t<F>;
        using NextTask = std::invoke_result_t<Func>;
        static_assert(detail::IsTaskTypeV<NextTask>, "ContinueWith expects a function returning Task<...>.");
        static_assert(std::is_same_v<typename NextTask::ErrorType, E>, "ContinueWith requires identical error types in v1.");
        return detail::ContinueWithImpl<E, Func>(this, &ctx, Func(std::forward<F>(func)));
    }
}// namespace NGIN::Async
