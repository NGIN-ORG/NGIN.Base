#pragma once
#include <cstdint>

namespace NGIN::Math
{

    /**
 * @brief Compile-time rational number for scaling and unit conversions.
 *
 * @tparam Numerator The numerator of the ratio.
 * @tparam Denominator The denominator of the ratio.
 *
 * Provides constexpr value computation and type-safe rational arithmetic.
 */
    template<int64_t Numerator, int64_t Denominator>
    struct Ratio
    {
        static_assert(Denominator != 0, "Denominator must not be zero");
        static constexpr int64_t numerator   = Numerator;
        static constexpr int64_t denominator = Denominator;

        /// @brief Returns the value of the ratio as double (constexpr).
        static constexpr double Value() noexcept
        {
            return static_cast<double>(Numerator) / static_cast<double>(Denominator);
        }
    };

}// namespace NGIN::Math
