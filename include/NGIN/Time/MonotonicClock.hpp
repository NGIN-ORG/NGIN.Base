/// @file MonotonicClock.hpp
/// @brief Platform monotonic clock.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Time
{
    /// @brief Monotonic clock based on platform high-resolution timers.
    struct MonotonicClock final
    {
        [[nodiscard]] static NGIN_FOUNDATION_API TimePoint Now() noexcept;
    };
}// namespace NGIN::Time
