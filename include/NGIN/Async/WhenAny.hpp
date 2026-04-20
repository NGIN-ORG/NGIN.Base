/// @file WhenAny.hpp
/// @brief Task combinator that completes when any task completes.
#pragma once

#include <atomic>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    namespace detail::when_any
    {
        struct SharedState final
        {
            std::atomic<bool>            done {false};
            std::atomic<NGIN::UIntSize>  index {static_cast<NGIN::UIntSize>(-1)};
            NGIN::Execution::ExecutorRef exec {};
            std::coroutine_handle<>      awaiting {};
            CancellationRegistration     cancellationRegistration {};
        };

        [[nodiscard]] inline bool CancelWhenAny(void* context) noexcept
        {
            auto* state    = static_cast<SharedState*>(context);
            bool  expected = false;
            if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }

            state->index.store(static_cast<NGIN::UIntSize>(-1), std::memory_order_release);
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
            (void) co_await task.AsCompletion();

            bool expected = false;
            if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                state->index.store(index, std::memory_order_release);
                if (state->awaiting)
                {
                    state->exec.Execute(state->awaiting);
                }
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
                ctx.GetCancellationToken().Register(state->cancellationRegistration,
                                                    state->exec,
                                                    awaiting,
                                                    &CancelWhenAny,
                                                    state.get());

                std::apply([&](auto&... task) { (task.Schedule(ctx), ...); }, tasks);

                auto& exec = state->exec;
                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    (([&] {
                         auto watch = WatchTask(state, std::get<Indices>(tasks), static_cast<NGIN::UIntSize>(Indices));
                         exec.Execute(std::coroutine_handle<>::from_address(watch.handle.address()));
                     }()),
                     ...);
                }(std::make_index_sequence<sizeof...(TTasks)> {});

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
