/// @file ThisFiber.hpp
/// @brief Calling-fiber utilities for stackful fibers.
#pragma once

#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/Fiber.hpp>

namespace NGIN::Execution::ThisFiber
{
    /// @brief True only when the calling thread is currently executing inside a running fiber.
    [[nodiscard]] inline bool IsInFiber() noexcept
    {
        return NGIN::Execution::Fiber::IsInFiber();
    }

    /// @brief True when the fiber runtime is initialized for the calling thread (even if not currently inside a fiber).
    [[nodiscard]] inline bool IsInitialized() noexcept
    {
        return NGIN::Execution::Fiber::IsMainFiberInitialized();
    }

    inline void YieldNow() noexcept
    {
        NGIN::Execution::Fiber::YieldNow();
    }
}// namespace NGIN::Execution::ThisFiber
