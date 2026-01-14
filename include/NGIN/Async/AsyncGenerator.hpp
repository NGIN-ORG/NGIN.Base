/// @file AsyncGenerator.hpp
/// @brief Cooperative async pull generator integrated with TaskContext scheduling and cancellation.
#pragma once

#include <cassert>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/SpinLock.hpp>

namespace NGIN::Async
{
    /// @brief Async pull generator that yields values via `co_yield` and is advanced via `co_await gen.Next(ctx)`.
    ///
    /// This is intentionally separate from `Task<T>` (single-result). `AsyncGenerator<T>` yields a sequence of values
    /// over time, resuming on the provided `TaskContext` executor and observing cancellation cooperatively.
    ///
    /// Usage:
    /// `for (;;) { auto next = co_await gen.Next(ctx); if (!next) { ... } if (!*next) break; ... **next ... }`
    /// To report a fault without exceptions, `co_await AsyncGenerator<T>::ReturnError(MakeAsyncError(...)); co_return;`.
    template<typename T>
    class AsyncGenerator final
    {
    public:
        struct promise_type final
        {
            NGIN::Sync::SpinLock           lock {};
            NGIN::Execution::ExecutorRef   exec {};
            std::coroutine_handle<>        consumer {};
            std::optional<T>               current {};
            AsyncError                     error {};
            bool                           hasError {false};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr             exception {};
#endif
            bool                           completed {false};

            promise_type() = default;

            explicit promise_type(TaskContext& ctx) noexcept
                : exec(ctx.GetExecutor())
            {
            }

            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit promise_type(TaskContext& ctx, Args&&...) noexcept
                : promise_type(ctx)
            {
            }

            AsyncGenerator get_return_object() noexcept
            {
                return AsyncGenerator(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct YieldAwaiter final
            {
                promise_type* promise {nullptr};

                bool await_ready() noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type>) noexcept
                {
                    std::coroutine_handle<> toResume {};
                    NGIN::Execution::ExecutorRef exec {};
                    {
                        NGIN::Sync::LockGuard guard(promise->lock);
                        toResume = promise->consumer;
                        promise->consumer = {};
                        exec = promise->exec;
                    }

                    if (toResume)
                    {
                        if (exec.IsValid())
                        {
                            exec.Execute(toResume);
                        }
                        else
                        {
                            toResume.resume();
                        }
                    }

                    return std::noop_coroutine();
                }

                void await_resume() noexcept {}
            };

            struct FinalAwaiter
            {
                bool await_ready() noexcept
                {
                    return false;
                }

                void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    auto& p = h.promise();

                    std::coroutine_handle<> toResume {};
                    NGIN::Execution::ExecutorRef exec {};
                    {
                        NGIN::Sync::LockGuard guard(p.lock);
                        p.completed = true;
                        toResume    = p.consumer;
                        p.consumer  = {};
                        exec        = p.exec;
                    }

                    if (toResume)
                    {
                        if (exec.IsValid())
                        {
                            exec.Execute(toResume);
                        }
                        else
                        {
                            toResume.resume();
                        }
                    }
                }

                void await_resume() noexcept {}
            };

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            YieldAwaiter yield_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
            {
                {
                    NGIN::Sync::LockGuard guard(lock);
                    current  = std::move(value);
                }
                return YieldAwaiter {this};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                error = AsyncError {AsyncErrorCode::Fault, 0};
                hasError = true;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                exception = std::current_exception();
#endif
#else
                std::terminate();
#endif
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        AsyncGenerator() noexcept = default;

        explicit AsyncGenerator(handle_type handle) noexcept
            : m_handle(handle)
        {
        }

        AsyncGenerator(AsyncGenerator&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.m_handle = {};
        }

        AsyncGenerator& operator=(AsyncGenerator&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_handle       = other.m_handle;
                other.m_handle = {};
            }
            return *this;
        }

        AsyncGenerator(const AsyncGenerator&)            = delete;
        AsyncGenerator& operator=(const AsyncGenerator&) = delete;

        ~AsyncGenerator()
        {
            Reset();
        }

        class NextAwaiter final
        {
        public:
            NextAwaiter(AsyncGenerator& gen, TaskContext& ctx) noexcept
                : m_gen(gen)
                , m_ctx(ctx)
            {
            }

            bool await_ready() const noexcept
            {
                if (m_ctx.IsCancellationRequested())
                {
                    return true;
                }

                if (!m_gen.m_handle)
                {
                    return true;
                }

                auto& p = m_gen.m_handle.promise();
                NGIN::Sync::LockGuard guard(p.lock);
                return p.current.has_value() || p.completed;
            }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                if (m_ctx.IsCancellationRequested())
                {
                    return awaiting;
                }

                if (!m_gen.m_handle)
                {
                    return awaiting;
                }

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                m_awaiting = awaiting;
                m_propagateException = &AsyncGenerator::template PropagateChildException<Promise>;
#endif

                auto& p = m_gen.m_handle.promise();

                {
                    NGIN::Sync::LockGuard guard(p.lock);
                    if (p.hasError || p.current.has_value() || p.completed)
                    {
                        return awaiting;
                    }

                    assert(!p.consumer && "AsyncGenerator::Next does not support concurrent consumers");
                    p.consumer = awaiting;

                    if (!p.exec.IsValid())
                    {
                        p.exec = m_ctx.GetExecutor();
                    }

                    if (!p.exec.IsValid())
                    {
                        return awaiting;
                    }
                }

                m_ctx.GetCancellationToken().Register(
                        m_cancellationRegistration,
                        {},
                        {},
                        +[](void* ctx) noexcept -> bool {
                            auto* promise = static_cast<promise_type*>(ctx);
                            if (!promise)
                            {
                                return false;
                            }

                            std::coroutine_handle<> toResume {};
                            NGIN::Execution::ExecutorRef exec {};
                            {
                                NGIN::Sync::LockGuard guard(promise->lock);
                                toResume = promise->consumer;
                                promise->consumer = {};
                                exec = promise->exec;
                            }

                            if (toResume)
                            {
                                if (exec.IsValid())
                                {
                                    exec.Execute(toResume);
                                }
                                else
                                {
                                    toResume.resume();
                                }
                            }
                            return false;
                        },
                        &p);

                return m_gen.m_handle;
            }

            AsyncExpected<std::optional<T>> await_resume() noexcept
            {
                if (m_ctx.IsCancellationRequested())
                {
                    return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                }

                if (!m_gen.m_handle)
                {
                    return std::optional<T> {};
                }

                auto& p = m_gen.m_handle.promise();
                NGIN::Sync::LockGuard guard(p.lock);

#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                if (p.hasError && p.exception && m_propagateException && m_awaiting)
                {
                    m_propagateException(p.exception, m_awaiting);
                }
#endif

                if (p.hasError)
                {
                    return std::unexpected(p.error);
                }

                if (p.current.has_value())
                {
                    auto out = std::move(p.current);
                    p.current.reset();
                    return out;
                }

                if (p.completed)
                {
                    return std::optional<T> {};
                }

                return std::optional<T> {};
            }

        private:
            AsyncGenerator&                   m_gen;
            TaskContext&                      m_ctx;
            mutable CancellationRegistration  m_cancellationRegistration {};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::coroutine_handle<>           m_awaiting {};
            using ExceptionPropagator = void (*)(std::exception_ptr, std::coroutine_handle<>) noexcept;
            ExceptionPropagator               m_propagateException {};
#endif
        };

        [[nodiscard]] auto Next(TaskContext& ctx) noexcept
        {
            return NextAwaiter(*this, ctx);
        }

        struct ReturnErrorAwaiter final
        {
            AsyncError error {};

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                auto& p = h.promise();
                p.error = error;
                p.hasError = true;
                return false;
            }
            void await_resume() const noexcept {}
        };

        [[nodiscard]] static ReturnErrorAwaiter ReturnError(AsyncError error) noexcept
        {
            return ReturnErrorAwaiter {error};
        }

    private:
        void Reset() noexcept
        {
            if (m_handle)
            {
                m_handle.destroy();
                m_handle = {};
            }
        }

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

        handle_type m_handle {};
    };
}// namespace NGIN::Async
