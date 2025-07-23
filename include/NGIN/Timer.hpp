#pragma once
#include <chrono>

#include <NGIN/Units.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN
{
    /// @brief A simple timer class for measuring time
    class Timer
    {
    public:
        /// @brief Start the timer
        inline void Start() noexcept
        {
            start     = std::chrono::high_resolution_clock::now();
            isRunning = true;
        }

        /// @brief Stop the timer
        inline void Stop() noexcept
        {
            end       = std::chrono::high_resolution_clock::now();
            isRunning = false;
        }

        /// @brief Reset the timer
        inline void Reset() noexcept
        {
            start = std::chrono::high_resolution_clock::now();
        }

        /// @brief  Get the elapsed time
        /// @tparam T The Unit of time to return the elapsed time in
        /// @return  The elapsed time
        template<typename T = Seconds>
        inline T GetElapsed() const noexcept
        {
            if (isRunning)
            {
                const auto now = std::chrono::high_resolution_clock::now();
                return UnitCast<T>(Seconds(std::chrono::duration_cast<std::chrono::duration<double>>(now - start).count()));
            }
            else
            {
                return UnitCast<T>(Seconds(std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count()));
            }
        }

    private:
        /// @brief The start time
        std::chrono::time_point<std::chrono::high_resolution_clock> start = {};
        /// @brief The end time
        std::chrono::time_point<std::chrono::high_resolution_clock> end = {};
        /// @brief Is the timer running
        bool isRunning = false;
    };
}// namespace NGIN