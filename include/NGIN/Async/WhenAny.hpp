/// @file WhenAny.hpp
/// @brief Task combinator that completes when any task completes.
#pragma once

#include <atomic>
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
    namespace detail::when_any
    {
        struct SharedState final
        {
            using IsFinishedFn = bool (*)(std::coroutine_handle<>) noexcept;
            using DetachFn     = void (*)(std::coroutine_handle<>, std::coroutine_handle<>) noexcept;

            std::atomic<bool>                    done {false};
            std::atomic<NGIN::UIntSize>          index {static_cast<NGIN::UIntSize>(-1)};
            NGIN::Execution::ExecutorRef         exec {};
            std::coroutine_handle<>              awaiting {};
            CancellationRegistration             cancellationRegistration {};
            std::vector<std::coroutine_handle<>> watchers {};
            std::vector<std::coroutine_handle<>> childHandles {};
            std::vector<IsFinishedFn>            childFinished {};
            std::vector<DetachFn>                childDetach {};
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

        [[nodiscard]] inline bool IsChildFinished(const SharedState& state, NGIN::UIntSize index) noexcept
        {
            return index < state.childHandles.size() && state.childFinished[index] &&
                   state.childFinished[index](state.childHandles[index]);
        }

        inline void ResumeWatcherIfSuspended(const SharedState& state, NGIN::UIntSize index) noexcept
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

        inline void ResumeAwaiting(const SharedState& state) noexcept
        {
            if (state.awaiting)
            {
                state.exec.Execute(state.awaiting);
            }
        }

        inline void ResumeOtherWatchers(const SharedState& state, NGIN::UIntSize currentIndex) noexcept
        {
            for (NGIN::UIntSize index = 0; index < state.watchers.size(); ++index)
            {
                if (index != currentIndex)
                {
                    ResumeWatcherIfSuspended(state, index);
                }
            }
        }

        inline void ResumeAllWatchers(const SharedState& state) noexcept
        {
            for (NGIN::UIntSize index = 0; index < state.watchers.size(); ++index)
            {
                ResumeWatcherIfSuspended(state, index);
            }
        }

        [[nodiscard]] inline bool CancelWhenAny(void* context) noexcept
        {
            auto* state    = static_cast<SharedState*>(context);
            bool  expected = false;
            if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            state->index.store(static_cast<NGIN::UIntSize>(-1), std::memory_order_release);
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

        template<typename TTask>
        [[nodiscard]] inline Detached WatchTask(std::shared_ptr<SharedState> state, TTask& task, NGIN::UIntSize index)
        {
            (void) co_await NGIN::Async::detail::AwaitTaskHandleCompletion(task);

            if (index < state->watchers.size())
            {
                state->watchers[index] = {};
            }

            if (state->done.load(std::memory_order_acquire))
            {
                co_return;
            }

            bool expected = false;
            if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                state->index.store(index, std::memory_order_release);
                ResumeOtherWatchers(*state, index);
                ResumeAwaiting(*state);
            }

            co_return;
        }

        template<typename... TTasks>
        struct Awaiter final
        {
            TaskContext&                 ctx;
            std::shared_ptr<SharedState> state;
            std::tuple<TTasks&...>       tasks;

            bool await_ready() const noexcept
            {
                return ctx.IsCancellationRequested() ||
                       std::apply([](auto&... task) { return (task.IsCompleted() || ...); }, tasks);
            }

            bool await_suspend(std::coroutine_handle<> awaiting) const
            {
                if (ctx.IsCancellationRequested())
                {
                    return false;
                }

                state->exec     = ctx.GetExecutor();
                state->awaiting = awaiting;
                state->watchers.resize(sizeof...(TTasks));
                state->childHandles.resize(sizeof...(TTasks));
                state->childFinished.resize(sizeof...(TTasks));
                state->childDetach.resize(sizeof...(TTasks));

                auto& exec = state->exec;
                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    (([&] {
                         auto watch               = WatchTask(state, std::get<Indices>(tasks), static_cast<NGIN::UIntSize>(Indices));
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

                ctx.GetCancellationToken().Register(state->cancellationRegistration,
                                                    state->exec,
                                                    awaiting,
                                                    &CancelWhenAny,
                                                    state.get());

                std::apply([&](auto&... task) { (task.Schedule(ctx), ...); }, tasks);

                return true;
            }

            [[nodiscard]] NGIN::UIntSize await_resume() const noexcept
            {
                if (ctx.IsCancellationRequested())
                {
                    return static_cast<NGIN::UIntSize>(-1);
                }

                const auto signaled = state->index.load(std::memory_order_acquire);
                if (signaled != static_cast<NGIN::UIntSize>(-1))
                {
                    return signaled;
                }

                NGIN::UIntSize found = static_cast<NGIN::UIntSize>(-1);
                NGIN::UIntSize index = 0;
                std::apply(
                        [&](auto&... task) {
                            (([&] {
                                 if (found == static_cast<NGIN::UIntSize>(-1) && task.IsCompleted())
                                 {
                                     found = index;
                                 }
                                 ++index;
                             }()),
                             ...);
                        },
                        tasks);
                return found;
            }
        };
    }// namespace detail::when_any

    template<typename TFirstTask, typename... TOtherTasks>
        requires(detail::IsTaskTypeV<TFirstTask>) && (detail::IsTaskTypeV<TOtherTasks> && ...) &&
                (std::is_same_v<typename std::remove_reference_t<TFirstTask>::ErrorType,
                                typename std::remove_reference_t<TOtherTasks>::ErrorType> &&
                 ...)
    [[nodiscard]] inline Task<NGIN::UIntSize, typename std::remove_reference_t<TFirstTask>::ErrorType> WhenAny(TaskContext& ctx, TFirstTask& firstTask, TOtherTasks&... otherTasks)
    {
        using E             = typename std::remove_reference_t<TFirstTask>::ErrorType;
        using OutCompletion = Completion<NGIN::UIntSize, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        auto state = std::make_shared<detail::when_any::SharedState>();
        auto index = co_await detail::when_any::Awaiter<TFirstTask, TOtherTasks...> {
                ctx,
                std::move(state),
                std::tuple<TFirstTask&, TOtherTasks&...>(firstTask, otherTasks...)};
        if (ctx.IsCancellationRequested() || index == static_cast<NGIN::UIntSize>(-1))
        {
            co_return OutCompletion::Canceled();
        }

        co_return index;
    }
}// namespace NGIN::Async
