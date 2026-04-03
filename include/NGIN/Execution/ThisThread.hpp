/// @file ThisThread.hpp
/// @brief Calling-thread utilities (NGIN replacement for std::this_thread).
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Units.hpp>

#include <cstdint>
#include <string_view>

namespace NGIN::Execution::ThisThread
{
    using ThreadId = NGIN::UInt64;

    [[nodiscard]] NGIN_BASE_API std::uint32_t HardwareConcurrency() noexcept;
    [[nodiscard]] NGIN_BASE_API ThreadId      GetId() noexcept;
    NGIN_BASE_API void                        YieldNow() noexcept;

    inline void RelaxCpu() noexcept
    {
        NGIN_CPU_RELAX();
    }

    template<typename TUnit>
        requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
    inline void SleepFor(const TUnit& duration) noexcept
    {
        NGIN::Time::SleepFor(duration);
    }

    inline void SleepUntil(NGIN::Time::TimePoint timePoint) noexcept
    {
        const auto now = NGIN::Time::MonotonicClock::Now();
        if (timePoint <= now)
        {
            return;
        }
        const auto deltaNs = timePoint.ToNanoseconds() - now.ToNanoseconds();
        NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(deltaNs)));
    }

    [[nodiscard]] NGIN_BASE_API bool SetName(std::string_view name) noexcept;
    [[nodiscard]] NGIN_BASE_API bool SetAffinity(UInt64 affinityMask) noexcept;
    [[nodiscard]] NGIN_BASE_API bool SetPriority(int value) noexcept;
}// namespace NGIN::Execution::ThisThread
