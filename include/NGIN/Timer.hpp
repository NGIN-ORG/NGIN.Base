#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Units.hpp>

namespace NGIN
{
    /// @brief A simple timer class for measuring time
    class Timer
    {
    public:
        /// @brief Starts a new measurement, discarding any previous elapsed value.
        inline void Start() noexcept
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            m_start        = now;
            m_end          = now;
            m_isRunning    = true;
        }

        /// @brief Stops the active measurement. Calling Stop while stopped is a no-op.
        inline void Stop() noexcept
        {
            if (m_isRunning)
            {
                m_end       = NGIN::Time::MonotonicClock::Now();
                m_isRunning = false;
            }
        }

        /// @brief Clears the elapsed value and leaves the timer stopped.
        inline void Reset() noexcept
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            m_start        = now;
            m_end          = now;
            m_isRunning    = false;
        }

        /// @brief Reports whether the timer is currently measuring.
        [[nodiscard]] inline bool IsRunning() const noexcept
        {
            return m_isRunning;
        }

        /// @brief Gets the elapsed time in the specified time unit.
        /// @tparam TUnit Time unit to return (must satisfy QuantityOf<TIME>)
        template<typename TUnit = NGIN::Units::Seconds>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] inline TUnit GetElapsed() const noexcept
        {
            using ValueType      = typename TUnit::ValueType;
            const auto now       = m_isRunning ? NGIN::Time::MonotonicClock::Now() : m_end;
            const auto elapsedNs = now.ToNanoseconds() - m_start.ToNanoseconds();
            const auto seconds   = static_cast<ValueType>(elapsedNs) / static_cast<ValueType>(1'000'000'000.0);
            return NGIN::Units::UnitCast<TUnit>(NGIN::Units::Seconds(seconds));
        }

    private:
        NGIN::Time::TimePoint m_start {};
        NGIN::Time::TimePoint m_end {};
        bool                  m_isRunning {false};
    };
}// namespace NGIN
