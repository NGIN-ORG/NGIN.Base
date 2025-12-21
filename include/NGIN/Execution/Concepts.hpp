/// @file Concepts.hpp
/// @brief Concepts for executors/schedulers in NGIN::Execution.
#pragma once

#include <concepts>
#include <utility>

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Execution
{
    /// @brief Minimal executor surface used by the async runtime.
    ///
    /// Required operations:
    /// - `Execute(WorkItem)` for immediate scheduling
    /// - `ExecuteAt(WorkItem, TimePoint)` for time-based scheduling
    template<typename T>
    concept ExecutorConcept = requires(T& executor, WorkItem item, NGIN::Time::TimePoint resumeAt) {
        { executor.Execute(std::move(item)) } noexcept;
        executor.ExecuteAt(std::move(item), resumeAt);
    };

    /// @brief Optional capability: cooperative "pump" execution on the calling thread.
    template<typename T>
    concept CooperativeExecutorConcept =
            ExecutorConcept<T> && requires(T& executor) {
                { executor.RunOne() } noexcept -> std::same_as<bool>;
                { executor.RunUntilIdle() } noexcept;
            };
}// namespace NGIN::Execution
