/// @file ThisFiber.hpp
/// @brief Calling-fiber utilities for stackful fibers.
#pragma once

#include <NGIN/Execution/Fiber.hpp>

namespace NGIN::Execution::ThisFiber
{
    [[nodiscard]] inline bool IsInFiber() noexcept
    {
        return NGIN::Execution::Fiber::IsInFiber();
    }

    inline void YieldNow() noexcept
    {
        NGIN::Execution::Fiber::YieldNow();
    }
}// namespace NGIN::Execution::ThisFiber
