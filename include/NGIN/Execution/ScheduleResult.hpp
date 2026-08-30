/// @file ScheduleResult.hpp
/// @brief Explicit result model for immediate and timed work submission.
#pragma once

#include <cstdint>
#include <expected>

namespace NGIN::Execution
{
    /// @brief Reason that an executor did not accept a work item.
    enum class ScheduleError : std::uint8_t
    {
        /// @brief The executor reference has no dispatch target.
        InvalidExecutor,
        /// @brief The scheduler has begun or completed shutdown.
        Stopped,
        /// @brief Scheduler policy rejected the work item.
        Rejected,
        /// @brief Queue, timer, or callable storage could not be allocated.
        ResourceExhausted,
    };

    /// @brief Allocation-free success or a concrete scheduling failure.
    using ScheduleResult = std::expected<void, ScheduleError>;
}// namespace NGIN::Execution
