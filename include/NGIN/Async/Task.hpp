/// <summary>
/// Core coroutine types: Task<T>/Task<void> and TaskContext.
/// </summary>
#pragma once

#include <coroutine>
#include <exception>
#include <utility>
#include <atomic>
#include <cassert>
#include <type_traits>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        [[nodiscard]] inline bool IsTaskCanceled(const std::exception_ptr& error) noexcept
        {
            if (!error)
            {
                return false;
            }
            try
            {
                std::rethrow_exception(error);
            } catch (const TaskCanceled&)
            {
                return true;
            } catch (...)
            {
                return false;
            }
        }
    }// namespace detail

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
            T                       m_value;
            std::exception_ptr      m_error;
            bool                    m_canceled {false};
            std::atomic<bool>       m_finished {false};
            NGIN::Sync::AtomicCondition m_finishedCondition {};
            std::coroutine_handle<> m_continuation {};
            NGIN::Execution::ExecutorRef m_executor {};

            promise_type() = default;

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
            void unhandled_exception() noexcept
            {
                m_error = std::current_exception();
                m_canceled = detail::IsTaskCanceled(m_error);
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(), m_started(false) {}

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

        void await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            auto& prom          = m_handle.promise();
            prom.m_continuation = awaiting;
            prom.m_executor     = m_executor;
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                // If a scheduler is not yet set, this is a bug!
                assert(m_executor.IsValid() && "Task must have an executor before being awaited!");
                m_executor.Schedule(m_handle);
            }
            // else: already scheduled, just attach continuation
        }

        T await_resume()
        {
            auto& p = m_handle.promise();
            if (p.m_error)
                std::rethrow_exception(p.m_error);
            return std::move(p.m_value);
        }

        /// Schedule this task on a given context's scheduler.
        void Start(TaskContext& ctx) noexcept
        {
            if (!m_started.exchange(true))
            {
                m_executor      = ctx.GetExecutor();
                m_handle.promise().m_executor = m_executor;
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

        T Get()
        {
            Wait();
            auto& p = m_handle.promise();
            if (p.m_error)
                std::rethrow_exception(p.m_error);
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
            return p.m_error != nullptr;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            if (!m_handle)
            {
                return false;
            }
            return m_handle.promise().m_canceled;
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
                std::exception_ptr              error {};
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

                                state->error = std::make_exception_ptr(TaskCanceled());
                                return true;
                            },
                            state.get());

                    auto chain = [](Task& parent,
                                    TaskContext& ctx,
                                    std::shared_ptr<SharedState> state,
                                    Func func) -> Detached {
                        std::exception_ptr error {};
                        try
                        {
                            ctx.ThrowIfCancellationRequested();
                            parent.Start(ctx);
                            auto value = co_await parent;
                            ctx.ThrowIfCancellationRequested();

                            auto nextTask = func(std::move(value));
                            nextTask.Start(ctx);
                            co_await nextTask;
                        } catch (...)
                        {
                            error = std::current_exception();
                        }

                        bool expected = false;
                        if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        {
                            state->error = std::move(error);
                            if (state->awaiting)
                            {
                                state->exec.Execute(state->awaiting);
                            }
                        }
                        co_return;
                    }(parent, *ctx, state, std::move(func));

                    return std::coroutine_handle<>::from_address(chain.handle.address());
                }

                void await_resume()
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    if (ctx && ctx->IsCancellationRequested() && (!state || !state->error))
                    {
                        throw TaskCanceled();
                    }
                    if (state && state->error)
                    {
                        std::rethrow_exception(state->error);
                    }
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
            std::exception_ptr      m_error;
            bool                    m_canceled {false};
            std::atomic<bool>       m_finished {false};
            NGIN::Sync::AtomicCondition m_finishedCondition {};
            std::coroutine_handle<> m_continuation {};
            NGIN::Execution::ExecutorRef m_executor {};

            promise_type() = default;

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
                m_error = std::current_exception();
                m_canceled = detail::IsTaskCanceled(m_error);
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_executor(), m_started(false) {}

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

        void await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            auto& prom          = m_handle.promise();
            prom.m_continuation = awaiting;
            prom.m_executor     = m_executor;
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                assert(m_executor.IsValid() && "Task must have an executor before being awaited!");
                m_executor.Schedule(m_handle);
            }
        }

        void await_resume()
        {
            if (m_handle.promise().m_error)
                std::rethrow_exception(m_handle.promise().m_error);
        }

        void Start(TaskContext& ctx) noexcept
        {
            if (!m_started.exchange(true))
            {
                m_executor      = ctx.GetExecutor();
                m_handle.promise().m_executor = m_executor;
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

        void Get()
        {
            Wait();
            auto& p = m_handle.promise();
            if (p.m_error)
                std::rethrow_exception(p.m_error);
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
            return p.m_error != nullptr;
        }

        bool IsCanceled() const noexcept
        {
            if (!m_handle)
            {
                return false;
            }
            return m_handle.promise().m_canceled;
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
                std::exception_ptr              error {};
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

                                state->error = std::make_exception_ptr(TaskCanceled());
                                return true;
                            },
                            state.get());

                    auto chain = [](Task& parent,
                                    TaskContext& ctx,
                                    std::shared_ptr<SharedState> state,
                                    Func func) -> Detached {
                        std::exception_ptr error {};
                        try
                        {
                            ctx.ThrowIfCancellationRequested();
                            parent.Start(ctx);
                            co_await parent;
                            ctx.ThrowIfCancellationRequested();

                            auto nextTask = func();
                            nextTask.Start(ctx);
                            co_await nextTask;
                        } catch (...)
                        {
                            error = std::current_exception();
                        }

                        bool expected = false;
                        if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        {
                            state->error = std::move(error);
                            if (state->awaiting)
                            {
                                state->exec.Execute(state->awaiting);
                            }
                        }
                        co_return;
                    }(parent, *ctx, state, std::move(func));

                    return std::coroutine_handle<>::from_address(chain.handle.address());
                }

                void await_resume()
                {
                    auto* ctx = parent.m_scheduler_ctx;
                    if (ctx && ctx->IsCancellationRequested() && (!state || !state->error))
                    {
                        throw TaskCanceled();
                    }
                    if (state && state->error)
                    {
                        std::rethrow_exception(state->error);
                    }
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
            co_await ctx.Delay(duration);
        }
    };

}// namespace NGIN::Async
