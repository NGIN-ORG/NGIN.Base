/// @file TimePoint.hpp
/// @brief Monotonic time point for scheduling and timers (nanosecond ticks).
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Time
{
    /// @brief Opaque monotonic time point expressed as nanoseconds since an unspecified epoch.
    struct TimePoint final
    {
        /// @brief Constructs the epoch value of this monotonic clock domain.
        constexpr TimePoint() noexcept = default;

        /// @brief Constructs a time point from nanoseconds since the unspecified epoch.
        static constexpr TimePoint FromNanoseconds(UInt64 nanoseconds) noexcept
        {
            return TimePoint(nanoseconds);
        }

        /// @brief Returns nanoseconds since the unspecified epoch.
        [[nodiscard]] constexpr UInt64 ToNanoseconds() const noexcept
        {
            return m_nanoseconds;
        }

        /// @brief Returns whether two time points represent the same tick.
        friend constexpr bool operator==(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds == b.m_nanoseconds;
        }
        /// @brief Returns whether two time points represent different ticks.
        friend constexpr bool operator!=(TimePoint a, TimePoint b) noexcept
        {
            return !(a == b);
        }
        /// @brief Returns whether `a` precedes `b`.
        friend constexpr bool operator<(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds < b.m_nanoseconds;
        }
        /// @brief Returns whether `a` precedes or equals `b`.
        friend constexpr bool operator<=(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds <= b.m_nanoseconds;
        }
        /// @brief Returns whether `a` follows `b`.
        friend constexpr bool operator>(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds > b.m_nanoseconds;
        }
        /// @brief Returns whether `a` follows or equals `b`.
        friend constexpr bool operator>=(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds >= b.m_nanoseconds;
        }

    private:
        constexpr explicit TimePoint(UInt64 nanoseconds) noexcept
            : m_nanoseconds(nanoseconds)
        {
        }

        UInt64 m_nanoseconds {0};
    };
}// namespace NGIN::Time
