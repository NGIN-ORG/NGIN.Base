/// @file WithDeadline.hpp
/// @brief Inherited monotonic deadlines with joined inner work and timer cleanup.
#pragma once
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/detail/DeadlineState.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        struct DeadlineEnvironment final
        {
            AsyncEnvironment environment;
            bool             tracked {};
            bool             await_ready() const noexcept { return false; }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
            {
                tracked = handle.promise().HasContinuation();
                if (tracked)
                    handle.promise().m_environment = environment;
                return false;
            }
            bool await_resume() const noexcept { return tracked; }
        };
        struct JoinDeadline final
        {
            DeadlineState& state;
            bool           await_ready() const noexcept { return state.IsRetired(); }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
            {
                handle.promise().RetainFrameReference();
                if (state.Install(&handle.promise(), NGIN::Execution::WorkItem([handle] {
                        PromiseRuntimeCommon::ResumeRetained(handle);
                    })))
                    return true;
                handle.promise().ReleaseFrameReference(handle);
                return false;
            }
            void await_resume() const noexcept {}
        };

        template<typename T, typename E>
        Task<T, E> RunDeadline(NGIN::Time::TimePoint deadline, Task<T, E> task)
        {
            auto                            environment = co_await CurrentEnvironment();
            std::optional<Completion<T, E>> outcome;
            std::shared_ptr<DeadlineState>  state;
            if (environment.cancellation.IsCancellationRequested())
                outcome.emplace(Completion<T, E>::Stopped(environment.cancellation.GetStopReason()));
            else if (!environment.deadline || deadline < *environment.deadline)
            {
                if (!environment.executor.SupportsTimerRemoval())
                    outcome.emplace(Completion<T, E>::Faulted(MakeSchedulingFault(
                            AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected)));
                else if (!(co_await DeadlineEnvironment {environment}))
                    outcome.emplace(Completion<T, E>::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidContinuationState)));
                else
                {
                    try
                    {
                        state = std::allocate_shared<DeadlineState>(
                                std::pmr::polymorphic_allocator<DeadlineState>(environment.resource), environment);
                    } catch (const std::bad_alloc&)
                    {
                        outcome.emplace(Completion<T, E>::Faulted(MakeSchedulingFault(
                                AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::ResourceExhausted)));
                    }
                    if (state)
                    {
                        const auto admitted = state->Start(deadline);
                        if (!admitted)
                            outcome.emplace(Completion<T, E>::Faulted(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, admitted.error())));
                        else
                        {
                            environment.deadline     = deadline;
                            environment.cancellation = state->Token();
                            (void) co_await DeadlineEnvironment {environment};
                        }
                    }
                }
            }
            if (!outcome)
            {
                try
                {
                    outcome.emplace(co_await task.AsCompletion());
                } catch (...)
                {
                    auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    fault.capturedException = std::current_exception();
#endif
                    outcome.emplace(Completion<T, E>::Faulted(std::move(fault)));
                }
            }
            if (state)
            {
                state->Close();
                co_await JoinDeadline {*state};
            }
            if constexpr (std::is_void_v<T>)
                co_await StoreVoidCompletion<E> {std::move(*outcome)};
            else
                co_return std::move(*outcome);
        }
    }// namespace detail

    /// Consumes a cold task. A stricter deadline owns one removable timer and linked
    /// stop source; later/equal inherited deadlines share their existing owner.
    /// Expiry requests Deadline stop, then waits for actual completion and cleanup.
    /// Already-completed inputs preserve their outcome without allocating a timer.
    template<typename T, typename E>
    [[nodiscard]] Task<T, E> WithDeadline(NGIN::Time::TimePoint deadline, Task<T, E> task)
    {
        if (task.IsCompleted())
            return std::move(task);
        return detail::RunDeadline(deadline, std::move(task));
    }
}// namespace NGIN::Async
