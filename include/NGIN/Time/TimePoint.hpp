/// @file TimePoint.hpp
/// @brief Monotonic time point for scheduling and timers (nanosecond ticks).
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Time
{
    /// @brief Opaque monotonic time point expressed as nanoseconds since an unspecified epoch.
    struct TimePoint final
    {
        constexpr TimePoint() noexcept = default;

        static constexpr TimePoint FromNanoseconds(UInt64 nanoseconds) noexcept
        {
            return TimePoint(nanoseconds);
        }

        [[nodiscard]] constexpr UInt64 ToNanoseconds() const noexcept
        {
            return m_nanoseconds;
        }

        friend constexpr bool operator==(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds == b.m_nanoseconds;
        }
        friend constexpr bool operator!=(TimePoint a, TimePoint b) noexcept
        {
            return !(a == b);
        }
        friend constexpr bool operator<(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds < b.m_nanoseconds;
        }
        friend constexpr bool operator<=(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds <= b.m_nanoseconds;
        }
        friend constexpr bool operator>(TimePoint a, TimePoint b) noexcept
        {
            return a.m_nanoseconds > b.m_nanoseconds;
        }
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

