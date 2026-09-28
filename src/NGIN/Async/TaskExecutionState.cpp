#include <NGIN/Async/detail/TaskExecutionState.hpp>

#include <new>
#include <type_traits>
#include <utility>

namespace NGIN::Async::detail
{
    TaskExecutionStateOwner TaskExecutionState::Create(TaskContinuationState      initial,
                                                       std::pmr::memory_resource* resource) noexcept
    {
        static_assert(std::is_nothrow_default_constructible_v<TaskExecutionState>);
        if (!resource)
            return {};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try
        {
#endif
            auto* storage     = resource->allocate(sizeof(TaskExecutionState), alignof(TaskExecutionState));
            auto* state       = std::construct_at(static_cast<TaskExecutionState*>(storage));
            state->m_resource = resource;
            state->m_continuationState.store(initial, std::memory_order_relaxed);
            return TaskExecutionStateOwner {state};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        } catch (...)
        {
            return {};
        }
#endif
    }

    void TaskExecutionStateDeleter::operator()(TaskExecutionState* state) const noexcept
    {
        auto* resource = state->m_resource;
        std::destroy_at(state);
        resource->deallocate(state, sizeof(TaskExecutionState), alignof(TaskExecutionState));
    }

    std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
    TaskExecutionState::ReserveFor(NGIN::Execution::ExecutorRef executor) noexcept
    {
        return executor.ReserveCompletion(NGIN::Execution::WorkItem([this] {
            auto work   = std::move(m_pendingContinuation);
            m_chainWork = 0;
            work.Invoke();
        }));
    }

    void TaskExecutionState::Activate(NGIN::Execution::ExecutorRef           executor,
                                      NGIN::Execution::CompletionReservation ticket) noexcept
    {
        m_executor         = executor;
        m_taskContinuation = std::move(ticket);
    }

    NGIN::Execution::ScheduleResult TaskExecutionState::Rebind(NGIN::Execution::ExecutorRef executor) noexcept
    {
        if (executor == m_executor && m_taskContinuation.IsValid())
            return {};
        auto ticket = ReserveFor(executor);
        if (!ticket)
            return std::unexpected(ticket.error());
        Activate(executor, std::move(*ticket));
        return {};
    }

    NGIN::Execution::ScheduleResult TaskExecutionState::Reserve(NGIN::Execution::ExecutorRef executor) noexcept
    {
        if (!executor.SupportsCompletionReservations())
        {
            m_executor = executor;
            return {};
        }
        return Rebind(executor);
    }
}// namespace NGIN::Async::detail
