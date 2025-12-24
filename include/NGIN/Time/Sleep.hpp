/// @file Sleep.hpp
/// @brief Sleep utilities using NGIN::Units and platform APIs (no std::chrono).
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
}
#else
#include <time.h>
#endif

namespace NGIN::Time
{
    template<typename TUnit>
        requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
    [[nodiscard]] inline UInt64 ToNanosecondsCeil(const TUnit& duration) noexcept
    {
        const auto ns = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue();
        if (ns <= 0.0)
        {
            return 0;
        }
        const auto truncated = static_cast<UInt64>(ns);
        return (static_cast<double>(truncated) < ns) ? (truncated + 1ull) : truncated;
    }

    template<typename TUnit>
        requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
    inline void SleepFor(const TUnit& duration) noexcept
    {
        const UInt64 ns = ToNanosecondsCeil(duration);
        if (ns == 0)
        {
            return;
        }

#if defined(_WIN32)
        // Windows Sleep() is millisecond granularity; round up.
        const UInt64 ms = (ns + 999'999ull) / 1'000'000ull;
        ::Sleep(static_cast<unsigned long>(ms));
#else
        timespec req {};
        req.tv_sec  = static_cast<time_t>(ns / 1'000'000'000ull);
        req.tv_nsec = static_cast<long>(ns % 1'000'000'000ull);
        ::nanosleep(&req, nullptr);
#endif
    }
}// namespace NGIN::Time
