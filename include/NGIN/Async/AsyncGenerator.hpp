/// @file AsyncGenerator.hpp
/// @brief Cooperative async pull generator integrated with TaskContext scheduling and cancellation.
#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
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
    /// `while (auto v = co_await gen.Next(ctx)) { ... use *v ... }`
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
            std::exception_ptr             error {};
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

            YieldAwaiter yield_value(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
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
                error = std::current_exception();
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
                return p.error != nullptr || p.current.has_value() || p.completed;
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
            {
                if (m_ctx.IsCancellationRequested())
                {
                    return awaiting;
                }

                if (!m_gen.m_handle)
                {
                    return awaiting;
                }

                auto& p = m_gen.m_handle.promise();

                {
                    NGIN::Sync::LockGuard guard(p.lock);
                    if (p.error || p.current.has_value() || p.completed)
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

            std::optional<T> await_resume()
            {
                m_ctx.ThrowIfCancellationRequested();

                if (!m_gen.m_handle)
                {
                    return std::nullopt;
                }

                auto& p = m_gen.m_handle.promise();
                NGIN::Sync::LockGuard guard(p.lock);

                if (p.error)
                {
                    std::rethrow_exception(p.error);
                }

                if (p.current.has_value())
                {
                    auto out = std::move(p.current);
                    p.current.reset();
                    return out;
                }

                if (p.completed)
                {
                    return std::nullopt;
                }

                return std::nullopt;
            }

        private:
            AsyncGenerator&                   m_gen;
            TaskContext&                      m_ctx;
            mutable CancellationRegistration  m_cancellationRegistration {};
        };

        [[nodiscard]] auto Next(TaskContext& ctx) noexcept
        {
            return NextAwaiter(*this, ctx);
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

        handle_type m_handle {};
    };
}// namespace NGIN::Async
