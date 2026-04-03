#include <NGIN/Time/MonotonicClock.hpp>

#include <time.h>

namespace NGIN::Time
{
    TimePoint MonotonicClock::Now() noexcept
    {
        timespec ts {};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        const auto nanos = static_cast<UInt64>(ts.tv_sec) * 1'000'000'000ull + static_cast<UInt64>(ts.tv_nsec);
        return TimePoint::FromNanoseconds(nanos);
    }
}// namespace NGIN::Time
