#pragma once

#include <NGIN/Units.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>

using namespace NGIN::Units;

namespace NGIN
{
    /// @brief A simple timer class for measuring time
    class Timer
    {
    public:
        /// @brief Start the timer
        inline void Start() noexcept
        {
            start     = NGIN::Time::MonotonicClock::Now();
            isRunning = true;
        }

        /// @brief Stop the timer
        inline void Stop() noexcept
        {
            end       = NGIN::Time::MonotonicClock::Now();
            isRunning = false;
        }

        /// @brief Reset the timer
        inline void Reset() noexcept
        {
            start = NGIN::Time::MonotonicClock::Now();
        }

        /// @brief Get the elapsed time in the specified time unit (defaults to Seconds)
        /// @tparam TUnit Time unit to return (must satisfy QuantityOf<TIME>)
        /// @brief Get elapsed time in specified unit (defaults to Seconds)
        template<typename TUnit = Seconds>
            requires QuantityOf<TIME, TUnit>
        inline TUnit GetElapsed() const noexcept
        {
            using ValueT = typename TUnit::ValueType;
            const auto nowNanos = (isRunning ? NGIN::Time::MonotonicClock::Now() : end).ToNanoseconds();
            const auto diffNanos = nowNanos - start.ToNanoseconds();
            const auto secs = static_cast<ValueT>(diffNanos) / static_cast<ValueT>(1'000'000'000.0);
            return UnitCast<TUnit>(Seconds(secs));
        }


    private:
        /// @brief The start time
        NGIN::Time::TimePoint start = {};
        /// @brief The end time
        NGIN::Time::TimePoint end = {};
        /// @brief Is the timer running
        bool isRunning = false;
    };
}// namespace NGIN
