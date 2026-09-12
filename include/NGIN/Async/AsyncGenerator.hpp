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
        /// @brief Producer promise using the shared task frame and continuation machinery.
        struct promise_type final : detail::PromiseRuntimeCommon
        {
            NGIN::Sync::SpinLock          lock {};
            std::coroutine_handle<>       consumer {};
            detail::PromiseRuntimeCommon* consumerTarget {};
            void (*resumeConsumer)(std::coroutine_handle<>) noexcept {};
            std::optional<T>          current {};
            std::optional<E>          domainError {};
            std::optional<AsyncFault> fault {};
            bool                      completed {false};
            bool                      canceled {false};
            bool                      consumerActive {false};

            promise_type() = default;
            explicit promise_type(TaskContext& ctx) noexcept : detail::PromiseRuntimeCommon(ctx) {}
            template<typename... Args>
                requires(sizeof...(Args) > 0)
            explicit promise_type(TaskContext& ctx, Args&&...) noexcept : promise_type(ctx)
            {}

            AsyncGenerator get_return_object() noexcept
            {
                return AsyncGenerator(std::coroutine_handle<promise_type>::from_promise(*this));
            }
            std::suspend_always initial_suspend() noexcept { return {}; }

            struct YieldAwaiter final
            {
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<promise_type> self) const noexcept
                {
                    self.promise().EndAdvance(self, false);
                }
                void await_resume() const noexcept {}
            };

            struct FinalAwaiter final
            {
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<promise_type> self) const noexcept
                {
                    self.promise().EndAdvance(self, true);
                }
                void await_resume() const noexcept {}
            };

            YieldAwaiter yield_value(T value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            {
                NGIN::Sync::LockGuard guard(lock);
                current = std::move(value);
                return {};
            }
            void return_void() noexcept {}
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
            FinalAwaiter final_suspend() noexcept { return {}; }

            void SetCanceled() noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                canceled = true;
            }
            void SetDomainError(E error) noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                domainError = std::move(error);
            }
            void SetFault(AsyncFault asyncFault) noexcept
            {
                NGIN::Sync::LockGuard guard(lock);
                fault = std::move(asyncFault);
            }
            void MarkFinishedAndResume(std::coroutine_handle<promise_type> self) noexcept
            {
                EndAdvance(self, true);
            }

            // A consumer can leave only at a producer yield or terminal boundary.
            // End all producer-frame access before publishing that boundary.
            void EndAdvance(std::coroutine_handle<promise_type> self, bool terminal) noexcept
            {
                std::coroutine_handle<>       awaiting;
                detail::PromiseRuntimeCommon* target;
                void (*resume)(std::coroutine_handle<>) noexcept;
                {
                    NGIN::Sync::LockGuard guard(lock);
                    completed = terminal;
                    m_finished.store(terminal, std::memory_order_release);
                    awaiting = std::exchange(consumer, {});
                    target   = std::exchange(consumerTarget, nullptr);
                    resume   = std::exchange(resumeConsumer, nullptr);
                }
                assert(awaiting && target && resume);
                m_taskContinuation.Reset();
                const bool active = m_executionReferenceActive.exchange(false, std::memory_order_acq_rel);
                assert(active);
                (void) active;
                ReleaseFrameReference(self);
                target->QueueContinuation(NGIN::Execution::WorkItem([awaiting, resume] { resume(awaiting); }));
            }
        };

        /// @brief Native producer coroutine handle.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty generator.
        AsyncGenerator() noexcept = default;
        /// @brief Takes ownership of a newly created producer frame.
        explicit AsyncGenerator(handle_type handle) noexcept : m_handle(handle) {}
        /// @brief Transfers the generator's ownership without invalidating existing advances.
        AsyncGenerator(AsyncGenerator&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
        AsyncGenerator& operator=(AsyncGenerator&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }
        AsyncGenerator(const AsyncGenerator&)            = delete;
        AsyncGenerator& operator=(const AsyncGenerator&) = delete;
        /// @brief Releases ownership; an existing Next keeps its producer alive through completion.
        ~AsyncGenerator() { Reset(); }

        /// @brief Advances the generator on its executor and returns the next item or end marker.
        /// @details Snapshots frame ownership before returning the cold task. Only one Next may
        /// advance or consume a result at a time. Cancellation waits for producer quiescence at
        /// its next yield or terminal boundary; uncancelable producer work must finish naturally.
        [[nodiscard]] Task<GeneratorNext<T>, E> Next(TaskContext& ctx)
        {
            return NextImpl(ctx, FrameOwner(m_handle));
        }

    private:
        class FrameOwner final
        {
        public:
            explicit FrameOwner(handle_type handle) noexcept : m_handle(handle)
            {
                if (m_handle)
                    m_handle.promise().RetainFrameReference();
            }
            FrameOwner(FrameOwner&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
            FrameOwner(const FrameOwner&)            = delete;
            FrameOwner& operator=(const FrameOwner&) = delete;
            ~FrameOwner()
            {
                if (m_handle)
                    m_handle.promise().ReleaseFrameReference(m_handle);
            }
            handle_type Get() const noexcept { return m_handle; }

        private:
            handle_type m_handle;
        };

        struct AdvanceAwaiter final
        {
            handle_type               producer;
            TaskContext&              context;
            std::optional<AsyncFault> fault;
            bool                      claimed {false};

            ~AdvanceAwaiter()
            {
                if (claimed)
                {
                    NGIN::Sync::LockGuard guard(producer.promise().lock);
                    producer.promise().consumerActive = false;
                }
            }
            bool await_ready() const noexcept { return false; }

            template<typename ParentPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept
            {
                static_assert(std::derived_from<ParentPromise, detail::PromiseRuntimeCommon>);
                if (context.IsCancellationRequested() || !producer)
                    return awaiting;
                promise_type& promise = producer.promise();
                {
                    NGIN::Sync::LockGuard guard(promise.lock);
                    if (promise.consumerActive)
                    {
                        fault = MakeAsyncFault(AsyncFaultCode::InvalidContinuationState);
                        return awaiting;
                    }
                    promise.consumerActive = true;
                    claimed                = true;
                    if (promise.current || promise.domainError || promise.fault || promise.canceled || promise.completed)
                        return awaiting;
                }
                if (!awaiting.promise().m_taskContinuation.IsValid())
                {
                    fault = detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                        NGIN::Execution::ScheduleError::Rejected);
                    return awaiting;
                }
                if (!promise.m_executor.IsValid())
                    promise.m_executor = context.GetExecutor();
                if (!promise.m_executor.IsValid())
                {
                    fault = MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage);
                    return awaiting;
                }
                auto admission = promise.ReserveExecution();
                if (!admission || !promise.m_taskContinuation.IsValid())
                {
                    fault = detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                        admission ? NGIN::Execution::ScheduleError::Rejected : admission.error());
                    return awaiting;
                }
                promise.AcquireExecutionReference();
                awaiting.promise().RetainFrameReference();
                {
                    NGIN::Sync::LockGuard guard(promise.lock);
                    promise.consumer       = awaiting;
                    promise.consumerTarget = &awaiting.promise();
                    promise.resumeConsumer = +[](std::coroutine_handle<> raw) noexcept {
                        detail::PromiseRuntimeCommon::ResumeRetained(
                                std::coroutine_handle<ParentPromise>::from_address(raw.address()));
                    };
                }
                // Submission may run concurrently and retire this awaiter.
                // The shared submission protocol owns rejection and discard delivery.
                (void) promise.SubmitTracked(producer);
                return std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };

        static Task<GeneratorNext<T>, E> NextImpl(TaskContext& ctx, FrameOwner owner)
        {
            using NextCompletion = Completion<GeneratorNext<T>, E>;
            AdvanceAwaiter advance {owner.Get(), ctx, {}};
            co_await advance;
            if (advance.fault)
                co_return NextCompletion::Faulted(*advance.fault);
            if (ctx.IsCancellationRequested())
                co_return NextCompletion::Canceled();
            if (!owner.Get())
                co_return GeneratorNext<T>::End();

            promise_type&         promise = owner.Get().promise();
            NGIN::Sync::LockGuard guard(promise.lock);
            if (promise.fault)
                co_return NextCompletion::Faulted(*promise.fault);
            if (promise.canceled)
                co_return NextCompletion::Canceled();
            if (promise.domainError)
                co_return NextCompletion::DomainFailure(*promise.domainError);
            if (promise.current)
            {
                T value = std::move(*promise.current);
                promise.current.reset();
                co_return GeneratorNext<T>::Item(std::move(value));
            }
            co_return GeneratorNext<T>::End();
        }

        void Reset() noexcept
        {
            if (handle_type handle = std::exchange(m_handle, {}))
                handle.promise().ReleaseFrameReference(handle);
        }
        handle_type m_handle {};
    };
}// namespace NGIN::Async
