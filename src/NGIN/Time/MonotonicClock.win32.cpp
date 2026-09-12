#include "PerformanceCounter.hpp"
#include <NGIN/Time/MonotonicClock.hpp>

#include <windows.h>

namespace NGIN::Time
{
    TimePoint MonotonicClock::Now() noexcept
    {
        static LARGE_INTEGER frequency = [] {
            LARGE_INTEGER value {};
            ::QueryPerformanceFrequency(&value);
            return value;
        }();

        LARGE_INTEGER counter {};
        ::QueryPerformanceCounter(&counter);

        const auto ticks     = static_cast<UInt64>(counter.QuadPart);
        const auto freqTicks = static_cast<UInt64>(frequency.QuadPart);
        const auto nanos     = detail::PerformanceCounterNanoseconds(ticks, freqTicks);
        return TimePoint::FromNanoseconds(nanos);
    }
}// namespace NGIN::Time
