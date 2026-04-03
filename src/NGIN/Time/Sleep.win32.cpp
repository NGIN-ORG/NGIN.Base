#include <NGIN/Time/Sleep.hpp>

extern "C"
{
    __declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
}

namespace NGIN::Time::detail
{
    void SleepForNanoseconds(UInt64 nanoseconds) noexcept
    {
        const UInt64 milliseconds = (nanoseconds + 999'999ull) / 1'000'000ull;
        ::Sleep(static_cast<unsigned long>(milliseconds));
    }
}// namespace NGIN::Time::detail
