/// <summary>
/// Core coroutine types: Task<T>/Task<void> and TaskContext.
/// </summary>
#pragma once

#include <coroutine>
#include <exception>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <atomic>
#include <cassert>
#include "IScheduler.hpp"

namespace NGIN::Async
{

    /// <summary>
    /// Execution context binding tasks to a specific IScheduler.
    /// </summary>
    class TaskContext
    {
    public:
        explicit TaskContext(IScheduler* scheduler = nullptr) noexcept
            : m_scheduler(scheduler) {}

        void Bind(IScheduler* scheduler) noexcept
        {
            m_scheduler = scheduler;
        }

        IScheduler* GetScheduler() const noexcept
        {
            return m_scheduler;
        }

        // Awaiter that yields control to the scheduler
        auto Yield() const noexcept
        {
            struct Awaiter
            {
                IScheduler* sched;
                bool        await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    sched->Schedule(h);
                }
                void await_resume() const noexcept {}
            };
            return Awaiter {m_scheduler};
        }

        // In TaskContext:
        auto Delay(std::chrono::milliseconds dur) const noexcept
        {
            struct DelayAwaiter
            {
                IScheduler*                           sched;
                std::chrono::milliseconds             dur;
                std::chrono::steady_clock::time_point until;

                DelayAwaiter(IScheduler* s, std::chrono::milliseconds d)
                    : sched(s), dur(d), until(std::chrono::steady_clock::now() + d) {}

                bool await_ready() const noexcept
                {
                    return dur.count() == 0;
                }
                void await_suspend(std::coroutine_handle<> handle) const
                {
                    sched->ScheduleDelay(handle, until);
                }
                void await_resume() const noexcept {}
            };
            return DelayAwaiter {m_scheduler, dur};
        }


        // Helper to start a task on this context and return it
        template<typename TaskT>
        TaskT Run(TaskT&& task)
        {
            task.Start(*this);
            return std::forward<TaskT>(task);
        }

        ///TODO: Make this use NGIN::Utilities::Callable to allow any callable
        template<typename Func, typename... Args>
        auto Run(Func&& func, Args&&... args)
                -> decltype(auto)
        {
            // Call the coroutine function with this context and all args
            auto task = func(*this, std::forward<Args>(args)...);
            task.Start(*this);
            return task;
        }

    private:
        IScheduler* m_scheduler {nullptr};
    };

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
            std::mutex              m_mutex;
            std::condition_variable m_cv;
            bool                    m_finished {false};
            std::coroutine_handle<> m_continuation {};

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
                    {
                        std::lock_guard lk(p.m_mutex);
                        p.m_finished = true;
                    }
                    p.m_cv.notify_all();
                    if (p.m_continuation)
                        p.m_continuation.resume();
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
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_scheduler(nullptr), m_started(false) {}

        Task(Task&& o) noexcept
            : m_handle(o.m_handle), m_scheduler(o.m_scheduler), m_started(o.m_started.load())
        {
            o.m_handle    = nullptr;
            o.m_scheduler = nullptr;
            o.m_started   = false;
        }
        Task& operator=(Task&& o) noexcept
        {
            if (this != &o)
            {
                if (m_handle)
                    m_handle.destroy();
                m_handle      = o.m_handle;
                m_scheduler   = o.m_scheduler;
                m_started     = o.m_started.load();
                o.m_handle    = nullptr;
                o.m_scheduler = nullptr;
                o.m_started   = false;
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
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                // If a scheduler is not yet set, this is a bug!
                assert(m_scheduler && "Task must have a scheduler before being awaited!");
                m_scheduler->Schedule(m_handle);
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
                m_scheduler = ctx.GetScheduler();
                m_scheduler->Schedule(m_handle);
            }
        }

        void Wait()
        {
            auto&            p = m_handle.promise();
            std::unique_lock lk(p.m_mutex);
            p.m_cv.wait(lk, [&p] { return p.m_finished; });
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
            std::lock_guard lk(p.m_mutex);
            return p.m_finished;
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            auto&           p = m_handle.promise();
            std::lock_guard lk(p.m_mutex);
            return !p.m_finished && m_handle && !m_handle.done();
        }

        [[nodiscard]] bool IsFaulted() const noexcept
        {
            auto&           p = m_handle.promise();
            std::lock_guard lk(p.m_mutex);
            return p.m_error != nullptr;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            // No cancellation support yet, always false
            return false;
        }

        handle_type Handle() const noexcept
        {
            return m_handle;
        }

        // --- Continuation support ---
        template<typename F>
        auto Then(F&& func)
        {
            using RetTask = decltype(func(std::declval<T>()));
            struct Awaiter
            {
                Task& parent;
                F     func;
                bool  await_ready() const noexcept { return parent.IsCompleted(); }
                auto  await_suspend(std::coroutine_handle<> h)
                {
                    struct Cont
                    {
                        std::coroutine_handle<> h;
                        Task*                   parent;
                        F                       func;
                        void                    operator()()
                        {
                            try
                            {
                                auto result   = parent->Get();
                                auto nextTask = func(std::move(result));
                                nextTask.Start(*parent->m_scheduler_ctx);
                                nextTask.Wait();
                            } catch (...)
                            {}
                            h.resume();
                        }
                    };
                    std::thread(Cont {h, &parent, std::move(func)}).detach();
                }
                auto await_resume() {}
            };
            struct ContTask
            {
                Task& parent;
                F     func;
                auto  operator co_await() { return Awaiter {parent, std::move(func)}; }
            };
            return ContTask {*this, std::forward<F>(func)};
        }

    private:
        handle_type      m_handle;
        IScheduler*      m_scheduler;
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
            std::mutex              m_mutex;
            std::condition_variable m_cv;
            bool                    m_finished {false};
            std::coroutine_handle<> m_continuation {};

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
                    {
                        std::lock_guard lk(p.m_mutex);
                        p.m_finished = true;
                    }
                    p.m_cv.notify_all();
                    if (p.m_continuation)
                        p.m_continuation.resume();
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
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) noexcept
            : m_handle(h), m_scheduler(nullptr), m_started(false) {}

        Task(Task&& o) noexcept
            : m_handle(o.m_handle), m_scheduler(o.m_scheduler), m_started(o.m_started.load())
        {
            o.m_handle    = nullptr;
            o.m_scheduler = nullptr;
            o.m_started   = false;
        }
        Task& operator=(Task&& o) noexcept
        {
            if (this != &o)
            {
                if (m_handle)
                    m_handle.destroy();
                m_handle      = o.m_handle;
                m_scheduler   = o.m_scheduler;
                m_started     = o.m_started.load();
                o.m_handle    = nullptr;
                o.m_scheduler = nullptr;
                o.m_started   = false;
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
            // Schedule only if not already started (atomic for thread safety)
            bool expected = false;
            if (m_started.compare_exchange_strong(expected, true))
            {
                assert(m_scheduler && "Task must have a scheduler before being awaited!");
                m_scheduler->Schedule(m_handle);
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
                m_scheduler = ctx.GetScheduler();
                m_scheduler->Schedule(m_handle);
            }
        }

        void Wait()
        {
            auto&            p = m_handle.promise();
            std::unique_lock lk(p.m_mutex);
            p.m_cv.wait(lk, [&p] { return p.m_finished; });
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
            std::lock_guard lk(p.m_mutex);
            return p.m_finished;
        }

        bool IsRunning() const noexcept
        {
            auto&           p = m_handle.promise();
            std::lock_guard lk(p.m_mutex);
            return !p.m_finished && m_handle && !m_handle.done();
        }

        bool IsFaulted() const noexcept
        {
            auto&           p = m_handle.promise();
            std::lock_guard lk(p.m_mutex);
            return p.m_error != nullptr;
        }

        bool IsCanceled() const noexcept
        {
            // No cancellation support yet, always false
            return false;
        }

        handle_type Handle() const noexcept
        {
            return m_handle;
        }

        // --- Continuation support ---
        template<typename F>
        auto Then(F&& func)
        {
            using RetTask = decltype(func());
            struct Awaiter
            {
                Task& parent;
                F     func;
                bool  await_ready() const noexcept { return parent.IsCompleted(); }
                auto  await_suspend(std::coroutine_handle<> h)
                {
                    struct Cont
                    {
                        std::coroutine_handle<> h;
                        Task*                   parent;
                        F                       func;
                        void                    operator()()
                        {
                            try
                            {
                                parent->Get();
                                auto nextTask = func();
                                nextTask.Start(*parent->m_scheduler_ctx);
                                nextTask.Wait();
                            } catch (...)
                            {}
                            h.resume();
                        }
                    };
                    std::thread(Cont {h, &parent, std::move(func)}).detach();
                }
                auto await_resume() {}
            };
            struct ContTask
            {
                Task& parent;
                F     func;
                auto  operator co_await() { return Awaiter {parent, std::move(func)}; }
            };
            return ContTask {*this, std::forward<F>(func)};
        }

    private:
        handle_type      m_handle;
        IScheduler*      m_scheduler;
        std::atomic_bool m_started;

        // For continuation support
        friend class TaskContext;
        TaskContext* m_scheduler_ctx {nullptr};

    public:
        /// Static delay: returns a Task<void> that completes after duration.
        static Task<void> Delay(TaskContext& ctx, std::chrono::milliseconds duration)
        {
            struct Awaiter
            {
                IScheduler*               sched;
                std::chrono::milliseconds dur;
                bool                      await_ready() const noexcept
                {
                    return dur.count() == 0;
                }
                void await_suspend(std::coroutine_handle<> h) const
                {
                    std::thread([sched = sched, h, d = dur]() mutable {
                        std::this_thread::sleep_for(d);
                        sched->Schedule(h);
                    }).detach();
                }
                void await_resume() const noexcept {}
            };
            co_await Awaiter {ctx.GetScheduler(), duration};
        }
    };

}// namespace NGIN::Async
