/// @file WhenAll.hpp
/// @brief Task combinator that completes when all tasks complete.
#pragma once

#include <atomic>
#include <coroutine>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

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
            std::atomic<bool>            done {false};
            std::atomic<NGIN::UIntSize>  remaining {0};
            NGIN::Execution::ExecutorRef exec {};
            std::coroutine_handle<>      awaiting {};
            CancellationRegistration     cancellationRegistration {};

            TaskStatus               status {TaskStatus::Pending};
            std::optional<E>         domainError {};
            std::optional<AsyncFault> fault {};
        };

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
        [[nodiscard]] inline Detached WatchTask(std::weak_ptr<SharedState<E>> weakState, TTask& task, NGIN::UIntSize)
        {
            auto outcome = co_await task.AsOutcome();

            if (auto state = weakState.lock())
            {
                if (!outcome.Succeeded())
                {
                    bool expected = false;
                    if (state->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        if (outcome.IsDomainError())
                        {
                            state->status = TaskStatus::DomainError;
                            state->domainError = outcome.DomainError();
                        }
                        else if (outcome.IsCanceled())
                        {
                            state->status = TaskStatus::Canceled;
                        }
                        else if (outcome.IsFault())
                        {
                            state->status = TaskStatus::Fault;
                            state->fault = outcome.Fault();
                        }

                        if (state->awaiting)
                        {
                            state->exec.Execute(state->awaiting);
                        }
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
                        if (state->awaiting)
                        {
                            state->exec.Execute(state->awaiting);
                        }
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
                std::apply([&](auto&... task) { (task.Schedule(ctx), ...); }, tasks);

                if (ctx.IsCancellationRequested())
                {
                    return false;
                }

                if (std::apply([](auto&... task) { return (task.IsCompleted() && ...); }, tasks))
                {
                    return false;
                }

                state->exec = ctx.GetExecutor();
                state->awaiting = awaiting;
                state->remaining.store(static_cast<NGIN::UIntSize>(sizeof...(TTasks)), std::memory_order_release);

                ctx.GetCancellationToken().Register(
                        state->cancellationRegistration, state->exec, awaiting, &CancelWhenAll<E>, state.get());

                std::weak_ptr<SharedState<E>> weakState = state;
                auto& exec = state->exec;
                [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                    (([&] {
                         auto watch = WatchTask<E>(weakState, std::get<Indices>(tasks), static_cast<NGIN::UIntSize>(Indices));
                         exec.Execute(std::coroutine_handle<>::from_address(watch.handle.address()));
                     }()),
                     ...);
                }(std::make_index_sequence<sizeof...(TTasks)> {});

                return true;
            }

            void await_resume() const noexcept {}
        };
    }// namespace detail::when_all

    template<typename TFirstTask, typename... TOtherTasks>
        requires(std::is_same_v<std::remove_reference_t<TFirstTask>,
                                Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType>>) &&
                (std::is_same_v<typename std::remove_reference_t<TFirstTask>::ErrorType,
                                typename std::remove_reference_t<TOtherTasks>::ErrorType> && ...) &&
                (std::is_same_v<std::remove_reference_t<TOtherTasks>,
                                Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType>> && ...)
    [[nodiscard]] inline Task<void, typename std::remove_reference_t<TFirstTask>::ErrorType>
    WhenAll(TaskContext& ctx, TFirstTask& firstTask, TOtherTasks&... otherTasks)
    {
        using E = typename std::remove_reference_t<TFirstTask>::ErrorType;

        if (ctx.IsCancellationRequested())
        {
            co_await Task<void, E>::ReturnCanceled();
            co_return;
        }

        auto state = std::make_shared<detail::when_all::SharedState<E>>();
        co_await detail::when_all::Awaiter<E, TFirstTask, TOtherTasks...> {
                ctx,
                state,
                std::tuple<TFirstTask&, TOtherTasks&...>(firstTask, otherTasks...)};

        if (state->status == TaskStatus::DomainError)
        {
            co_await Task<void, E>::ReturnError(std::move(*state->domainError));
            co_return;
        }
        if (state->status == TaskStatus::Canceled || ctx.IsCancellationRequested())
        {
            co_await Task<void, E>::ReturnCanceled();
            co_return;
        }
        if (state->status == TaskStatus::Fault)
        {
            co_await Task<void, E>::ReturnFault(*state->fault);
            co_return;
        }

        for (auto* task: std::initializer_list<Task<void, E>*> {&firstTask, &otherTasks...})
        {
            auto outcome = task->Get();
            if (outcome.IsDomainError())
            {
                co_await Task<void, E>::ReturnError(outcome.DomainError());
                co_return;
            }
            if (outcome.IsCanceled())
            {
                co_await Task<void, E>::ReturnCanceled();
                co_return;
            }
            if (outcome.IsFault())
            {
                co_await Task<void, E>::ReturnFault(outcome.Fault());
                co_return;
            }
        }

        co_return;
    }

    template<typename E, typename... T>
        requires(sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
    [[nodiscard]] inline Task<std::tuple<T...>, E> WhenAll(TaskContext& ctx, Task<T, E>&... tasks)
    {
        if (ctx.IsCancellationRequested())
        {
            co_return Canceled;
        }

        auto state = std::make_shared<detail::when_all::SharedState<E>>();
        co_await detail::when_all::Awaiter<E, Task<T, E>...> {ctx, state, std::tuple<Task<T, E>&...>(tasks...)};

        if (state->status == TaskStatus::DomainError)
        {
            co_return NGIN::Utilities::Unexpected<E>(std::move(*state->domainError));
        }
        if (state->status == TaskStatus::Canceled || ctx.IsCancellationRequested())
        {
            co_return Canceled;
        }
        if (state->status == TaskStatus::Fault)
        {
            co_return Fault(*state->fault);
        }

        auto output = std::tuple<T...> {std::move(*tasks.Get())...};
        co_return output;
    }
}// namespace NGIN::Async
