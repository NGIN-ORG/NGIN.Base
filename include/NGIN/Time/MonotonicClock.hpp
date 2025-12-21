/// @file MonotonicClock.hpp
/// @brief Platform monotonic clock.
#pragma once

#include <NGIN/Time/TimePoint.hpp>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <time.h>
#endif

namespace NGIN::Time
{
    /// @brief Monotonic clock based on platform high-resolution timers.
    struct MonotonicClock final
    {
        [[nodiscard]] static inline TimePoint Now() noexcept
        {
#if defined(_WIN32)
            static LARGE_INTEGER frequency = [] {
                LARGE_INTEGER f {};
                ::QueryPerformanceFrequency(&f);
                return f;
            }();

            LARGE_INTEGER counter {};
            ::QueryPerformanceCounter(&counter);

            const auto ticks     = static_cast<UInt64>(counter.QuadPart);
            const auto freqTicks = static_cast<UInt64>(frequency.QuadPart);
            const auto nanos     = (ticks * 1'000'000'000ull) / freqTicks;
            return TimePoint::FromNanoseconds(nanos);
#else
            timespec ts {};
            ::clock_gettime(CLOCK_MONOTONIC, &ts);
            const auto nanos = static_cast<UInt64>(ts.tv_sec) * 1'000'000'000ull + static_cast<UInt64>(ts.tv_nsec);
            return TimePoint::FromNanoseconds(nanos);
#endif
        }
    };
}// namespace NGIN::Time

