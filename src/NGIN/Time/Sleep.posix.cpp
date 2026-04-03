#include <NGIN/Time/Sleep.hpp>

#include <time.h>

namespace NGIN::Time::detail
{
    void SleepForNanoseconds(UInt64 nanoseconds) noexcept
    {
        timespec request {};
        request.tv_sec  = static_cast<time_t>(nanoseconds / 1'000'000'000ull);
        request.tv_nsec = static_cast<long>(nanoseconds % 1'000'000'000ull);
        ::nanosleep(&request, nullptr);
    }
}// namespace NGIN::Time::detail
