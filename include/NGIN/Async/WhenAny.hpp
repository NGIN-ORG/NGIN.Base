/// @file WhenAny.hpp
/// @brief Structured task combinator that cancels and drains losing child operations.
#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    namespace detail::when_any
    {
        template<typename Factory>
        using FactoryTask = std::remove_cvref_t<std::invoke_result_t<Factory&, TaskContext&>>;

        template<typename E>
        struct SharedState final
        {
            SharedState(TaskContext& parent, const NGIN::UIntSize childCount)
                : exec(parent.GetExecutor()), totalChildren(childCount)
            {
                loserCancellation.reserve(childCount);
                childContexts.reserve(childCount);
                for (NGIN::UIntSize index = 0; index < childCount; ++index)
                {
                    loserCancellation.emplace_back();
                    childContexts.push_back(parent.WithLinkedCancellationToken(loserCancellation.back().GetToken()));
                }
            }

            NGIN::Execution::ExecutorRef                 exec {};
            std::mutex                                   mutex {};
            bool                                         completed {false};
            NGIN::UIntSize                               completedChildren {0};
            NGIN::UIntSize                               totalChildren {0};
            std::optional<Completion<NGIN::UIntSize, E>> firstCompletion {};
            std::coroutine_handle<>                      continuation {};
            std::coroutine_handle<>                      allContinuation {};
            std::vector<CancellationSource>              loserCancellation {};
            std::vector<TaskContext>                     childContexts {};

            [[nodiscard]] TaskContext& ChildContext(const NGIN::UIntSize index) noexcept
            {
                return childContexts[index];
            }

            [[nodiscard]] bool SetContinuation(std::coroutine_handle<> handle)
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (completed)
                {
                    return false;
                }

                continuation = handle;
                return true;
            }

            [[nodiscard]] bool SetAllContinuation(std::coroutine_handle<> handle)
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (completedChildren == totalChildren)
                {
                    return false;
                }
                allContinuation = handle;
                return true;
            }

            void Complete(const NGIN::UIntSize completedIndex, Completion<NGIN::UIntSize, E> outcome)
            {
                std::coroutine_handle<> firstToResume {};
                std::coroutine_handle<> allToResume {};
                bool                    selectedWinner = false;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    ++completedChildren;
                    if (!completed)
                    {
                        completed       = true;
                        selectedWinner  = true;
                        firstCompletion = std::move(outcome);
                        firstToResume   = continuation;
                    }
                    if (completedChildren == totalChildren)
                    {
                        allToResume = allContinuation;
                    }
                }

                // Cancellation may synchronously invoke arbitrary callbacks, so it must remain outside the state lock.
                if (selectedWinner)
                {
                    for (NGIN::UIntSize index = 0; index < loserCancellation.size(); ++index)
                    {
                        if (index != completedIndex)
                        {
                            loserCancellation[index].Cancel();
                        }
                    }
                }

                Resume(firstToResume);
                if (allToResume && allToResume != firstToResume)
                {
                    Resume(allToResume);
                }
            }

            void Resume(const std::coroutine_handle<> handle) const noexcept
            {
                if (!handle)
                {
                    return;
                }

                if (exec.IsValid())
                {
                    const NGIN::Execution::ScheduleResult result = exec.Execute(handle);
                    if (!result)
                    {
                        handle.resume();
                    }
                }
                else
                {
                    handle.resume();
                }
            }
        };

        template<typename E>
        class AwaitAll final
        {
        public:
            explicit AwaitAll(std::shared_ptr<SharedState<E>> state) noexcept
                : m_state(std::move(state))
            {
            }

            [[nodiscard]] bool await_ready() const
            {
                std::lock_guard<std::mutex> guard(m_state->mutex);
                return m_state->completedChildren == m_state->totalChildren;
            }

            [[nodiscard]] bool await_suspend(std::coroutine_handle<> awaiting)
            {
                return m_state->SetAllContinuation(awaiting);
            }

            void await_resume() const noexcept {}

        private:
            std::shared_ptr<SharedState<E>> m_state;
        };

        template<typename E>
        class AwaitFirst final
        {
        public:
            explicit AwaitFirst(std::shared_ptr<SharedState<E>> state) noexcept
                : m_state(std::move(state))
            {
            }

            [[nodiscard]] bool await_ready() const
            {
                std::lock_guard<std::mutex> guard(m_state->mutex);
                return m_state->completed;
            }

            [[nodiscard]] bool await_suspend(std::coroutine_handle<> awaiting)
            {
                return m_state->SetContinuation(awaiting);
            }

            [[nodiscard]] Completion<NGIN::UIntSize, E> await_resume() const
            {
                std::lock_guard<std::mutex> guard(m_state->mutex);
                return std::move(*m_state->firstCompletion);
            }

        private:
            std::shared_ptr<SharedState<E>> m_state;
        };

        template<typename E, typename T>
        [[nodiscard]] Completion<NGIN::UIntSize, E> ConvertWinner(
                const NGIN::UIntSize winner,
                Completion<T, E>     completion)
        {
            if (completion.Succeeded())
            {
                return Completion<NGIN::UIntSize, E>::Success(winner);
            }
            if (completion.IsDomainError())
            {
                return Completion<NGIN::UIntSize, E>::DomainFailure(std::move(completion).DomainError());
            }
            if (completion.IsCanceled())
            {
                return Completion<NGIN::UIntSize, E>::Canceled();
            }
            return Completion<NGIN::UIntSize, E>::Faulted(std::move(completion).Fault());
        }

        template<std::size_t Index, typename T, typename E>
        inline Task<void, E> Watch(std::shared_ptr<SharedState<E>> state, Operation<T, E> operation)
        {
            Completion<T, E>         completion = co_await operation;
            constexpr NGIN::UIntSize index      = static_cast<NGIN::UIntSize>(Index);
            state->Complete(index, ConvertWinner<E>(index, std::move(completion)));
            co_return;
        }

        template<typename E, typename FactoriesTuple, std::size_t... Indices>
        [[nodiscard]] inline auto SpawnChildren(
                const std::shared_ptr<SharedState<E>>& state,
                FactoriesTuple&                        factories,
                std::index_sequence<Indices...>)
        {
            return std::tuple {
                    Spawn(state->ChildContext(Indices),
                          std::invoke(std::get<Indices>(factories), state->ChildContext(Indices)))...};
        }

        template<typename E, typename OperationsTuple, std::size_t... Indices>
        inline void DetachWatchers(
                TaskContext&                           ctx,
                const std::shared_ptr<SharedState<E>>& state,
                OperationsTuple&                       operations,
                std::index_sequence<Indices...>)
        {
            (Detach(ctx, Watch<Indices>(state, std::move(std::get<Indices>(operations)))), ...);
        }
    }// namespace detail::when_any

    /// @brief Runs child factories, cancels every loser, drains them, and returns the winning argument index.
    /// @details Each factory receives a child-specific context linked to the parent context. The first terminal child
    /// selects the outcome, after which cancellation is requested for every loser. The combinator returns only after
    /// all child watchers finish, so a slow or non-cooperative loser adds observable completion latency. A winning
    /// domain error, cancellation, or fault is propagated after the drain.
    template<typename... TFactories>
        requires(sizeof...(TFactories) > 0) &&
                (std::invocable<TFactories&, TaskContext&> && ...) &&
                (detail::IsTaskTypeV<detail::when_any::FactoryTask<TFactories>> && ...) &&
                (std::is_same_v<typename detail::when_any::FactoryTask<TFactories>::ErrorType,
                                typename detail::when_any::FactoryTask<
                                        std::tuple_element_t<0, std::tuple<TFactories...>>>::ErrorType> &&
                 ...)
    [[nodiscard]] inline Task<
            NGIN::UIntSize,
            typename detail::when_any::FactoryTask<std::tuple_element_t<0, std::tuple<TFactories...>>>::ErrorType> WhenAny(TaskContext& ctx, TFactories... factories)
    {
        using E = typename detail::when_any::FactoryTask<
                std::tuple_element_t<0, std::tuple<TFactories...>>>::ErrorType;
        using OutCompletion = Completion<NGIN::UIntSize, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        std::shared_ptr<detail::when_any::SharedState<E>> state =
                std::make_shared<detail::when_any::SharedState<E>>(ctx, sizeof...(TFactories));
        std::tuple<TFactories...> factoryTuple(std::move(factories)...);
        auto                      operations = detail::when_any::SpawnChildren(
                state,
                factoryTuple,
                std::make_index_sequence<sizeof...(TFactories)> {});
        detail::when_any::DetachWatchers(
                ctx,
                state,
                operations,
                std::make_index_sequence<sizeof...(TFactories)> {});

        OutCompletion winner = co_await detail::when_any::AwaitFirst<E> {state};
        co_await detail::when_any::AwaitAll<E> {state};
        co_return winner;
    }
}// namespace NGIN::Async
