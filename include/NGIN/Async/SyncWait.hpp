/// @file SyncWait.hpp
/// @brief Blocking root entry using structured task ownership.
#pragma once
#include <NGIN/Async/TaskScope.hpp>
#include <condition_variable>

namespace NGIN::Async
{
    /// @brief Runs a cold root and joins its execution before returning the completion.
    /// @details The executor must be driven independently. Do not block an executor
    /// thread needed by this root. Ready inputs require no executor or owner allocation.
    template<typename T, typename E>
    [[nodiscard]] Completion<T, E> SyncWait(TaskContext& ctx, Task<T, E>&& task)
    {
        using Result = Completion<T, E>;
        try
        {
            if (task.m_ready || !task.m_handle)
                return task.AsCompletion().await_resume();

            Task<T, E> input(std::move(task));

            struct Retirement final
            {
                std::mutex              mutex;
                std::condition_variable condition;
                bool                    done {};
                static void             Notify(void* raw) noexcept
                {
                    auto&           self = *static_cast<Retirement*>(raw);
                    std::lock_guard lock(self.mutex);
                    self.done = true;
                    self.condition.notify_one();
                }
            } retirement;
            TaskScope<E> owner(ctx.GetEnvironment(), 1);
            detail::RootScopeAccess::OnRetirement(owner, &Retirement::Notify, &retirement);
            auto admitted = owner.Spawn(std::move(input));
            if (!admitted)
            {
                if (admitted.error() == NGIN::Execution::ScheduleError::Stopped)
                    return Result::Stopped(ctx.GetCancellationToken().GetStopReason());
                return Result::Faulted(detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, admitted.error()));
            }
            owner.Close();
            {
                std::unique_lock lock(retirement.mutex);
                retirement.condition.wait(lock, [&] { return retirement.done; });
            }
            // No producer callback retains owner storage after the notification.
            // Finalize the already-retired scope on this thread, including captures.
            detail::RootScopeAccess::JoinRetired(owner);
            return admitted->TakeResult();
        } catch (const std::bad_alloc&)
        {
            return Result::Faulted(detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                               NGIN::Execution::ScheduleError::ResourceExhausted));
        } catch (...)
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            return Result::Faulted(std::move(fault));
        }
    }
}// namespace NGIN::Async
