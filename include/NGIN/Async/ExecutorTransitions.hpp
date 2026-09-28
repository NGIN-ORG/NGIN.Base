/// @file ExecutorTransitions.hpp
/// @brief Explicit startup, completion, and current-executor transitions.
#pragma once

#include <NGIN/Async/Task.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        template<typename Promise>
        std::coroutine_handle<> FailTransition(std::coroutine_handle<Promise> awaiting,
                                               NGIN::Execution::ScheduleError error) noexcept
        {
            awaiting.promise().SetFault(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, error));
            awaiting.promise().MarkFinishedAndResume(awaiting);
            return std::noop_coroutine();
        }

        struct SwitchExecutorAwaiter final
        {
            NGIN::Execution::ExecutorRef target;
            bool                         await_ready() const noexcept { return false; }
            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const noexcept
            {
                auto& promise = awaiting.promise();
                auto  result  = promise.RebindExecutor(target);
                if (!result)
                    return FailTransition(awaiting, result.error());
                if (promise.CanTransfer())
                    return awaiting;
                promise.QueueContinuation(NGIN::Execution::WorkItem(awaiting));
                return std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };

        struct YieldCurrentAwaiter final
        {
            bool await_ready() const noexcept { return false; }
            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) const
            {
                TaskContext current(awaiting.promise().m_environment);
                return current.YieldNow().await_suspend(awaiting);
            }
            void await_resume() const noexcept {}
        };

        struct TerminalTransition final
        {
            TaskExecutionState*                    state {};
            NGIN::Execution::ExecutorRef           target;
            NGIN::Execution::CompletionReservation ticket;
            bool                                   await_ready() const noexcept { return false; }
            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                awaiting.promise().m_environment.executor = target;
                state->Activate(target, std::move(ticket));
                if (awaiting.promise().CanTransfer())
                    return awaiting;
                awaiting.promise().QueueContinuation(NGIN::Execution::WorkItem(awaiting));
                return std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };

        struct PrepareTerminalTransition final
        {
            NGIN::Execution::ExecutorRef target;
            TerminalTransition           prepared {};
            bool                         await_ready() const noexcept { return false; }
            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                auto& state  = awaiting.promise().ChainState();
                auto  ticket = state.ReserveFor(target);
                if (!ticket)
                    return FailTransition(awaiting, ticket.error());
                prepared.state  = &state;
                prepared.target = target;
                prepared.ticket = std::move(*ticket);
                return awaiting;
            }
            TerminalTransition await_resume() noexcept { return std::move(prepared); }
        };

        template<typename E>
        struct StoreVoidCompletion final
        {
            Completion<void, E> completion;
            bool                await_ready() const noexcept { return false; }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> awaiting)
            {
                awaiting.promise().SetCompletion(std::move(completion));
                return false;
            }
            void await_resume() const noexcept {}
        };
    }// namespace detail

    /// Changes the current computation's executor; other logical properties survive.
    [[nodiscard]] inline detail::SwitchExecutorAwaiter SwitchTo(NGIN::Execution::ExecutorRef executor) noexcept
    {
        return {executor};
    }

    /// Always defers on the active logical executor, subject to cooperative cancellation.
    [[nodiscard]] inline detail::YieldCurrentAwaiter YieldNow() noexcept
    {
        return {};
    }

    /// Admits target delivery before starting the child. Ordinary completion does not restore the caller's executor.
    template<typename T, typename E>
    [[nodiscard]] Task<T, E> StartsOn(NGIN::Execution::ExecutorRef executor, Task<T, E> task)
    {
        co_await SwitchTo(executor);
        if constexpr (std::is_void_v<T>)
            co_await std::move(task);
        else
            co_return co_await std::move(task);
    }

    /// Preserves incoming startup and pre-admits target delivery for every terminal outcome.
    template<typename T, typename E>
    [[nodiscard]] Task<T, E> ContinuesOn(NGIN::Execution::ExecutorRef executor, Task<T, E> task)
    {
        auto                            transition = co_await detail::PrepareTerminalTransition {executor};
        std::optional<Completion<T, E>> completion;
#if NGIN_ASYNC_HAS_EXCEPTIONS
        try
        {
#endif
            completion.emplace(co_await task.AsCompletion());
#if NGIN_ASYNC_HAS_EXCEPTIONS
        } catch (...)
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            completion.emplace(Completion<T, E>::Faulted(std::move(fault)));
        }
#endif
        co_await transition;
        if constexpr (std::is_void_v<T>)
            co_await detail::StoreVoidCompletion<E> {std::move(*completion)};
        else
            co_return std::move(*completion);
    }
}// namespace NGIN::Async
