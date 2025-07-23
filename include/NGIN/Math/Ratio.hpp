#pragma once
#include <NGIN/Primitives.hpp>
#include <cstdint>

namespace NGIN::Math
{

    /// @brief Compile-time rational number for scaling and unit conversions.
    /// @tparam Numerator The numerator of the ratio.
    /// @tparam Denominator The denominator of the ratio.
    /// @tparam IntType The integer type used for numerator and denominator (default: Int64).
    /// @tparam ValueType The floating-point type used for value representation (default: F64).
    ///
    /// Provides constexpr value computation and type-safe rational arithmetic.
    template<int64_t Numerator, int64_t Denominator, typename IntType = Int64, typename ValueType = F64>
    struct Ratio
    {
        static_assert(Denominator != 0, "Denominator must not be zero");
        static constexpr IntType NUMERATOR   = Numerator;
        static constexpr IntType DENOMINATOR = Denominator;

        /// @brief Returns the value of the ratio as double (constexpr).
        static constexpr ValueType Value() noexcept
        {
            return static_cast<ValueType>(Numerator) / static_cast<ValueType>(Denominator);
        }
    };

}// namespace NGIN::Math
