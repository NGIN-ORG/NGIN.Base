/// @file WhenAll.hpp
/// @brief Task combinator that completes when all tasks complete.
#pragma once

#include <atomic>
#include <coroutine>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/SpinLock.hpp>

namespace NGIN::Async
{
    namespace detail::when_all
    {
        struct WhenAllSharedState final
        {
            std::atomic<bool>               done {false};
            std::atomic<NGIN::UIntSize>     remaining {0};
            NGIN::Execution::ExecutorRef    exec {};
            std::coroutine_handle<>         awaiting {};
            CancellationRegistration        cancellationRegistration {};

            NGIN::Sync::SpinLock            errorLock {};
            AsyncError                      firstError {};
            bool                            hasError {false};
        };

        [[nodiscard]] inline bool CancelWhenAll(void* ctx) noexcept
        {
            auto* state = static_cast<WhenAllSharedState*>(ctx);
            bool expected = false;
            if (!state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }
            NGIN::Sync::LockGuard guard(state->errorLock);
            if (!state->hasError)
            {
                state->firstError = MakeAsyncError(AsyncErrorCode::Canceled);
                state->hasError = true;
            }
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
        [[nodiscard]] inline Detached WatchTask(std::weak_ptr<WhenAllSharedState> weakState, TTask& task)
        {
            if (auto result = co_await task; !result)
            {
                if (auto state = weakState.lock())
                {
                    NGIN::Sync::LockGuard guard(state->errorLock);
                    if (!state->hasError)
                    {
                        state->firstError = result.error();
                        state->hasError = true;
                    }
                }
            }

            if (auto state = weakState.lock())
            {
                const auto left = state->remaining.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (left == 0)
                {
                    bool expected = false;
                    if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        if (state->awaiting)
                        {
                            state->exec.Execute(state->awaiting);
                        }
                    }
                }
            }

            co_return;
        }

        template<typename... TTasks>
        struct WhenAllAwaiter final
        {
            TaskContext&                       ctx;
            std::shared_ptr<WhenAllSharedState> state;
            std::tuple<TTasks&...>             tasks;

            bool await_ready() const noexcept
            {
                if (ctx.IsCancellationRequested())
                {
                    return true;
                }
                return std::apply([](auto&... t) { return (t.IsCompleted() && ...); }, tasks);
            }

            bool await_suspend(std::coroutine_handle<> awaiting) const
            {
                std::apply([&](auto&... t) { (t.Schedule(ctx), ...); }, tasks);

                if (ctx.IsCancellationRequested())
                {
                    return false;
                }

                if (std::apply([](auto&... t) { return (t.IsCompleted() && ...); }, tasks))
                {
                    return false;
                }

                state->exec      = ctx.GetExecutor();
                state->awaiting  = awaiting;
                state->remaining.store(static_cast<NGIN::UIntSize>(sizeof...(TTasks)), std::memory_order_release);

                ctx.GetCancellationToken().Register(
                        state->cancellationRegistration, state->exec, awaiting, &CancelWhenAll, state.get());

                std::weak_ptr<WhenAllSharedState> weakState = state;
                auto& exec                                  = state->exec;

                std::apply(
                        [&](auto&... t) {
                            (([&] {
                                 auto watch = WatchTask(weakState, t);
                                 exec.Execute(std::coroutine_handle<>::from_address(watch.handle.address()));
                             }()),
                             ...);
                        },
                        tasks);

                return true;
            }

            AsyncExpected<void> await_resume() const noexcept
            {
                if (ctx.IsCancellationRequested())
                {
                    return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
                }
                if (state->hasError)
                {
                    return std::unexpected(state->firstError);
                }
                return {};
            }
        };
    }// namespace detail::when_all

    /// @brief Await multiple `Task<void>` and complete when all are finished.
    template<typename... TTasks>
        requires(sizeof...(TTasks) > 0) && (std::is_same_v<std::remove_reference_t<TTasks>, Task<void>> && ...)
    [[nodiscard]] inline Task<void> WhenAll(TaskContext& ctx, TTasks&... tasks)
    {
        if (ctx.IsCancellationRequested())
        {
            co_await Task<void>::ReturnError(MakeAsyncError(AsyncErrorCode::Canceled));
            co_return;
        }
        auto state = std::make_shared<detail::when_all::WhenAllSharedState>();
        auto waitResult =
                co_await detail::when_all::WhenAllAwaiter<TTasks...> {ctx, state, std::tuple<TTasks&...>(tasks...)};
        if (!waitResult)
        {
            co_await Task<void>::ReturnError(waitResult.error());
            co_return;
        }

        // Surface faults from individual tasks (first error wins, .NET-like aggregation can be added later).
        for (auto* taskPtr: std::initializer_list<Task<void>*> {&tasks...})
        {
            auto result = taskPtr->Get();
            if (!result)
            {
                co_await Task<void>::ReturnError(result.error());
                co_return;
            }
        }
        co_return;
    }

    /// @brief Await multiple `Task<T>` and complete when all are finished, returning a tuple of results.
    template<typename... T>
        requires(sizeof...(T) > 0) && ((!std::is_void_v<T>) && ...)
    [[nodiscard]] inline Task<std::tuple<T...>> WhenAll(TaskContext& ctx, Task<T>&... tasks)
    {
        if (ctx.IsCancellationRequested())
        {
            co_return std::unexpected(MakeAsyncError(AsyncErrorCode::Canceled));
        }
        auto state = std::make_shared<detail::when_all::WhenAllSharedState>();
        auto waitResult = co_await detail::when_all::WhenAllAwaiter<Task<T>...> {ctx,
                                                                               state,
                                                                               std::tuple<Task<T>&...>(tasks...)};
        if (!waitResult)
        {
            co_return std::unexpected(waitResult.error());
        }

        auto results = std::tuple<AsyncExpected<T>...> {tasks.Get()...};
        AsyncError firstError {};
        bool       failed = false;
        std::apply(
                [&](auto&... r) {
                    (([&] {
                         if (!failed && !r)
                         {
                             firstError = r.error();
                             failed = true;
                         }
                     }()),
                     ...);
                },
                results);

        if (failed)
        {
            co_return std::unexpected(firstError);
        }

        auto output = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::tuple<T...> {std::move(*std::get<Is>(results))...};
        }(std::make_index_sequence<sizeof...(T)> {});

        co_return output;
    }
}// namespace NGIN::Async
