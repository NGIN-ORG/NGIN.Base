#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <NGIN/Math/Ratio.hpp>

namespace NGIN::Units
{
    template<int64_t Numerator, int64_t Denominator>
    using Ratio = NGIN::Math::Ratio<Numerator, Denominator>;
    /// @brief Represents the exponents for SI base quantities.
    struct QuantityExponents
    {
        static constexpr std::size_t NUM_EXPONENTS = 7;
        std::array<int8_t, NUM_EXPONENTS> exponents {};// L, M, T, I, Θ, N, J
        constexpr bool operator==(const QuantityExponents& other) const noexcept
        {
            for (std::size_t k = 0; k < NUM_EXPONENTS; ++k)
                if (exponents[k] != other.exponents[k])
                    return false;
            return true;
        }
        constexpr bool operator!=(const QuantityExponents& other) const noexcept
        {
            return !(*this == other);
        }
    };

    /// @brief Compile-time addition of exponents
    constexpr QuantityExponents AddExponents(const QuantityExponents& a, const QuantityExponents& b) noexcept
    {
        QuantityExponents r {};
        for (std::size_t k = 0; k < QuantityExponents::NUM_EXPONENTS; ++k)
            r.exponents[k] = static_cast<int8_t>(a.exponents[k] + b.exponents[k]);
        return r;
    }
    /// @brief Compile-time subtraction of exponents
    constexpr QuantityExponents SubExponents(const QuantityExponents& a, const QuantityExponents& b) noexcept
    {
        QuantityExponents r {};
        for (std::size_t k = 0; k < QuantityExponents::NUM_EXPONENTS; ++k)
            r.exponents[k] = static_cast<int8_t>(a.exponents[k] - b.exponents[k]);
        return r;
    }

    // SI base quantities
    constexpr QuantityExponents LENGTH {1, 0, 0, 0, 0, 0, 0};
    constexpr QuantityExponents MASS {0, 1, 0, 0, 0, 0, 0};
    constexpr QuantityExponents TIME {0, 0, 1, 0, 0, 0, 0};
    constexpr QuantityExponents CURRENT {0, 0, 0, 1, 0, 0, 0};
    constexpr QuantityExponents TEMPERATURE {0, 0, 0, 0, 1, 0, 0};
    constexpr QuantityExponents AMOUNT {0, 0, 0, 0, 0, 1, 0};
    constexpr QuantityExponents LUMINOUS {0, 0, 0, 0, 0, 0, 1};

    /// @brief Core Unit type
    /// @tparam Q Quantity exponents
    /// @tparam RatioT Compile-time scaling ratio to base unit (NGIN::Ratio)
    /// @tparam ValueT Value type
    ///
    /// Example: Unit<LENGTH, Ratio<1,1>, double> for meters
    ///          Unit<TIME, Ratio<1,1000>, double> for milliseconds
    ///          Unit<AddExponents(LENGTH, TIME), Ratio<1,1>, double> for meter-seconds
    ///
    /// All arithmetic and conversions are constexpr/noexcept

    template<QuantityExponents Q, typename RatioT = Ratio<1, 1>, typename ValueT = double>
    class Unit
    {
    public:
        using ValueType                              = ValueT;
        static constexpr QuantityExponents Exponents = Q;
        using RatioToBase                            = RatioT;

        constexpr explicit Unit(ValueT value) noexcept : m_value(value) {}
        constexpr ValueT GetValue() const noexcept
        {
            return m_value;
        }

        // Arithmetic (same exponents)
        constexpr Unit operator+(const Unit& other) const noexcept
        {
            return Unit(m_value + other.m_value);
        }
        constexpr Unit operator-(const Unit& other) const noexcept
        {
            return Unit(m_value - other.m_value);
        }
        constexpr Unit operator*(ValueT scalar) const noexcept
        {
            return Unit(m_value * scalar);
        }
        constexpr Unit operator/(ValueT scalar) const noexcept
        {
            return Unit(m_value / scalar);
        }
        constexpr bool operator==(const Unit& other) const noexcept
        {
            return m_value == other.m_value;
        }
        constexpr bool operator!=(const Unit& other) const noexcept
        {
            return m_value != other.m_value;
        }

        // Unit algebra: multiplication/division yields new Unit type
        template<QuantityExponents Q2, typename R2>
        constexpr auto operator*(const Unit<Q2, R2, ValueT>& rhs) const noexcept
        {
            return Unit<AddExponents(Q, Q2), Ratio<1, 1>, ValueT>(m_value * rhs.GetValue());
        }
        template<QuantityExponents Q2, typename R2>
        constexpr auto operator/(const Unit<Q2, R2, ValueT>& rhs) const noexcept
        {
            return Unit<SubExponents(Q, Q2), Ratio<1, 1>, ValueT>(m_value / rhs.GetValue());
        }

        // Conversion to base unit
        constexpr ValueT ToBase() const noexcept
        {
            return m_value * RatioT::Value();
        }
        static constexpr ValueT FromBase(ValueT baseValue) noexcept
        {
            return baseValue / RatioT::Value();
        }

    private:
        ValueT m_value;
    };

    /// @brief UnitCast: convert between units of same exponents
    /// Compile-time error if exponents differ
    ///
    template<QuantityExponents Q, typename RTo, typename RFrom, typename ValueT>
    constexpr Unit<Q, RTo, ValueT> UnitCast(const Unit<Q, RFrom, ValueT>& from) noexcept
    {
        static_assert(Q == Q, "UnitCast: Exponents must match");
        // Convert to base, then to target
        ValueT base = from.ToBase();
        return Unit<Q, RTo, ValueT>(Unit<Q, RTo, ValueT>::FromBase(base));
    }

    /// @brief User-friendly UnitCast: UnitCast<ToUnit>(from)
    template<typename ToUnit, typename FromUnit>
    constexpr ToUnit UnitCast(const FromUnit& from) noexcept
    {
        static_assert(
                ToUnit::Exponents == FromUnit::Exponents,
                "UnitCast: Units must have the same quantity exponents");
        auto baseValue = FromUnit::RatioToBase::Value() * from.GetValue();
        auto toValue   = ToUnit::FromBase(baseValue);
        return ToUnit(toValue);
    }

    // Example: SI time units
    using Seconds      = Unit<TIME, Ratio<1, 1>, double>;
    using Milliseconds = Unit<TIME, Ratio<1, 1000>, double>;
    using Microseconds = Unit<TIME, Ratio<1, 1000000>, double>;
    using Minutes      = Unit<TIME, Ratio<60, 1>, double>;
    using Hours        = Unit<TIME, Ratio<3600, 1>, double>;
    using Days         = Unit<TIME, Ratio<86400, 1>, double>;

    // Example: Derived unit
    using Velocity = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), Ratio<1, 1>, double>;

    // User extension example
    // struct MyUnit : Unit<QuantityExponents{...}, Ratio<...>, float> { ... };
    // constexpr MyUnit operator"" _my(long double v) { return MyUnit(static_cast<float>(v)); }

}// namespace NGIN::Units
