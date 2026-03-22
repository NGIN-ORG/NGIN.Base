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
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/SpinLock.hpp>

namespace NGIN::Async
{
    template<typename T>
    class GeneratorNext final
    {
    public:
        [[nodiscard]] static GeneratorNext Item(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
        {
            return GeneratorNext(std::move(value));
        }

        [[nodiscard]] static GeneratorNext End() noexcept
        {
            return GeneratorNext();
        }

        [[nodiscard]] bool HasItem() const noexcept
        {
            return m_value.has_value();
        }

        [[nodiscard]] bool IsEnd() const noexcept
        {
            return !m_value.has_value();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return HasItem();
        }

        [[nodiscard]] T& Value() noexcept
        {
            assert(m_value.has_value());
            return *m_value;
        }

        [[nodiscard]] const T& Value() const noexcept
        {
            assert(m_value.has_value());
            return *m_value;
        }

        [[nodiscard]] T& operator*() noexcept
        {
            return Value();
        }

        [[nodiscard]] const T& operator*() const noexcept
        {
            return Value();
        }

        [[nodiscard]] T* operator->() noexcept
        {
            assert(m_value.has_value());
            return &*m_value;
        }

        [[nodiscard]] const T* operator->() const noexcept
        {
            assert(m_value.has_value());
            return &*m_value;
        }

    private:
        GeneratorNext() = default;

        explicit GeneratorNext(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            : m_value(std::move(value))
        {
        }

        std::optional<T> m_value {};
    };

    /// @brief Async pull generator that yields values via `co_yield` and advances via `co_await gen.Next(ctx)`.
    template<typename T, typename E = NoError>
    class AsyncGenerator final
    {
    public:
        struct promise_type final
        {
            NGIN::Sync::SpinLock         lock {};
            NGIN::Execution::ExecutorRef exec {};
            std::coroutine_handle<>      consumer {};
            std::optional<T>             current {};
            std::optional<E>             domainError {};
            std::optional<AsyncFault>    fault {};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            std::exception_ptr           exception {};
#endif
            bool completed {false};
            bool canceled {false};

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
                    return promise->WakeConsumer();
                }

                void await_resume() noexcept {}
            };

            struct FinalAwaiter final
            {
                bool await_ready() noexcept
                {
                    return false;
                }

                void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
                {
                    auto& promise = handle.promise();
                    {
                        NGIN::Sync::LockGuard guard(promise.lock);
                        promise.completed = true;
                    }
                    auto consumer = promise.WakeConsumer();
                    if (consumer)
                    {
                        consumer.resume();
                    }
                }

                void await_resume() noexcept {}
            };

            YieldAwaiter yield_value(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            {
                {
                    NGIN::Sync::LockGuard guard(lock);
                    current = std::move(value);
                }
                return YieldAwaiter {this};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                exception = std::current_exception();
#endif
#else
                std::terminate();
#endif
            }

            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void SetCanceled() noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                canceled = true;
            }

            void SetFault(AsyncFault asyncFault) noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                fault = std::move(asyncFault);
            }

            void MarkFinishedAndResume(std::coroutine_handle<promise_type>) noexcept
            {
                {
                    NGIN::Sync::LockGuard guard(lock);
                    completed = true;
                }

                auto consumer = WakeConsumer();
                if (consumer)
                {
                    consumer.resume();
                }
            }

            std::coroutine_handle<> WakeConsumer() noexcept
            {
                std::coroutine_handle<> toResume {};
                NGIN::Execution::ExecutorRef executor {};
                {
                    NGIN::Sync::LockGuard guard(lock);
                    toResume = consumer;
                    consumer = {};
                    executor = exec;
                }

                if (toResume && executor.IsValid())
                {
                    executor.Execute(toResume);
                    return std::noop_coroutine();
                }

                return toResume;
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

        struct AdvanceAwaiter final
        {
            AsyncGenerator&                 generator;
            TaskContext&                    context;
            CancellationRegistration        cancellationRegistration {};

            bool await_ready() const noexcept
            {
                if (context.IsCancellationRequested())
                {
                    return true;
                }

                if (!generator.m_handle)
                {
                    return true;
                }

                auto& promise = generator.m_handle.promise();
                NGIN::Sync::LockGuard guard(promise.lock);
                return promise.current.has_value() || promise.domainError.has_value() || promise.fault.has_value() ||
                       promise.canceled || promise.completed;
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
            {
                if (context.IsCancellationRequested())
                {
                    return awaiting;
                }

                if (!generator.m_handle)
                {
                    return awaiting;
                }

                auto& promise = generator.m_handle.promise();

                {
                    NGIN::Sync::LockGuard guard(promise.lock);
                    if (promise.current.has_value() || promise.domainError.has_value() || promise.fault.has_value() ||
                        promise.canceled || promise.completed)
                    {
                        return awaiting;
                    }

                    assert(!promise.consumer && "AsyncGenerator::Next does not support concurrent consumers");
                    promise.consumer = awaiting;

                    if (!promise.exec.IsValid())
                    {
                        promise.exec = context.GetExecutor();
                    }

                    if (!promise.exec.IsValid())
                    {
                        promise.fault = MakeAsyncFault(AsyncFaultCode::InvalidState);
                        promise.completed = true;
                        promise.consumer = {};
                        return awaiting;
                    }
                }

                context.GetCancellationToken().Register(
                        cancellationRegistration,
                        {},
                        {},
                        +[](void* rawPromise) noexcept -> bool {
                            auto* promise = static_cast<promise_type*>(rawPromise);
                            if (!promise)
                            {
                                return false;
                            }

                            std::coroutine_handle<> toResume {};
                            NGIN::Execution::ExecutorRef executor {};
                            {
                                NGIN::Sync::LockGuard guard(promise->lock);
                                toResume = promise->consumer;
                                promise->consumer = {};
                                executor = promise->exec;
                            }

                            if (toResume)
                            {
                                if (executor.IsValid())
                                {
                                    executor.Execute(toResume);
                                }
                                else
                                {
                                    toResume.resume();
                                }
                            }
                            return false;
                        },
                        &promise);

                return generator.m_handle;
            }

            void await_resume() const noexcept {}
        };

        [[nodiscard]] Task<GeneratorNext<T>, E> Next(TaskContext& ctx)
        {
            co_await AdvanceAwaiter {*this, ctx};

            if (ctx.IsCancellationRequested())
            {
                co_return Canceled;
            }

            if (!m_handle)
            {
                co_return GeneratorNext<T>::End();
            }

            auto& promise = m_handle.promise();
            NGIN::Sync::LockGuard guard(promise.lock);

            if (promise.fault.has_value())
            {
                co_return Fault(*promise.fault);
            }

            if (promise.canceled)
            {
                co_return Canceled;
            }

            if (promise.domainError.has_value())
            {
                co_return NGIN::Utilities::Unexpected(*promise.domainError);
            }

            if (promise.current.has_value())
            {
                auto value = std::move(*promise.current);
                promise.current.reset();
                co_return GeneratorNext<T>::Item(std::move(value));
            }

            co_return GeneratorNext<T>::End();
        }

        struct ReturnErrorAwaiter final
        {
            E error {};

            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
            {
                auto& promise = handle.promise();
                {
                    NGIN::Sync::LockGuard guard(promise.lock);
                    promise.domainError = error;
                }
                return false;
            }

            void await_resume() const noexcept {}
        };

        struct ReturnCanceledAwaiter final
        {
            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
            {
                handle.promise().SetCanceled();
                return false;
            }

            void await_resume() const noexcept {}
        };

        struct ReturnFaultAwaiter final
        {
            AsyncFault fault {};

            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
            {
                handle.promise().SetFault(fault);
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
            return ReturnFaultAwaiter {std::move(fault)};
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
