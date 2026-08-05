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
    /// @brief Result of advancing an async generator: one item or end-of-sequence.
    template<typename T>
    class GeneratorNext final
    {
    public:
        /// @brief Creates a result containing a yielded item.
        [[nodiscard]] static GeneratorNext Item(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
        {
            return GeneratorNext(std::move(value));
        }

        /// @brief Creates an end-of-sequence result.
        [[nodiscard]] static GeneratorNext End() noexcept
        {
            return GeneratorNext();
        }

        /// @brief Returns whether a yielded item is present.
        [[nodiscard]] bool HasItem() const noexcept
        {
            return m_value.has_value();
        }

        /// @brief Returns whether the generator reached end-of-sequence.
        [[nodiscard]] bool IsEnd() const noexcept
        {
            return !m_value.has_value();
        }

        /// @brief Returns whether a yielded item is present.
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return HasItem();
        }

        /// @brief Returns the yielded item.
        /// @pre `HasItem()` is `true`.
        [[nodiscard]] T& Value() noexcept
        {
            assert(m_value.has_value());
            return *m_value;
        }

        /// @brief Returns the yielded item.
        /// @pre `HasItem()` is `true`.
        [[nodiscard]] const T& Value() const noexcept
        {
            assert(m_value.has_value());
            return *m_value;
        }

        /// @brief Returns the yielded item.
        [[nodiscard]] T& operator*() noexcept
        {
            return Value();
        }

        /// @brief Returns the yielded item.
        [[nodiscard]] const T& operator*() const noexcept
        {
            return Value();
        }

        /// @brief Returns a pointer to the yielded item.
        /// @pre `HasItem()` is `true`.
        [[nodiscard]] T* operator->() noexcept
        {
            assert(m_value.has_value());
            return &*m_value;
        }

        /// @brief Returns an immutable pointer to the yielded item.
        /// @pre `HasItem()` is `true`.
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
            std::exception_ptr exception {};
#endif
            bool completed {false};
            bool canceled {false};

            /// @brief Constructs a promise without a bound executor.
            promise_type() = default;

            /// @brief Constructs a promise using a task context's executor.
            explicit promise_type(TaskContext& ctx) noexcept
                : exec(ctx.GetExecutor())
            {
            }

            /// @brief Constructs a promise from a leading task context and ignores coroutine arguments.
            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit promise_type(TaskContext& ctx, Args&&...) noexcept
                : promise_type(ctx)
            {
            }

            /// @brief Returns the async generator owning this coroutine frame.
            AsyncGenerator get_return_object() noexcept
            {
                return AsyncGenerator(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            /// @brief Suspends before production so consumers control advancement.
            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct YieldAwaiter final
            {
                promise_type* promise {nullptr};

                /// @brief Always suspends after publishing a yielded item.
                bool await_ready() noexcept
                {
                    return false;
                }

                /// @brief Wakes the waiting consumer and transfers control appropriately.
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type>) noexcept
                {
                    return promise->WakeConsumer();
                }

                /// @brief Performs no producer resume-time work.
                void await_resume() noexcept {}
            };

            struct FinalAwaiter final
            {
                /// @brief Always enters final suspension so the generator owns frame destruction.
                bool await_ready() noexcept
                {
                    return false;
                }

                /// @brief Marks completion and wakes the waiting consumer.
                void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
                {
                    promise_type& promise = handle.promise();
                    {
                        NGIN::Sync::LockGuard guard(promise.lock);
                        promise.completed = true;
                    }
                    std::coroutine_handle<> consumer = promise.WakeConsumer();
                    if (consumer)
                    {
                        consumer.resume();
                    }
                }

                /// @brief Performs no final resume-time work.
                void await_resume() noexcept {}
            };

            /// @brief Publishes a yielded value and suspends until the next consumer advance.
            YieldAwaiter yield_value(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            {
                {
                    NGIN::Sync::LockGuard guard(lock);
                    current = std::move(value);
                }
                return YieldAwaiter {this};
            }

            /// @brief Completes a generator that reaches `co_return`.
            void return_void() noexcept {}

            /// @brief Converts an escaping exception to an async fault, or terminates when exceptions are disabled.
            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                NGIN::Sync::LockGuard guard(lock);
                fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                exception = std::current_exception();
#endif
#else
                std::terminate();
#endif
            }

            /// @brief Returns the awaiter that marks completion and wakes a consumer.
            FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            /// @brief Marks the generator outcome as canceled.
            void SetCanceled() noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                canceled = true;
            }

            /// @brief Stores a recoverable domain error.
            void SetDomainError(E error) noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                domainError = std::move(error);
            }

            /// @brief Stores an unexpected asynchronous fault.
            void SetFault(AsyncFault asyncFault) noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                fault = std::move(asyncFault);
            }

            /// @brief Marks completion and wakes the registered consumer.
            void MarkFinishedAndResume(std::coroutine_handle<promise_type>) noexcept
            {
                {
                    NGIN::Sync::LockGuard guard(lock);
                    completed = true;
                }

                std::coroutine_handle<> consumer = WakeConsumer();
                if (consumer)
                {
                    consumer.resume();
                }
            }

            /// @brief Removes and schedules the registered consumer continuation.
            /// @return Direct continuation, or `std::noop_coroutine()` when scheduled through an executor.
            std::coroutine_handle<> WakeConsumer() noexcept
            {
                std::coroutine_handle<>      toResume {};
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

        /// @brief Coroutine handle type owned by the generator.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty generator.
        AsyncGenerator() noexcept = default;

        /// @brief Takes ownership of an async-generator coroutine handle.
        explicit AsyncGenerator(handle_type handle) noexcept
            : m_handle(handle)
        {
        }

        /// @brief Transfers ownership of a coroutine frame.
        AsyncGenerator(AsyncGenerator&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.m_handle = {};
        }

        /// @brief Destroys the current frame and transfers ownership of another frame.
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

        /// @brief Async generators are non-copyable because they uniquely own a coroutine frame.
        AsyncGenerator(const AsyncGenerator&) = delete;
        /// @brief Async generators are non-copy-assignable because they uniquely own a coroutine frame.
        AsyncGenerator& operator=(const AsyncGenerator&) = delete;

        /// @brief Destroys the owned coroutine frame.
        ~AsyncGenerator()
        {
            Reset();
        }

        /// @brief Awaiter that coordinates one producer advance with one consumer continuation.
        struct AdvanceAwaiter final
        {
            AsyncGenerator&          generator;
            TaskContext&             context;
            CancellationRegistration cancellationRegistration {};

            /// @brief Returns whether an outcome is already available without resuming the producer.
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

                promise_type&         promise = generator.m_handle.promise();
                NGIN::Sync::LockGuard guard(promise.lock);
                return promise.current.has_value() || promise.domainError.has_value() || promise.fault.has_value() ||
                       promise.canceled || promise.completed;
            }

            /// @brief Registers the consumer and transfers execution to the producer coroutine.
            /// @details Concurrent consumers fault the generator with `InvalidContinuationState`.
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

                promise_type& promise = generator.m_handle.promise();

                std::coroutine_handle<>      concurrentConsumer {};
                NGIN::Execution::ExecutorRef concurrentExecutor {};
                {
                    NGIN::Sync::LockGuard guard(promise.lock);
                    if (promise.current.has_value() || promise.domainError.has_value() || promise.fault.has_value() ||
                        promise.canceled || promise.completed)
                    {
                        return awaiting;
                    }

                    if (promise.consumer)
                    {
                        promise.fault      = MakeAsyncFault(AsyncFaultCode::InvalidContinuationState);
                        promise.completed  = true;
                        concurrentConsumer = promise.consumer;
                        concurrentExecutor = promise.exec;
                        promise.consumer   = {};
                    }
                    else
                    {
                        promise.consumer = awaiting;
                    }

                    if (!promise.exec.IsValid())
                    {
                        promise.exec = context.GetExecutor();
                    }

                    if (!promise.exec.IsValid())
                    {
                        promise.fault     = MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage);
                        promise.completed = true;
                        promise.consumer  = {};
                        return awaiting;
                    }
                }

                if (concurrentConsumer)
                {
                    if (concurrentExecutor.IsValid())
                    {
                        concurrentExecutor.Execute(concurrentConsumer);
                    }
                    else
                    {
                        concurrentConsumer.resume();
                    }
                    return awaiting;
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

                            std::coroutine_handle<>      toResume {};
                            NGIN::Execution::ExecutorRef executor {};
                            {
                                NGIN::Sync::LockGuard guard(promise->lock);
                                toResume          = promise->consumer;
                                promise->consumer = {};
                                executor          = promise->exec;
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

            /// @brief Performs no consumer resume-time work.
            void await_resume() const noexcept {}
        };

        /// @brief Advances the generator and asynchronously returns its next item or end marker.
        /// @details Domain errors, cancellation, and faults are propagated through the returned task.
        [[nodiscard]] Task<GeneratorNext<T>, E> Next(TaskContext& ctx)
        {
            using NextCompletion = Completion<GeneratorNext<T>, E>;

            co_await AdvanceAwaiter {*this, ctx};

            if (ctx.IsCancellationRequested())
            {
                co_return NextCompletion::Canceled();
            }

            if (!m_handle)
            {
                co_return GeneratorNext<T>::End();
            }

            promise_type&         promise = m_handle.promise();
            NGIN::Sync::LockGuard guard(promise.lock);

            if (promise.fault.has_value())
            {
                co_return NextCompletion::Faulted(*promise.fault);
            }

            if (promise.canceled)
            {
                co_return NextCompletion::Canceled();
            }

            if (promise.domainError.has_value())
            {
                co_return NextCompletion::DomainFailure(*promise.domainError);
            }

            if (promise.current.has_value())
            {
                T value = std::move(*promise.current);
                promise.current.reset();
                co_return GeneratorNext<T>::Item(std::move(value));
            }

            co_return GeneratorNext<T>::End();
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
