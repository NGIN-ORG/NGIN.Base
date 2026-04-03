/// @file Sleep.hpp
/// @brief Sleep utilities using NGIN::Units and platform APIs (no std::chrono).
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Time
{
    namespace detail
    {
        NGIN_BASE_API void SleepForNanoseconds(UInt64 nanoseconds) noexcept;
    }

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
        detail::SleepForNanoseconds(ns);
    }
}// namespace NGIN::Time
