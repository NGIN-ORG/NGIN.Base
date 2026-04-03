#include <NGIN/Time/MonotonicClock.hpp>

#include <Windows.h>

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
        const auto nanos     = (ticks * 1'000'000'000ull) / freqTicks;
        return TimePoint::FromNanoseconds(nanos);
    }
}// namespace NGIN::Time
