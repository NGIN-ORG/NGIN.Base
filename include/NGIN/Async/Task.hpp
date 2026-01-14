/// <summary>
/// Core coroutine types: Task<T>/Task<void> and TaskContext.
/// </summary>
#pragma once

#include <coroutine>
#include <utility>
#include <atomic>
#include <cassert>
#include <type_traits>

#include <exception>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class BaseTask
    {
    };

    //------------------------------------------------------------------------
    // Task<T>
    //------------------------------------------------------------------------
    template<typename T = void>
    class Task : public BaseTask
    {
    public:
        struct promise_type
        {
            T                       m_value {};
            AsyncError              m_error {};
            bool                    m_hasError {false};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr      m_exception {};
            using ExceptionPropagator = void (*)(std::exception_ptr, std::coroutine_handle<>) noexcept;
            ExceptionPropagator      m_setChildException {};

            void SetChildException(std::exception_ptr ex) noexcept
            {
                if (!m_exception)
                {
                    m_exception = ex;
                }
            }
#endif
            std::atomic<bool>       m_finished {false};
            NGIN::Sync::AtomicCondition m_finishedCondition {};
            std::coroutine_handle<> m_continuation {};
            TaskContext*            m_ctx {nullptr};
            NGIN::Execution::ExecutorRef m_executor {};

            promise_type() = default;

            explicit promise_type(TaskContext& ctx) noexcept
                : m_ctx(&ctx)
                , m_executor(ctx.GetExecutor())
            {
            }

            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit promise_type(TaskContext& ctx, Args&&...) noexcept
                : promise_type(ctx)
            {
            }

            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter
            {
                bool await_ready() noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    auto& p = h.promise();
                    p.m_finished.store(true, std::memory_order_release);
                    p.m_finishedCondition.NotifyAll();
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    if (p.m_continuation && p.m_setChildException && p.m_exception)
                    {
                        p.m_setChildException(p.m_exception, p.m_continuation);
                    }
#endif
                    if (p.m_continuation)
                    {
                        if (p.m_executor.IsValid())
                            p.m_executor.Execute(p.m_continuation);
                        else
                            p.m_continuation.resume();
                    }
                }
                void await_resume() noexcept {}
            };
            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_value(T value) noexcept
            {
                m_value = std::move(value);
            }
            void return_value(AsyncExpected<T> result) noexcept
            {
                if (!result)
                {
                    m_error = result.error();
                    m_hasError = true;
                    return;
                }
                m_value = std::move(*result);
            }

            void return_value(std::unexpected<AsyncError> error) noexcept
            {
                m_error = error.error();
                m_hasError = true;
            }

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                m_error = AsyncError {AsyncErrorCode::Fault, 0};
                m_hasError = true;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_exception = std::current_exception();
#endif
#else
                std::terminate();
#endif
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h)
            , m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
            , m_started(false)
            , m_scheduler_ctx(h ? h.promise().m_ctx : nullptr)
        {
        }

        Task(Task&& o) noexcept
            : m_handle(o.m_handle), m_executor(o.m_executor), m_started(o.m_started.load()), m_scheduler_ctx(o.m_scheduler_ctx)
        {
            o.m_handle    = nullptr;
            o.m_executor  = {};
            o.m_started   = false;
            o.m_scheduler_ctx = nullptr;
        }
        Task& operator=(Task&& o) noexcept
        {
            if (this != &o)
            {
                if (m_handle)
                    m_handle.destroy();
                m_handle      = o.m_handle;
                m_executor    = o.m_executor;
                m_started     = o.m_started.load();
                o.m_handle    = nullptr;
                o.m_executor  = {};
                o.m_started   = false;
                m_scheduler_ctx = o.m_scheduler_ctx;
                o.m_scheduler_ctx = nullptr;
            }
            return *this;
        }
        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            if (m_handle)
                m_handle.destroy();
        }

        bool await_ready() const noexcept
        {
            return !m_handle || m_handle.done();
        }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
        {
            auto& prom          = m_handle.promise();
            prom.m_continuation = awaiting;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            prom.m_setChildException = &Task::template PropagateChildException<Promise>;
#endif
            if (m_executor.IsValid())
            {
                prom.m_executor = m_executor;
            }
            else
            {
                m_executor = prom.m_executor;
            }
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                // If a scheduler is not yet set, this is a programmer error; fail fast even in Release.
                assert(m_executor.IsValid() && "Task must have an executor before being awaited!");
                if (!m_executor.IsValid())
                {
                    std::terminate();
                }
                prom.m_executor = m_executor;
                m_executor.Schedule(m_handle);
            }
            // else: already scheduled, just attach continuation
        }

        AsyncExpected<T> await_resume() noexcept
        {
            auto& p = m_handle.promise();
            if (p.m_hasError)
            {
                return std::unexpected(p.m_error);
            }
            return std::move(p.m_value);
        }

        /// Schedule this task on a given context's scheduler.
        void Start(TaskContext& ctx) noexcept
        {
            if (!m_started.exchange(true))
            {
                m_executor      = ctx.GetExecutor();
                m_handle.promise().m_executor = m_executor;
                m_handle.promise().m_ctx      = &ctx;
                m_scheduler_ctx = &ctx;
                m_executor.Schedule(m_handle);
            }
        }

        void Wait()
        {
            auto&            p = m_handle.promise();
            while (!p.m_finished.load(std::memory_order_acquire))
            {
                const auto gen = p.m_finishedCondition.Load();
                if (p.m_finished.load(std::memory_order_acquire))
                {
                    break;
                }
                p.m_finishedCondition.Wait(gen);
            }
        }

        AsyncExpected<T> Get()
        {
            Wait();
            auto& p = m_handle.promise();
            if (p.m_hasError)
            {
                return std::unexpected(p.m_error);
            }
            return std::move(p.m_value);
        }

        [[nodiscard]] bool IsCompleted() const noexcept
        {
            auto&           p = m_handle.promise();
            return p.m_finished.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            auto&           p = m_handle.promise();
            return !p.m_finished.load(std::memory_order_acquire) && m_handle && !m_handle.done();
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            auto&           p = m_handle.promise();
            return p.m_hasError;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            if (!m_handle)
            {
                return false;
            }
            auto& p = m_handle.promise();
            return p.m_hasError && p.m_error.code == AsyncErrorCode::Canceled;
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

        struct ReturnErrorAwaiter final
        {
            AsyncError error {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                auto& p = h.promise();
                p.m_error = error;
                p.m_hasError = true;
                return false;
            }
            void await_resume() const noexcept {}
        };

        [[nodiscard]] static ReturnErrorAwaiter ReturnError(AsyncError error) noexcept
        {
            return ReturnErrorAwaiter {error};
        }

        handle_type Handle() const noexcept
        {
            return m_handle;
        }

        // --- Continuation support ---
        template<typename F>
        auto Then(F&& func)
        {
            using Func = std::decay_t<F>;

            struct SharedState final
            {
                std::atomic<bool>               done {false};
                NGIN::Execution::ExecutorRef    exec {};
                std::coroutine_handle<>         awaiting {};
                CancellationRegistration        cancellationRegistration {};
                AsyncError                      error {};
                bool                            hasError {false};
            };

            struct Awaiter
            {
                Task&                      parent;
                Func                       func;
                std::shared_ptr<SharedState> state {};

                bool await_ready() const noexcept
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    return ctx && ctx->IsCancellationRequested();
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting)
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    assert(ctx != nullptr && "Task::Then requires the parent task to have been started with a TaskContext");

                    struct Detached
                    {
                        struct promise_type
                        {
                            Detached get_return_object() noexcept
                            {
                                return Detached {std::coroutine_handle<promise_type>::from_promise(*this)};
                            }
                            std::suspend_always initial_suspend() noexcept { return {}; }
                            struct Final
                            {
                                bool await_ready() noexcept { return false; }
                                void await_suspend(std::coroutine_handle<promise_type> h) noexcept { h.destroy(); }
                                void await_resume() noexcept {}
                            };
                            Final final_suspend() noexcept { return {}; }
                            void return_void() noexcept {}
                            void unhandled_exception() noexcept {}
                        };

                        using handle_type = std::coroutine_handle<promise_type>;
                        handle_type handle {};

                        explicit Detached(handle_type h) noexcept
                            : handle(h)
                        {
                        }

                        Detached(Detached&& other) noexcept
                            : handle(other.handle)
                        {
                            other.handle = nullptr;
                        }

                        Detached(const Detached&)            = delete;
                        Detached& operator=(const Detached&) = delete;
                        Detached& operator=(Detached&&)      = delete;

                        ~Detached() = default;
                    };

                    if (ctx->IsCancellationRequested())
                    {
                        return awaiting;
                    }

                    state          = std::make_shared<SharedState>();
                    state->exec    = ctx->GetExecutor();
                    state->awaiting = awaiting;
                    ctx->GetCancellationToken().Register(
                            state->cancellationRegistration,
                            state->exec,
                            awaiting,
                            +[](void* ctx) noexcept -> bool {
                                auto* state = static_cast<SharedState*>(ctx);
                                bool  expected = false;
                                if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                                {
                                    return false;
                                }

                                state->error = MakeAsyncError(AsyncErrorCode::Canceled);
                                state->hasError = true;
                                return true;
                            },
                            state.get());

                    auto chain = [](Task& parent,
                                    TaskContext& ctx,
                                    std::shared_ptr<SharedState> state,
                                    Func func) -> Detached {
                        AsyncError error {};
                        bool       hasError = false;

                        if (ctx.IsCancellationRequested())
                        {
                            error = MakeAsyncError(AsyncErrorCode::Canceled);
                            hasError = true;
                        }
                        else
                        {
                            parent.Start(ctx);
                            auto parentResult = co_await parent;
                            if (!parentResult)
                            {
                                error = parentResult.error();
                                hasError = true;
                            }
                            else if (ctx.IsCancellationRequested())
                            {
                                error = MakeAsyncError(AsyncErrorCode::Canceled);
                                hasError = true;
                            }
                            else
                            {
                                auto nextTask = func(std::move(*parentResult));
                                nextTask.Start(ctx);
                                auto nextResult = co_await nextTask;
                                if (!nextResult)
                                {
                                    error = nextResult.error();
                                    hasError = true;
                                }
                            }
                        }

                        bool expected = false;
                        if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        {
                            state->error = error;
                            state->hasError = hasError;
                            if (state->awaiting)
                            {
                                state->exec.Execute(state->awaiting);
                            }
                        }
                        co_return;
                    }(parent, *ctx, state, std::move(func));

                    return std::coroutine_handle<>::from_address(chain.handle.address());
                }

                AsyncExpected<void> await_resume() noexcept
                {
                    if (state && state->hasError)
                    {
                        return std::unexpected(state->error);
                    }
                    auto* ctx = parent.m_scheduler_ctx;
                    if (ctx && ctx->IsCancellationRequested())
                    {
                        return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                    }
                    return {};
                }
            };
            struct ContTask
            {
                Task& parent;
                Func  func;
                auto  operator co_await() { return Awaiter {parent, std::move(func)}; }
            };
            return ContTask {*this, Func(std::forward<F>(func))};
        }

    private:
        handle_type      m_handle;
        NGIN::Execution::ExecutorRef m_executor;
        std::atomic_bool m_started;

        // For continuation support
        friend class TaskContext;
        TaskContext* m_scheduler_ctx {nullptr};

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        template<typename Promise>
        static void PropagateChildException(std::exception_ptr ex, std::coroutine_handle<> cont) noexcept
        {
            if (!ex || !cont)
            {
                return;
            }
            if constexpr (requires(Promise& p) { p.SetChildException(ex); })
            {
                auto typed = std::coroutine_handle<Promise>::from_address(cont.address());
                typed.promise().SetChildException(ex);
            }
            else
            {
                (void)cont;
            }
        }
#endif
    };

    //------------------------------------------------------------------------
    // Task<void>
    //------------------------------------------------------------------------
    template<>
    class Task<void> : public BaseTask
    {
    public:
        struct promise_type
        {
            AsyncError              m_error {};
            bool                    m_hasError {false};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr      m_exception {};
            using ExceptionPropagator = void (*)(std::exception_ptr, std::coroutine_handle<>) noexcept;
            ExceptionPropagator      m_setChildException {};

            void SetChildException(std::exception_ptr ex) noexcept
            {
                if (!m_exception)
                {
                    m_exception = ex;
                }
            }
#endif

            std::atomic<bool>       m_finished {false};
            NGIN::Sync::AtomicCondition m_finishedCondition {};
            std::coroutine_handle<> m_continuation {};
            TaskContext*            m_ctx {nullptr};
            NGIN::Execution::ExecutorRef m_executor {};

            promise_type() = default;

            explicit promise_type(TaskContext& ctx) noexcept
                : m_ctx(&ctx)
                , m_executor(ctx.GetExecutor())
            {
            }

            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit promise_type(TaskContext& ctx, Args&&...) noexcept
                : promise_type(ctx)
            {
            }

            Task get_return_object() noexcept
            {
                return Task {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaiter
            {
                bool await_ready() noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    auto& p = h.promise();
                    p.m_finished.store(true, std::memory_order_release);
                    p.m_finishedCondition.NotifyAll();
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    if (p.m_continuation && p.m_setChildException && p.m_exception)
                    {
                        p.m_setChildException(p.m_exception, p.m_continuation);
                    }
#endif
                    if (p.m_continuation)
                    {
                        if (p.m_executor.IsValid())
                            p.m_executor.Execute(p.m_continuation);
                        else
                            p.m_continuation.resume();
                    }
                }
                void await_resume() noexcept {}
            };
            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                m_error = AsyncError {AsyncErrorCode::Fault, 0};
                m_hasError = true;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_exception = std::current_exception();
#endif
#else
                std::terminate();
#endif
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h)
            , m_executor(h ? h.promise().m_executor : NGIN::Execution::ExecutorRef {})
            , m_started(false)
            , m_scheduler_ctx(h ? h.promise().m_ctx : nullptr)
        {
        }

        Task(Task&& o) noexcept
            : m_handle(o.m_handle), m_executor(o.m_executor), m_started(o.m_started.load()), m_scheduler_ctx(o.m_scheduler_ctx)
        {
            o.m_handle    = nullptr;
            o.m_executor  = {};
            o.m_started   = false;
            o.m_scheduler_ctx = nullptr;
        }
        Task& operator=(Task&& o) noexcept
        {
            if (this != &o)
            {
                if (m_handle)
                    m_handle.destroy();
                m_handle      = o.m_handle;
                m_executor    = o.m_executor;
                m_started     = o.m_started.load();
                o.m_handle    = nullptr;
                o.m_executor  = {};
                o.m_started   = false;
                m_scheduler_ctx = o.m_scheduler_ctx;
                o.m_scheduler_ctx = nullptr;
            }
            return *this;
        }
        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        ~Task()
        {
            if (m_handle)
                m_handle.destroy();
        }

        bool await_ready() const noexcept
        {
            return !m_handle || m_handle.done();
        }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
        {
            auto& prom          = m_handle.promise();
            prom.m_continuation = awaiting;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            prom.m_setChildException = &Task::template PropagateChildException<Promise>;
#endif
            if (m_executor.IsValid())
            {
                prom.m_executor = m_executor;
            }
            else
            {
                m_executor = prom.m_executor;
            }
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                assert(m_executor.IsValid() && "Task must have an executor before being awaited!");
                if (!m_executor.IsValid())
                {
                    std::terminate();
                }
                prom.m_executor = m_executor;
                m_executor.Schedule(m_handle);
            }
        }

        AsyncExpected<void> await_resume() noexcept
        {
            auto& p = m_handle.promise();
            if (p.m_hasError)
            {
                return std::unexpected(p.m_error);
            }
            return {};
        }

        void Start(TaskContext& ctx) noexcept
        {
            if (!m_started.exchange(true))
            {
                m_executor      = ctx.GetExecutor();
                m_handle.promise().m_executor = m_executor;
                m_handle.promise().m_ctx      = &ctx;
                m_scheduler_ctx = &ctx;
                m_executor.Schedule(m_handle);
            }
        }

        void Wait()
        {
            auto&            p = m_handle.promise();
            while (!p.m_finished.load(std::memory_order_acquire))
            {
                const auto gen = p.m_finishedCondition.Load();
                if (p.m_finished.load(std::memory_order_acquire))
                {
                    break;
                }
                p.m_finishedCondition.Wait(gen);
            }
        }

        AsyncExpected<void> Get()
        {
            Wait();
            auto& p = m_handle.promise();
            if (p.m_hasError)
            {
                return std::unexpected(p.m_error);
            }
            return {};
        }

        bool IsCompleted() const noexcept
        {
            auto&           p = m_handle.promise();
            return p.m_finished.load(std::memory_order_acquire);
        }

        bool IsRunning() const noexcept
        {
            auto&           p = m_handle.promise();
            return !p.m_finished.load(std::memory_order_acquire) && m_handle && !m_handle.done();
        }

        bool IsFaulted() const noexcept
        {
            auto&           p = m_handle.promise();
            return p.m_hasError;
        }

        bool IsCanceled() const noexcept
        {
            if (!m_handle)
            {
                return false;
            }
            auto& p = m_handle.promise();
            return p.m_hasError && p.m_error.code == AsyncErrorCode::Canceled;
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

        struct ReturnErrorAwaiter final
        {
            AsyncError error {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                auto& p = h.promise();
                p.m_error = error;
                p.m_hasError = true;
                return false;
            }
            void await_resume() const noexcept {}
        };

        [[nodiscard]] static ReturnErrorAwaiter ReturnError(AsyncError error) noexcept
        {
            return ReturnErrorAwaiter {error};
        }


        handle_type Handle() const noexcept
        {
            return m_handle;
        }

        // --- Continuation support ---
        template<typename F>
        auto Then(F&& func)
        {
            using Func = std::decay_t<F>;

            struct SharedState final
            {
                std::atomic<bool>               done {false};
                NGIN::Execution::ExecutorRef    exec {};
                std::coroutine_handle<>         awaiting {};
                CancellationRegistration        cancellationRegistration {};
                AsyncError                      error {};
                bool                            hasError {false};
            };

            struct Awaiter
            {
                Task&                      parent;
                Func                       func;
                std::shared_ptr<SharedState> state {};

                bool await_ready() const noexcept
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    return ctx && ctx->IsCancellationRequested();
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting)
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    assert(ctx != nullptr && "Task::Then requires the parent task to have been started with a TaskContext");

                    struct Detached
                    {
                        struct promise_type
                        {
                            Detached get_return_object() noexcept
                            {
                                return Detached {std::coroutine_handle<promise_type>::from_promise(*this)};
                            }
                            std::suspend_always initial_suspend() noexcept { return {}; }
                            struct Final
                            {
                                bool await_ready() noexcept { return false; }
                                void await_suspend(std::coroutine_handle<promise_type> h) noexcept { h.destroy(); }
                                void await_resume() noexcept {}
                            };
                            Final final_suspend() noexcept { return {}; }
                            void return_void() noexcept {}
                            void unhandled_exception() noexcept {}
                        };

                        using handle_type = std::coroutine_handle<promise_type>;
                        handle_type handle {};

                        explicit Detached(handle_type h) noexcept
                            : handle(h)
                        {
                        }

                        Detached(Detached&& other) noexcept
                            : handle(other.handle)
                        {
                            other.handle = nullptr;
                        }

                        Detached(const Detached&)            = delete;
                        Detached& operator=(const Detached&) = delete;
                        Detached& operator=(Detached&&)      = delete;

                        ~Detached() = default;
                    };

                    if (ctx->IsCancellationRequested())
                    {
                        return awaiting;
                    }

                    state          = std::make_shared<SharedState>();
                    state->exec    = ctx->GetExecutor();
                    state->awaiting = awaiting;
                    ctx->GetCancellationToken().Register(
                            state->cancellationRegistration,
                            state->exec,
                            awaiting,
                            +[](void* ctx) noexcept -> bool {
                                auto* state = static_cast<SharedState*>(ctx);
                                bool  expected = false;
                                if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                                {
                                    return false;
                                }

                                state->error = MakeAsyncError(AsyncErrorCode::Canceled);
                                state->hasError = true;
                                return true;
                            },
                            state.get());

                    auto chain = [](Task& parent,
                                    TaskContext& ctx,
                                    std::shared_ptr<SharedState> state,
                                    Func func) -> Detached {
                        AsyncError error {};
                        bool       hasError = false;

                        if (ctx.IsCancellationRequested())
                        {
                            error = MakeAsyncError(AsyncErrorCode::Canceled);
                            hasError = true;
                        }
                        else
                        {
                            parent.Start(ctx);
                            auto parentResult = co_await parent;
                            if (!parentResult)
                            {
                                error = parentResult.error();
                                hasError = true;
                            }
                            else if (ctx.IsCancellationRequested())
                            {
                                error = MakeAsyncError(AsyncErrorCode::Canceled);
                                hasError = true;
                            }
                            else
                            {
                                auto nextTask = func();
                                nextTask.Start(ctx);
                                auto nextResult = co_await nextTask;
                                if (!nextResult)
                                {
                                    error = nextResult.error();
                                    hasError = true;
                                }
                            }
                        }

                        bool expected = false;
                        if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        {
                            state->error = error;
                            state->hasError = hasError;
                            if (state->awaiting)
                            {
                                state->exec.Execute(state->awaiting);
                            }
                        }
                        co_return;
                    }(parent, *ctx, state, std::move(func));

                    return std::coroutine_handle<>::from_address(chain.handle.address());
                }

                AsyncExpected<void> await_resume() noexcept
                {
                    if (state && state->hasError)
                    {
                        return std::unexpected(state->error);
                    }
                    auto* ctx = parent.m_scheduler_ctx;
                    if (ctx && ctx->IsCancellationRequested())
                    {
                        return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                    }
                    return {};
                }
            };
            struct ContTask
            {
                Task& parent;
                Func  func;
                auto  operator co_await() { return Awaiter {parent, std::move(func)}; }
            };
            return ContTask {*this, Func(std::forward<F>(func))};
        }

    private:
        handle_type      m_handle;
        NGIN::Execution::ExecutorRef m_executor;
        std::atomic_bool m_started;

        // For continuation support
        friend class TaskContext;
        TaskContext* m_scheduler_ctx {nullptr};

    public:
        /// Static delay: returns a Task<void> that completes after duration.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        static Task<void> Delay(TaskContext& ctx, const TUnit& duration)
        {
            auto result = co_await ctx.Delay(duration);
            if (!result)
            {
                co_await ReturnError(result.error());
                co_return;
            }
            co_return;
        }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
    private:
        template<typename Promise>
        static void PropagateChildException(std::exception_ptr ex, std::coroutine_handle<> cont) noexcept
        {
            if (!ex || !cont)
            {
                return;
            }
            if constexpr (requires(Promise& p) { p.SetChildException(ex); })
            {
                auto typed = std::coroutine_handle<Promise>::from_address(cont.address());
                typed.promise().SetChildException(ex);
            }
            else
            {
                (void)cont;
            }
        }
#endif
    };

}// namespace NGIN::Async
