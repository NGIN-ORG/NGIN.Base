/// @file WhenAll.hpp
/// @brief Task combinator that completes when all tasks complete.
#pragma once

#include <atomic>
#include <coroutine>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    namespace detail::when_all
    {
        template<typename E>
        struct SharedState final
        {
            using IsFinishedFn = bool (*)(std::coroutine_handle<>) noexcept;
            using DetachFn     = void (*)(std::coroutine_handle<>, std::coroutine_handle<>) noexcept;

            std::atomic<bool>                    done {false};
            std::atomic<NGIN::UIntSize>          remaining {0};
            NGIN::Execution::ExecutorRef         exec {};
            std::coroutine_handle<>              awaiting {};
            CancellationRegistration             cancellationRegistration {};
            std::vector<std::coroutine_handle<>> watchers {};
            std::vector<std::coroutine_handle<>> childHandles {};
            std::vector<IsFinishedFn>            childFinished {};
            std::vector<DetachFn>                childDetach {};

            TaskStatus                status {TaskStatus::Pending};
            std::optional<E>          domainError {};
            std::optional<AsyncFault> fault {};
        };

        template<typename TTask>
        [[nodiscard]] inline bool IsTaskFinished(std::coroutine_handle<> handle) noexcept
        {
            if (!handle)
            {
                return true;
            }

            using HandleType = typename std::remove_reference_t<TTask>::handle_type;
            auto typed       = HandleType::from_address(handle.address());
            return typed.promise().m_finished.load(std::memory_order_acquire);
        }

        template<typename TTask>
        inline void DetachTaskContinuation(std::coroutine_handle<> childHandle, std::coroutine_handle<> watcher) noexcept
        {
            if (!childHandle || !watcher)
            {
                return;
            }

            using HandleType = typename std::remove_reference_t<TTask>::handle_type;
            auto  typed      = HandleType::from_address(childHandle.address());
            auto& promise    = typed.promise();
            if (!promise.m_finished.load(std::memory_order_acquire) &&
                promise.m_continuation.address() == watcher.address())
            {
                promise.m_continuation      = {};
                promise.m_completionHandler = nullptr;
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                promise.m_setChildException = nullptr;
#endif
            }
        }

        template<typename E>
        [[nodiscard]] inline bool IsChildFinished(const SharedState<E>& state, NGIN::UIntSize index) noexcept
        {
            return index < state.childHandles.size() && state.childFinished[index] &&
                   state.childFinished[index](state.childHandles[index]);
        }

        template<typename E>
        inline void ResumeWatcherIfSuspended(const SharedState<E>& state, NGIN::UIntSize index) noexcept
        {
            if (index >= state.watchers.size() || !state.watchers[index])
            {
                return;
            }

            if (index < state.childHandles.size() && index < state.childDetach.size() && state.childDetach[index])
            {
                state.childDetach[index](state.childHandles[index], state.watchers[index]);
            }

            if (!IsChildFinished(state, index))
            {
                state.exec.Execute(state.watchers[index]);
            }
        }

        template<typename E>
        inline void ResumeAwaiting(const SharedState<E>& state) noexcept
        {
            if (state.awaiting)
            {
                state.exec.Execute(state.awaiting);
            }
        }

        template<typename E>
        inline void ResumeOtherWatchers(const SharedState<E>& state, NGIN::UIntSize currentIndex) noexcept
        {
            for (NGIN::UIntSize index = 0; index < state.watchers.size(); ++index)
            {
                if (index != currentIndex)
                {
                    ResumeWatcherIfSuspended(state, index);
                }
            }
        }

        template<typename E>
        inline void ResumeAllWatchers(const SharedState<E>& state) noexcept
        {
            for (NGIN::UIntSize index = 0; index < state.watchers.size(); ++index)
            {
                ResumeWatcherIfSuspended(state, index);
            }
        }

        template<typename E>
        [[nodiscard]] inline bool CancelWhenAll(void* context) noexcept
        {
            auto* state    = static_cast<SharedState<E>*>(context);
            bool  expected = false;
            if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            state->status = TaskStatus::Canceled;
            ResumeAllWatchers(*state);
            return true;
        }

        struct Detached final
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
                void  return_void() noexcept {}
                void  unhandled_exception() noexcept {}
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

        template<typename E, typename TTask>
        [[nodiscard]] inline Detached WatchTask(std::weak_ptr<SharedState<E>> weakState, TTask& task, NGIN::UIntSize index)
        {
            auto outcome = co_await NGIN::Async::detail::AwaitTaskHandleCompletion(task);

            if (auto state = weakState.lock())
            {
                if (index < state->watchers.size())
                {
                    state->watchers[index] = {};
                }

                if (state->done.load(std::memory_order_acquire))
                {
                    co_return;
                }

                if (!outcome.Succeeded())
                {
                    bool expected = false;
                    if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        if (outcome.IsDomainError())
                        {
                            state->status      = TaskStatus::DomainError;
                            state->domainError = outcome.DomainError();
                        }
                        else if (outcome.IsCanceled())
                        {
                            state->status = TaskStatus::Canceled;
                        }
                        else if (outcome.IsFault())
                        {
                            state->status = TaskStatus::Fault;
                            state->fault  = outcome.Fault();
                        }

                        ResumeOtherWatchers(*state, index);
                        ResumeAwaiting(*state);
                    }
                    co_return;
                }

                const auto left = state->remaining.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (left == 0)
                {
                    bool expected = false;
                    if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        state->status = TaskStatus::Succeeded;
                        ResumeAwaiting(*state);
                    }
                }
            }

            co_return;
        }

        template<typename E, typename... TTasks>
        struct Awaiter final
        {
            TaskContext&                    ctx;
            std::shared_ptr<SharedState<E>> state;
            std::tuple<TTasks&...>          tasks;

            bool await_ready() const noexcept
            {
                return ctx.IsCancellationRequested() ||
                       std::apply([](auto&... task) { return (task.IsCompleted() && ...); }, tasks);
            }

            bool await_suspend(std::coroutine_handle<> awaiting) const
            {
                if (ctx.IsCancellationRequested())
                {
                    return false;
                }

                if (std::apply([](auto&... task) { return (task.IsCompleted() && ...); }, tasks))
                {
                    return false;
                }

                state->exec     = ctx.GetExecutor();
                state->awaiting = awaiting;
                state->remaining.store(static_cast<NGIN::UIntSize>(sizeof...(TTasks)), std::memory_order_release);
                state->watchers.resize(sizeof...(TTasks));
                state->childHandles.resize(sizeof...(TTasks));
                state->childFinished.resize(sizeof...(TTasks));
                state->childDetach.resize(sizeof...(TTasks));

                std::weak_ptr<SharedState<E>> weakState = state;
                auto&                         exec      = state->exec;
                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    (([&] {
                         auto watch               = WatchTask<E>(weakState, std::get<Indices>(tasks), static_cast<NGIN::UIntSize>(Indices));
                         state->watchers[Indices] = std::coroutine_handle<>::from_address(watch.handle.address());
                         state->childHandles[Indices] =
                                 std::coroutine_handle<>::from_address(std::get<Indices>(tasks).Handle().address());
                         state->childFinished[Indices] = &IsTaskFinished<decltype(std::get<Indices>(tasks))>;
                         state->childDetach[Indices]   = &DetachTaskContinuation<decltype(std::get<Indices>(tasks))>;
                     }()),
                     ...);
                }(std::make_index_sequence<sizeof...(TTasks)> {});

                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    ((exec.Execute(state->watchers[Indices])), ...);
                }(std::make_index_sequence<sizeof...(TTasks)> {});

                ctx.GetCancellationToken().Register(
                        state->cancellationRegistration, state->exec, awaiting, &CancelWhenAll<E>, state.get());

                std::apply([&](auto&... task) { (task.Schedule(ctx), ...); }, tasks);

                return true;
            }

            void await_resume() const noexcept {}
        };
    }// namespace detail::when_all

    template<typename TFirstTask, typename... TOtherTasks>
        requires(std::is_same_v<std::remove_reference_t<TFirstTask>,
                                Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType>>) &&
                (std::is_same_v<typename std::remove_reference_t<TFirstTask>::ErrorType,
                                typename std::remove_reference_t<TOtherTasks>::ErrorType> &&
                 ...) &&
                (std::is_same_v<std::remove_reference_t<TOtherTasks>,
                                Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType>> &&
                 ...)
    [[nodiscard]] inline Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType> WhenAll(TaskContext& ctx, TFirstTask& firstTask, TOtherTasks&... otherTasks)
    {
        using E = typename std::remove_reference_t<TFirstTask>::ErrorType;

        if (ctx.IsCancellationRequested())
        {
            co_await NGIN::Async::Canceled();
            co_return;
        }

        auto state = std::make_shared<detail::when_all::SharedState<E>>();
        co_await detail::when_all::Awaiter<E, TFirstTask, TOtherTasks...> {
                ctx,
                state,
                std::tuple<TFirstTask&, TOtherTasks&...>(firstTask, otherTasks...)};

        if (state->status == TaskStatus::DomainError)
        {
            co_await NGIN::Async::DomainFailure(std::move(*state->domainError));
            co_return;
        }
        if (state->status == TaskStatus::Canceled || ctx.IsCancellationRequested())
        {
            co_await NGIN::Async::Canceled();
            co_return;
        }
        if (state->status == TaskStatus::Fault)
        {
            co_await NGIN::Async::Faulted(*state->fault);
            co_return;
        }

        for (auto* task: std::initializer_list<Task<void, E>*> {&firstTask, &otherTasks...})
        {
            auto outcome = task->Get();
            if (outcome.IsDomainError())
            {
                co_await NGIN::Async::DomainFailure(outcome.DomainError());
                co_return;
            }
            if (outcome.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }
            if (outcome.IsFault())
            {
                co_await NGIN::Async::Faulted(outcome.Fault());
                co_return;
            }
        }

        co_return;
    }

    template<typename E, typename... T>
        requires(sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
    [[nodiscard]] inline Task<std::tuple<T...>, E> WhenAll(TaskContext& ctx, Task<T, E>&... tasks)
    {
        using OutCompletion = Completion<std::tuple<T...>, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        auto state = std::make_shared<detail::when_all::SharedState<E>>();
        co_await detail::when_all::Awaiter<E, Task<T, E>...> {ctx, state, std::tuple<Task<T, E>&...>(tasks...)};

        if (state->status == TaskStatus::DomainError)
        {
            co_return OutCompletion::DomainFailure(std::move(*state->domainError));
        }
        if (state->status == TaskStatus::Canceled || ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }
        if (state->status == TaskStatus::Fault)
        {
            co_return OutCompletion::Faulted(*state->fault);
        }

        auto output = std::tuple<T...> {std::move(*tasks.Get())...};
        co_return output;
    }
}// namespace NGIN::Async
