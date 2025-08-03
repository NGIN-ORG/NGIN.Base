#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <NGIN/Math/Ratio.hpp>
#include <ostream>

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
    using Nanoseconds  = Unit<TIME, Ratio<1, 1000000000>, double>;
    using Minutes      = Unit<TIME, Ratio<60, 1>, double>;
    using Hours        = Unit<TIME, Ratio<3600, 1>, double>;
    using Days         = Unit<TIME, Ratio<86400, 1>, double>;
    using Weeks        = Unit<TIME, Ratio<604800, 1>, double>;
    using Fortnights   = Unit<TIME, Ratio<1209600, 1>, double>;

    // Example: SI length units
    using Meters      = Unit<LENGTH, Ratio<1, 1>, double>;
    using Kilometers  = Unit<LENGTH, Ratio<1000, 1>, double>;
    using Centimeters = Unit<LENGTH, Ratio<1, 100>, double>;
    using Millimeters = Unit<LENGTH, Ratio<1, 1000>, double>;

    // Example: SI mass units
    using Kilograms  = Unit<MASS, Ratio<1, 1>, double>;
    using Grams      = Unit<MASS, Ratio<1, 1000>, double>;
    using Milligrams = Unit<MASS, Ratio<1, 1000000>, double>;

    // Example: SI current units
    using Amperes      = Unit<CURRENT, Ratio<1, 1>, double>;
    using Milliamperes = Unit<CURRENT, Ratio<1, 1000>, double>;

    // Example: SI temperature units
    using Kelvin  = Unit<TEMPERATURE, Ratio<1, 1>, double>;
    using Celsius = Unit<TEMPERATURE, Ratio<1, 1>, double>;// For now, treat as same as Kelvin for structure
    // Specializations for SI current units

    // Example: Derived unit
    using Velocity = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), Ratio<1, 1>, double>;

    // User extension example
    // struct MyUnit : Unit<QuantityExponents{...}, Ratio<...>, float> { ... };
    // constexpr MyUnit operator"" _my(long double v) { return MyUnit(static_cast<float>(v)); }

    // --- UnitTraits for metadata (symbol, name, etc.) ---
    template<typename UnitT>
    struct UnitTraits;


    // Specializations for SI time units
    // Specializations for SI length units
    template<>
    struct UnitTraits<Meters>
    {
        static constexpr const char* symbol = "m";
        static constexpr const char* name   = "meters";
    };
    template<>
    struct UnitTraits<Kilometers>
    {
        static constexpr const char* symbol = "km";
        static constexpr const char* name   = "kilometers";
    };
    template<>
    struct UnitTraits<Centimeters>
    {
        static constexpr const char* symbol = "cm";
        static constexpr const char* name   = "centimeters";
    };
    template<>
    struct UnitTraits<Millimeters>
    {
        static constexpr const char* symbol = "mm";
        static constexpr const char* name   = "millimeters";
    };

    // Specializations for SI mass units
    template<>
    struct UnitTraits<Kilograms>
    {
        static constexpr const char* symbol = "kg";
        static constexpr const char* name   = "kilograms";
    };
    template<>
    struct UnitTraits<Grams>
    {
        static constexpr const char* symbol = "g";
        static constexpr const char* name   = "grams";
    };
    template<>
    struct UnitTraits<Milligrams>
    {
        static constexpr const char* symbol = "mg";
        static constexpr const char* name   = "milligrams";
    };

    template<>
    struct UnitTraits<Seconds>
    {
        static constexpr const char* symbol = "s";
        static constexpr const char* name   = "seconds";
    };
    template<>
    struct UnitTraits<Milliseconds>
    {
        static constexpr const char* symbol = "ms";
        static constexpr const char* name   = "milliseconds";
    };
    template<>
    struct UnitTraits<Microseconds>
    {
        static constexpr const char* symbol = "us";
        static constexpr const char* name   = "microseconds";
    };
    template<>
    struct UnitTraits<Nanoseconds>
    {
        static constexpr const char* symbol = "ns";
        static constexpr const char* name   = "nanoseconds";
    };
    template<>
    struct UnitTraits<Minutes>
    {
        static constexpr const char* symbol = "min";
        static constexpr const char* name   = "minutes";
    };
    template<>
    struct UnitTraits<Hours>
    {
        static constexpr const char* symbol = "h";
        static constexpr const char* name   = "hours";
    };
    template<>
    struct UnitTraits<Days>
    {
        static constexpr const char* symbol = "d";
        static constexpr const char* name   = "days";
    };
    template<>
    struct UnitTraits<Weeks>
    {
        static constexpr const char* symbol = "wk";
        static constexpr const char* name   = "weeks";
    };
    template<>
    struct UnitTraits<Fortnights>
    {
        static constexpr const char* symbol = "fn";
        static constexpr const char* name   = "fortnights";
    };

    template<>
    struct UnitTraits<Amperes>
    {
        static constexpr const char* symbol = "A";
        static constexpr const char* name   = "amperes";
    };
    template<>
    struct UnitTraits<Milliamperes>
    {
        static constexpr const char* symbol = "mA";
        static constexpr const char* name   = "milliamperes";
    };

    // Specializations for SI temperature units
    template<>
    struct UnitTraits<Kelvin>
    {
        static constexpr const char* symbol = "K";
        static constexpr const char* name   = "kelvin";
    };
    template<>
    struct UnitTraits<Celsius>
    {
        static constexpr const char* symbol = "°C";
        static constexpr const char* name   = "celsius";
    };


    // Output streaming for units with or without UnitTraits (C++20 requires)
    template<QuantityExponents Q, typename RatioT, typename ValueT>
    std::ostream& operator<<(std::ostream& os, const Unit<Q, RatioT, ValueT>& u)
        requires requires { UnitTraits<Unit<Q, RatioT, ValueT>>::symbol; }
    {
        return os << u.GetValue() << ' ' << UnitTraits<Unit<Q, RatioT, ValueT>>::symbol;
    }

    template<QuantityExponents Q, typename RatioT, typename ValueT>
    std::ostream& operator<<(std::ostream& os, const Unit<Q, RatioT, ValueT>& u)
        requires(!requires { UnitTraits<Unit<Q, RatioT, ValueT>>::symbol; })
    {
        return os << u.GetValue();
    }

}// namespace NGIN::Units
