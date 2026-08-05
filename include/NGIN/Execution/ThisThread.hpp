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
    /// @brief Numeric identifier for an operating-system thread.
    using ThreadId = NGIN::UInt64;

    /// @brief Returns the implementation's estimate of available hardware threads.
    [[nodiscard]] NGIN_EXECUTION_API std::uint32_t HardwareConcurrency() noexcept;
    /// @brief Returns the calling thread's numeric identifier.
    [[nodiscard]] NGIN_EXECUTION_API ThreadId GetId() noexcept;
    /// @brief Yields the calling thread's remaining time slice.
    NGIN_EXECUTION_API void YieldNow() noexcept;

    /// @brief Issues an architecture-specific spin-wait hint.
    inline void RelaxCpu() noexcept
    {
        NGIN_CPU_RELAX();
    }

    /// @brief Suspends the calling thread for a time quantity.
    template<typename TUnit>
        requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
    inline void SleepFor(const TUnit& duration) noexcept
    {
        NGIN::Time::SleepFor(duration);
    }

    /// @brief Suspends the calling thread until a monotonic time point.
    inline void SleepUntil(NGIN::Time::TimePoint timePoint) noexcept
    {
        const NGIN::Time::TimePoint now = NGIN::Time::MonotonicClock::Now();
        if (timePoint <= now)
        {
            return;
        }
        const NGIN::UInt64 deltaNs = timePoint.ToNanoseconds() - now.ToNanoseconds();
        NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(deltaNs)));
    }

    /// @brief Attempts to set the calling thread's diagnostic name.
    [[nodiscard]] NGIN_EXECUTION_API bool SetName(std::string_view name) noexcept;
    /// @brief Attempts to restrict the calling thread to a processor affinity mask.
    [[nodiscard]] NGIN_EXECUTION_API bool SetAffinity(UInt64 affinityMask) noexcept;
    /// @brief Attempts to set the calling thread's platform priority.
    [[nodiscard]] NGIN_EXECUTION_API bool SetPriority(int value) noexcept;
}// namespace NGIN::Execution::ThisThread
