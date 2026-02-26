/// @file WhenAny.hpp
/// @brief Task combinator that completes when any task completes.
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        template<typename T>
        struct IsTaskType final: std::false_type
        {
        };

        template<typename TValue>
        struct IsTaskType<Task<TValue>> final: std::true_type
        {
        };

        template<typename T>
        inline constexpr bool IsTaskTypeValue = IsTaskType<std::remove_cvref_t<T>>::value;

        struct WhenAnySharedState final
        {
            std::atomic<bool>           done {false};
            std::atomic<NGIN::UIntSize> index {static_cast<NGIN::UIntSize>(-1)};
            NGIN::Execution::ExecutorRef exec {};
            std::coroutine_handle<>      awaiting {};
            CancellationRegistration     cancellationRegistration {};
        };

        [[nodiscard]] inline bool CancelWhenAny(void* ctx) noexcept
        {
            auto* state = static_cast<WhenAnySharedState*>(ctx);
            bool expected = false;
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
        [[nodiscard]] inline Detached WatchTask(std::shared_ptr<WhenAnySharedState> state, TTask& task, NGIN::UIntSize index)
        {
            (void)co_await task;

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
        struct WhenAnyAwaiter final
        {
            TaskContext&                          ctx;
            std::shared_ptr<WhenAnySharedState> state;
            std::tuple<TTasks&...>               tasks;

            bool await_ready() const noexcept
            {
                if (ctx.IsCancellationRequested())
                {
                    return true;
                }
                return std::apply([](auto&... t) { return (t.IsCompleted() || ...); }, tasks);
            }

            void await_suspend(std::coroutine_handle<> awaiting) const
            {
                if (ctx.IsCancellationRequested())
                {
                    return;
                }

                state->awaiting = awaiting;
                ctx.GetCancellationToken().Register(state->cancellationRegistration, state->exec, awaiting, &CancelWhenAny, state.get());

                std::apply([&](auto&... t) { (t.Schedule(ctx), ...); }, tasks);

                auto& exec = state->exec;
                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    (([&] {
                         auto watch = WatchTask(state, std::get<Indices>(tasks), static_cast<NGIN::UIntSize>(Indices));
                         exec.Execute(std::coroutine_handle<>::from_address(watch.handle.address()));
                     }()),
                     ...);
                }(std::make_index_sequence<sizeof...(TTasks)> {});
            }

            AsyncExpected<NGIN::UIntSize> await_resume() const noexcept
            {
                if (ctx.IsCancellationRequested())
                {
                    return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                }

                const auto signaled = state->index.load(std::memory_order_acquire);
                if (signaled != static_cast<NGIN::UIntSize>(-1))
                {
                    return signaled;
                }

                NGIN::UIntSize found = static_cast<NGIN::UIntSize>(-1);
                NGIN::UIntSize idx   = 0;
                std::apply(
                        [&](auto&... t) {
                            (([&] {
                                 if (found == static_cast<NGIN::UIntSize>(-1) && t.IsCompleted())
                                 {
                                     found = idx;
                                 }
                                 ++idx;
                             }()),
                             ...);
                        },
                        tasks);

                if (found == static_cast<NGIN::UIntSize>(-1))
                {
                    return std::unexpected(MakeAsyncError(AsyncErrorCode::InvalidState));
                }
                return found;
            }
        };
    }// namespace detail

    /// @brief Complete when any of the tasks completes; returns the index of the first completed task (0-based).
    ///
    /// Completion includes success, fault, or cancellation of the underlying task.
    template<typename... TTasks>
        requires(sizeof...(TTasks) > 0) && (detail::IsTaskTypeValue<TTasks> && ...)
    [[nodiscard]] inline Task<NGIN::UIntSize> WhenAny(TaskContext& ctx, TTasks&... tasks)
    {
        if (ctx.IsCancellationRequested())
        {
            co_return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
        }

        auto state  = std::make_shared<detail::WhenAnySharedState>();
        state->exec = ctx.GetExecutor();

        auto result = co_await detail::WhenAnyAwaiter<TTasks...> {ctx, std::move(state), std::tuple<TTasks&...>(tasks...)};
        if (!result)
        {
            co_return std::unexpected(result.error());
        }

        co_return *result;
    }
}// namespace NGIN::Async
