#pragma once
#include <chrono>

#include <NGIN/Units.hpp>
#include <NGIN/Primitives.hpp>

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
            start     = Clock::now();
            isRunning = true;
        }

        /// @brief Stop the timer
        inline void Stop() noexcept
        {
            end       = Clock::now();
            isRunning = false;
        }

        /// @brief Reset the timer
        inline void Reset() noexcept
        {
            start = Clock::now();
        }

        /// @brief Get the elapsed time in the specified time unit (defaults to Seconds)
        /// @tparam TUnit Time unit to return (must satisfy QuantityOf<TIME>)
        /// @brief Get elapsed time in specified unit (defaults to Seconds)
        template<typename TUnit = Seconds>
            requires QuantityOf<TIME, TUnit>
        inline TUnit GetElapsed() const noexcept
        {
            using ValueT = typename TUnit::ValueType;
            // Determine current or stored end time
            const auto now = isRunning ? Clock::now() : end;
            // Compute duration since start
            auto diff = now - start;
            // Convert to seconds
            ValueT secs = std::chrono::duration<ValueT>(diff).count();
            // Convert to desired unit
            return UnitCast<TUnit>(Seconds(secs));
        }


    private:
        /// Clock type for timing
        using Clock = std::chrono::steady_clock;
        /// @brief The start time
        std::chrono::time_point<Clock> start = {};
        /// @brief The end time
        std::chrono::time_point<Clock> end = {};
        /// @brief Is the timer running
        bool isRunning = false;
    };
}// namespace NGIN