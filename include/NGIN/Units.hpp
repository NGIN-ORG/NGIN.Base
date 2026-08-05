/// @file Units.hpp
/// @brief Strongly typed physical quantities with compile-time dimensions and conversion policies.
#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>
#include <type_traits>

namespace NGIN::Units
{
    /// @brief Represents the exponents for SI base quantities.
    struct QuantityExponents
    {
        static constexpr std::size_t      NUM_EXPONENTS = 7;
        std::array<int8_t, NUM_EXPONENTS> exponents {};// L, M, T, I, Θ, N, J

        /// @brief Returns whether every SI base-quantity exponent matches.
        constexpr bool operator==(const QuantityExponents& other) const noexcept
        {
            for (std::size_t k = 0; k < NUM_EXPONENTS; ++k)
                if (exponents[k] != other.exponents[k])
                    return false;
            return true;
        }
        /// @brief Returns whether any SI base-quantity exponent differs.
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


    //=== C++20 Concepts ===

    /// @brief Concept: any Unit type (relaxed to allow const/ref types)
    template<typename U>
    concept UnitType = requires {
        typename std::remove_cvref_t<U>::ValueType;
        { std::remove_cvref_t<U>::Exponents } -> std::convertible_to<QuantityExponents>;
    };

    /// @brief Concept: Unit of specific quantity exponents (relaxed)
    template<QuantityExponents E, typename U>
    concept QuantityOf = UnitType<U> && (std::remove_cvref_t<U>::Exponents == E);


    // --- Conversion Policies ---
    /// @brief Linear conversion policy using the scale factor `Num / Den`.
    template<int64_t Num, int64_t Den = 1>
    struct RatioPolicy
    {
        /// @brief Converts a scaled value to its base-unit representation.
        template<typename ValueT>
        static constexpr ValueT ToBase(ValueT value) noexcept
        {
            return value * static_cast<ValueT>(Num) / static_cast<ValueT>(Den);
        }
        /// @brief Converts a base-unit value to this policy's scaled representation.
        template<typename ValueT>
        static constexpr ValueT FromBase(ValueT base) noexcept
        {
            return base * static_cast<ValueT>(Den) / static_cast<ValueT>(Num);
        }
    };

    /// @brief Affine conversion policy that adds a fixed offset when converting to base units.
    template<double Offset>
    struct OffsetPolicy
    {
        /// @brief Converts an offset value to its base-unit representation.
        template<typename ValueT>
        static constexpr ValueT ToBase(ValueT value) noexcept
        {
            return value + static_cast<ValueT>(Offset);
        }
        /// @brief Converts a base-unit value to this policy's offset representation.
        template<typename ValueT>
        static constexpr ValueT FromBase(ValueT base) noexcept
        {
            return base - static_cast<ValueT>(Offset);
        }
    };

    /// @brief Conversion policy between Fahrenheit and kelvin.
    struct FahrenheitToKelvinPolicy
    {
        /// @brief Converts Fahrenheit to kelvin.
        template<typename ValueT>
        static constexpr ValueT ToBase(ValueT value) noexcept
        {
            // F -> K
            return (value - 32.0) * 5.0 / 9.0 + 273.15;
        }
        /// @brief Converts kelvin to Fahrenheit.
        template<typename ValueT>
        static constexpr ValueT FromBase(ValueT base) noexcept
        {
            // K -> F
            return (base - 273.15) * 9.0 / 5.0 + 32.0;
        }
    };

    // --- Unit class with ConversionPolicy ---

    /// @brief Strongly typed physical quantity with a dimension and conversion policy.
    /// @tparam Q SI base-quantity exponents defining the dimension.
    /// @tparam ValueT Numeric representation.
    /// @tparam Policy Conversion between this unit and its base unit.
    template<
            QuantityExponents Q,
            typename ValueT = double,
            typename Policy = RatioPolicy<1, 1>>
    class Unit
    {
    public:
        /// @brief Numeric representation stored by the unit.
        using ValueType = ValueT;

        /// @brief Compile-time SI base-quantity exponents.
        static constexpr QuantityExponents Exponents = Q;

        /// @brief Policy used for base-unit conversion.
        using ConversionPolicy = Policy;

        /// @brief Constructs a unit from a value expressed in this unit's representation.
        constexpr explicit Unit(ValueT value) noexcept : m_value(value) {}

        /// @brief Returns the stored value without performing unit conversion.
        constexpr ValueT GetValue() const noexcept
        {
            return m_value;
        }

        /// @brief Adds a quantity expressed in the same unit type.
        constexpr Unit operator+(const Unit& other) const noexcept
        {
            return Unit(m_value + other.m_value);
        }
        /// @brief Subtracts a quantity expressed in the same unit type.
        constexpr Unit operator-(const Unit& other) const noexcept
        {
            return Unit(m_value - other.m_value);
        }
        /// @brief Multiplies the stored value by a scalar.
        constexpr Unit operator*(ValueT scalar) const noexcept
        {
            return Unit(m_value * scalar);
        }
        /// @brief Divides the stored value by a scalar.
        constexpr Unit operator/(ValueT scalar) const noexcept
        {
            return Unit(m_value / scalar);
        }
        /// @brief Compares stored values for equality.
        constexpr bool operator==(const Unit& other) const noexcept
        {
            return m_value == other.m_value;
        }
        /// @brief Compares stored values for inequality.
        constexpr bool operator!=(const Unit& other) const noexcept
        {
            return m_value != other.m_value;
        }

        /// @brief Multiplies quantities and adds their dimensional exponents.
        template<QuantityExponents Q2, typename P2>
        constexpr auto operator*(const Unit<Q2, ValueT, P2>& rhs) const noexcept
        {
            return Unit<AddExponents(Q, Q2), ValueT, RatioPolicy<1, 1>>(m_value * rhs.GetValue());
        }
        /// @brief Divides quantities and subtracts their dimensional exponents.
        template<QuantityExponents Q2, typename P2>
        constexpr auto operator/(const Unit<Q2, ValueT, P2>& rhs) const noexcept
        {
            return Unit<SubExponents(Q, Q2), ValueT, RatioPolicy<1, 1>>(m_value / rhs.GetValue());
        }

        /// @brief Converts the stored value to the dimension's base-unit representation.
        constexpr ValueT ToBase() const noexcept
        {
            return Policy::ToBase(m_value);
        }
        /// @brief Converts a base-unit value to this unit's representation.
        static constexpr ValueT FromBase(ValueT baseValue) noexcept
        {
            return Policy::FromBase(baseValue);
        }

    private:
        ValueT m_value;
    };


    /// @brief UnitCast: convert between units of same exponents
    /// Compile-time error if exponents differ

    // UnitCast: convert between units of the same dimensions, even if they use different ValueT or Policies
    template<
            QuantityExponents Q,
            typename ToValueT, typename ToPolicy,
            typename FromValueT, typename FromPolicy>
    constexpr Unit<Q, ToValueT, ToPolicy>
    UnitCast(const Unit<Q, FromValueT, FromPolicy>& from) noexcept
    {
        // 1) Convert the source value to the common base unit (always as FromValueT)
        FromValueT base = FromPolicy::ToBase(from.GetValue());

        // 2) Convert that base value into the target policy’s units (still as FromValueT)
        FromValueT toValIntermediate = ToPolicy::FromBase(base);

        // 3) Finally, cast into the desired ValueT for the target Unit
        return Unit<Q, ToValueT, ToPolicy>(
                static_cast<ToValueT>(toValIntermediate));
    }

    // Helper overload accepting a concrete destination Unit type
    template<typename ToUnit, typename FromUnit>
    constexpr ToUnit UnitCast(const FromUnit& from) noexcept
    {
        static_assert(
                ToUnit::Exponents == FromUnit::Exponents,
                "UnitCast: Units must have the same quantity exponents");

        // Reuse the above template using ToUnit's ValueType and ConversionPolicy
        return UnitCast<
                ToUnit::Exponents,
                typename ToUnit::ValueType, typename ToUnit::ConversionPolicy,
                typename FromUnit::ValueType, typename FromUnit::ConversionPolicy>(from);
    }

    /// @brief ValueCast: convert between units of the same kind with different ValueType (policy/exponents preserved)
    template<typename ToValueT, typename FromUnit>
    constexpr auto ValueCast(const FromUnit& from) noexcept
    {
        using UnitT = Unit<
                FromUnit::Exponents,
                ToValueT,
                typename FromUnit::ConversionPolicy>;
        return UnitCast<UnitT>(from);
    }

    // Example: SI time units
    using Seconds      = Unit<TIME, double, RatioPolicy<1, 1>>;
    using Milliseconds = Unit<TIME, double, RatioPolicy<1, 1000>>;
    using Microseconds = Unit<TIME, double, RatioPolicy<1, 1000000>>;
    using Nanoseconds  = Unit<TIME, double, RatioPolicy<1, 1000000000>>;
    using Minutes      = Unit<TIME, double, RatioPolicy<60, 1>>;
    using Hours        = Unit<TIME, double, RatioPolicy<3600, 1>>;
    using Days         = Unit<TIME, double, RatioPolicy<86400, 1>>;
    using Weeks        = Unit<TIME, double, RatioPolicy<604800, 1>>;
    using Fortnights   = Unit<TIME, double, RatioPolicy<1209600, 1>>;

    // Example: SI length units
    using Meters      = Unit<LENGTH, double, RatioPolicy<1, 1>>;
    using Kilometers  = Unit<LENGTH, double, RatioPolicy<1000, 1>>;
    using Centimeters = Unit<LENGTH, double, RatioPolicy<1, 100>>;
    using Millimeters = Unit<LENGTH, double, RatioPolicy<1, 1000>>;

    // Example: SI mass units
    using Kilograms  = Unit<MASS, double, RatioPolicy<1, 1>>;
    using Grams      = Unit<MASS, double, RatioPolicy<1, 1000>>;
    using Milligrams = Unit<MASS, double, RatioPolicy<1, 1000000>>;

    // Example: SI current units
    using Amperes      = Unit<CURRENT, double, RatioPolicy<1, 1>>;
    using Milliamperes = Unit<CURRENT, double, RatioPolicy<1, 1000>>;

    // Example: SI temperature units
    using Kelvin     = Unit<TEMPERATURE, double, RatioPolicy<1, 1>>;
    using Celsius    = Unit<TEMPERATURE, double, OffsetPolicy<273.15>>;
    using Fahrenheit = Unit<TEMPERATURE, double, FahrenheitToKelvinPolicy>;

    // Example: Derived unit
    using Velocity = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), double, RatioPolicy<1, 1>>;

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


    /// @brief Writes a unit's value followed by its registered symbol.
    template<QuantityExponents Q, typename ValueT, typename Policy>
    std::ostream& operator<<(std::ostream& os, const Unit<Q, ValueT, Policy>& u)
        requires requires { UnitTraits<Unit<Q, ValueT, Policy>>::symbol; }
    {
        return os << u.GetValue() << ' ' << UnitTraits<Unit<Q, ValueT, Policy>>::symbol;
    }

    /// @brief Writes the raw value of a unit without a registered symbol.
    template<QuantityExponents Q, typename ValueT, typename Policy>
    std::ostream& operator<<(std::ostream& os, const Unit<Q, ValueT, Policy>& u)
        requires(!requires { UnitTraits<Unit<Q, ValueT, Policy>>::symbol; })
    {
        return os << u.GetValue();
    }

}// namespace NGIN::Units

#if defined(__cpp_lib_format)

//=== std::formatter integration ===
namespace std
{
    template<
            NGIN::Units::QuantityExponents Q,
            typename ValueT,
            typename Policy,
            typename CharT>
    struct formatter<NGIN::Units::Unit<Q, ValueT, Policy>, CharT> : public std::formatter<ValueT, CharT>
    {
        // delegate all the number‐parsing work to std::formatter<ValueT,CharT>
        using Base = std::formatter<ValueT, CharT>;

        /// @brief Parses the numeric format specification through the underlying value formatter.
        constexpr auto parse(std::format_parse_context& ctx)
        {
            return Base::parse(ctx);
        }

        /// @brief Formats the numeric value and appends a registered unit symbol when available.
        template<typename FormatContext>
        auto format(const NGIN::Units::Unit<Q, ValueT, Policy>& u, FormatContext& ctx) const
        {
            // format the raw value first
            auto out_it = Base::format(u.GetValue(), ctx);
            // if there's a symbol, append it
            if constexpr (requires {
                              NGIN::Units::UnitTraits<NGIN::Units::Unit<Q, ValueT, Policy>>::symbol;
                          })
            {
                return std::format_to(ctx.out(), " {}",
                                      NGIN::Units::UnitTraits<NGIN::Units::Unit<Q, ValueT, Policy>>::symbol);
            }
            else
            {
                return out_it;
            }
        }
    };
}// namespace std
#endif// __cpp_lib_format
