/// @file TaskExecutionState.hpp
/// @brief Internal execution-boundary storage shared by a sequential task chain.
#pragma once

#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <memory_resource>

namespace NGIN::Async::detail
{
    enum class TaskContinuationState : std::uint8_t
    {
        Empty,
        Installing,
        Installed,
        Completed,
    };

    struct TaskExecutionState;
    struct NGIN_EXECUTION_API TaskExecutionStateDeleter final
    {
        void operator()(TaskExecutionState* state) const noexcept;
    };
    using TaskExecutionStateOwner = std::unique_ptr<TaskExecutionState, TaskExecutionStateDeleter>;

    /// Owned by a running root, generator producer, or executor boundary. All
    /// sequential borrowers retire before the owning coroutine releases it.
    struct NGIN_EXECUTION_API TaskExecutionState final
    {
        [[nodiscard]] static TaskExecutionStateOwner Create(
                TaskContinuationState      initial,
                std::pmr::memory_resource* resource = std::pmr::get_default_resource()) noexcept;
        [[nodiscard]] NGIN::Execution::ScheduleResult Reserve(NGIN::Execution::ExecutorRef executor) noexcept;
        [[nodiscard]] std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
                                                      ReserveFor(NGIN::Execution::ExecutorRef executor) noexcept;
        void                                          Activate(NGIN::Execution::ExecutorRef executor, NGIN::Execution::CompletionReservation ticket) noexcept;
        [[nodiscard]] NGIN::Execution::ScheduleResult Rebind(NGIN::Execution::ExecutorRef executor) noexcept;

        NGIN::Execution::ExecutorRef            m_executor {};
        std::pmr::memory_resource*              m_resource {};
        NGIN::Execution::CompletionReservation  m_taskContinuation {};
        NGIN::Execution::CompletionReservation  m_foreignContinuation {};
        NGIN::Execution::WorkItem               m_pendingContinuation {};
        NGIN::Execution::CompletionReservation* m_reusableObserver {};
        // Internal owner arbitration runs at terminal publication, before queued
        // observation and frame retirement. It must not resume user code.
        void (*m_terminalObserver)(void*) noexcept {};
        void*                              m_terminalContext {};
        std::atomic<unsigned>              m_submissionFlags {0};
        unsigned                           m_chainWork {};
        std::atomic<TaskContinuationState> m_continuationState {TaskContinuationState::Empty};
    };
}// namespace NGIN::Async::detail
