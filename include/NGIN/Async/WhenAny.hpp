/// @file WhenAny.hpp
/// @brief Structured task combinator that cancels and drains losing child operations.
#pragma once

#include <array>
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

        struct TrackedTask final
        {
            bool tracked {};
            bool await_ready() const noexcept { return false; }
            template<typename Promise>
                requires std::derived_from<Promise, detail::PromiseRuntimeCommon>
            bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                tracked = awaiting.promise().m_taskContinuation.IsValid();
                return false;
            }
            bool await_resume() const noexcept { return tracked; }
        };

        template<typename E, typename... Factories>
        struct SharedState final
        {
            using Outcome                           = Completion<NGIN::UIntSize, E>;
            static constexpr std::size_t ChildCount = sizeof...(Factories);

            SharedState(TaskContext& parent, Factories... callbacks)
                : exec(parent.GetExecutor()), factories(std::in_place, std::move(callbacks)...)
            {
                loserCancellation.reserve(ChildCount);
                childContexts.reserve(ChildCount);
                for (std::size_t index = 0; index < ChildCount; ++index)
                {
                    loserCancellation.emplace_back();
                    childContexts.push_back(parent.WithLinkedCancellationToken(loserCancellation.back().GetToken()));
                }
            }

            void SelectWinner(std::size_t index, Outcome&& outcome) noexcept
            {
                bool winner = false;
                {
                    std::lock_guard lock(mutex);
                    if (!firstCompletion)
                    {
                        winner = true;
                        try
                        {
                            firstCompletion.emplace(std::move(outcome));
                        } catch (...)
                        {
                            firstCompletion.emplace(Outcome::Faulted(ExceptionFault()));
                        }
                    }
                }
                // Finish cancellation publication before contributing to the
                // join. Another child's notification may run concurrently.
                if (winner)
                    for (std::size_t other = 0; other < ChildCount; ++other)
                        if (other != index)
                            loserCancellation[other].Cancel();
            }

            void Complete() noexcept
            {
                detail::PromiseRuntimeCommon* target = nullptr;
                NGIN::Execution::WorkItem     resume;
                {
                    std::lock_guard lock(mutex);
                    ++completedChildren;
                    if (completedChildren == ChildCount && joinTarget)
                    {
                        target = std::exchange(joinTarget, nullptr);
                        resume = std::move(joinResume);
                    }
                }
                if (target)
                    target->QueueContinuation(std::move(resume));
            }

            static AsyncFault ExceptionFault() noexcept
            {
                AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                fault.capturedException = std::current_exception();
#endif
                return fault;
            }

            NGIN::Execution::ExecutorRef                                            exec;
            std::mutex                                                              mutex;
            std::size_t                                                             completedChildren {};
            std::optional<Outcome>                                                  firstCompletion;
            detail::PromiseRuntimeCommon*                                           joinTarget {};
            NGIN::Execution::WorkItem                                               joinResume;
            std::vector<CancellationSource>                                         loserCancellation;
            std::vector<TaskContext>                                                childContexts;
            std::optional<std::tuple<Factories...>>                                 factories;
            std::tuple<Operation<typename FactoryTask<Factories>::ValueType, E>...> operations;
            std::array<std::optional<AsyncFault>, ChildCount>                       creationFailures;
            std::array<bool, ChildCount>                                            observed {};
        };

        template<typename State>
        struct AwaitAll final
        {
            std::shared_ptr<State> state;

            bool await_ready() const noexcept
            {
                std::lock_guard lock(state->mutex);
                return state->completedChildren == State::ChildCount;
            }
            template<typename Promise>
                requires std::derived_from<Promise, detail::PromiseRuntimeCommon>
            bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                std::lock_guard lock(state->mutex);
                if (state->completedChildren == State::ChildCount)
                    return false;
                // The preflight check precedes child creation, so this join
                // never needs fresh admission or an inline rejection fallback.
                assert(awaiting.promise().m_taskContinuation.IsValid());
                awaiting.promise().RetainFrameReference();
                state->joinTarget = &awaiting.promise();
                state->joinResume = NGIN::Execution::WorkItem([awaiting] {
                    detail::PromiseRuntimeCommon::ResumeRetained(awaiting);
                });
                return true;
            }
            typename State::Outcome await_resume()
            {
                // No child or cancellation callback can still access a factory.
                // Release captures on the parent executor before returning.
                state->factories.reset();
                std::lock_guard lock(state->mutex);
                return std::move(*state->firstCompletion);
            }
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

        template<std::size_t Index, typename E, typename State>
        void ObserveChild(const std::shared_ptr<State>&           state,
                          NGIN::Execution::CompletionReservation& notification) noexcept
        {
            using Outcome = typename State::Outcome;
            if (!state->observed[Index])
            {
                state->observed[Index] = true;
                std::optional<Outcome> outcome;
                auto&                  operation = std::get<Index>(state->operations);
                try
                {
                    if (state->creationFailures[Index])
                        outcome.emplace(Outcome::Faulted(std::move(*state->creationFailures[Index])));
                    else
                        outcome.emplace(ConvertWinner<E>(static_cast<NGIN::UIntSize>(Index), operation.TakeResult()));
                } catch (...)
                {
                    outcome.emplace(Outcome::Faulted(State::ExceptionFault()));
                }
                state->SelectWinner(Index, std::move(*outcome));
                detail::OperationAccess::Retire(operation, notification);
                return;
            }
            // This second invocation follows final frame destruction, including
            // any lease still held by a concurrent cancellation/resume callback.
            notification.Reset();
            state->Complete();
        }

        template<typename E, typename State, std::size_t... Indices>
        NGIN::Execution::ScheduleResult ReserveNotifications(
                const std::shared_ptr<State>&                                          state,
                std::array<NGIN::Execution::CompletionReservation, State::ChildCount>& notifications,
                std::index_sequence<Indices...>)
        {
            NGIN::Execution::ScheduleResult admission {};
            ([&] {
                if (!admission)
                    return;
                auto ticket = state->exec.ReserveCompletion(NGIN::Execution::WorkItem([state, notification = &notifications[Indices]] {
                    ObserveChild<Indices, E>(state, *notification);
                }));
                if (!ticket)
                    admission = std::unexpected(ticket.error());
                else
                    notifications[Indices] = std::move(*ticket);
            }(),
             ...);
            return admission;
        }

        template<typename State, std::size_t... Indices>
        void SpawnChildren(
                const std::shared_ptr<State>&                                          state,
                std::array<NGIN::Execution::CompletionReservation, State::ChildCount>& notifications,
                std::index_sequence<Indices...>) noexcept
        {
            ([&] {
                try
                {
                    auto& ctx                            = state->childContexts[Indices];
                    std::get<Indices>(state->operations) = Spawn(ctx, std::invoke(std::get<Indices>(*state->factories), ctx));
                } catch (...)
                {
                    state->creationFailures[Indices] = State::ExceptionFault();
                    notifications[Indices].Schedule();
                    return;
                }
                if (!detail::OperationAccess::ObserveReusable(std::get<Indices>(state->operations), notifications[Indices]))
                    std::terminate();
            }(),
             ...);
        }
    }// namespace detail::when_any

    /// @brief Runs child factories, cancels every loser, drains them, and returns the winning argument index.
    /// @details Each factory receives a child-specific context linked to the parent context. The first terminal child
    /// selects the outcome, after which cancellation is requested for every loser. The combinator returns only after
    /// every child and its cancellation publication finish, so a slow or non-cooperative loser adds completion latency.
    /// A winning domain error, cancellation, or fault is propagated after the drain. Terminal notifications are
    /// reserved before invoking any factory; admission failure starts no children. Joining uses the parent's
    /// existing continuation reservation and remains valid during executor shutdown. Factories are retained until join.
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

        if (!(co_await detail::when_any::TrackedTask {}))
            co_return OutCompletion::Faulted(detail::MakeSchedulingFault(
                    AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected));

        using State = detail::when_any::SharedState<E, TFactories...>;
        std::shared_ptr<State>                                                state;
        std::array<NGIN::Execution::CompletionReservation, State::ChildCount> notifications;
        try
        {
            state          = std::make_shared<State>(ctx, std::move(factories)...);
            auto admission = detail::when_any::ReserveNotifications<E>(
                    state, notifications, std::index_sequence_for<TFactories...> {});
            if (!admission)
                co_return OutCompletion::Faulted(detail::MakeSchedulingFault(
                        AsyncFaultCode::SchedulerDispatchFailed, admission.error()));
        } catch (const std::bad_alloc&)
        {
            co_return OutCompletion::Faulted(detail::MakeSchedulingFault(
                    AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::ResourceExhausted));
        }
        detail::when_any::SpawnChildren(state, notifications, std::index_sequence_for<TFactories...> {});
        co_return co_await detail::when_any::AwaitAll<State> {state};
    }
}// namespace NGIN::Async
