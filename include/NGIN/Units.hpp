#pragma once
#include <NGIN/Primitives.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace NGIN
{
#pragma region Quantity Types

    /// @brief Represents a quantity with the following exponents:
    /// @tparam LengthExp
    /// @tparam MassExp
    /// @tparam TimeExp
    /// @tparam CurrentExp
    /// @tparam TemperatureExp
    /// @tparam AmountOfSubstanceExp
    /// @tparam LuminousIntensityExp
    template<
            int LengthExp,
            int MassExp,
            int TimeExp,
            int CurrentExp,
            int TemperatureExp,
            int AmountOfSubstanceExp,
            int LuminousIntensityExp>
    struct Quantity
    {
        static constexpr int LENGTH              = LengthExp;
        static constexpr int MASS                = MassExp;
        static constexpr int TIME                = TimeExp;
        static constexpr int CURRENT             = CurrentExp;
        static constexpr int TEMPERATURE         = TemperatureExp;
        static constexpr int AMOUNT_OF_SUBSTANCE = AmountOfSubstanceExp;
        static constexpr int LUMINOUS_INTENSITY  = LuminousIntensityExp;
    };

    // Helper to check if two Quantities are the same
    template<typename Q1, typename Q2>
    struct IsSameQuantity
    {
        static constexpr bool value =
                Q1::LENGTH == Q2::LENGTH &&
                Q1::MASS == Q2::MASS &&
                Q1::TIME == Q2::TIME &&
                Q1::CURRENT == Q2::CURRENT &&
                Q1::TEMPERATURE == Q2::TEMPERATURE &&
                Q1::AMOUNT_OF_SUBSTANCE == Q2::AMOUNT_OF_SUBSTANCE &&
                Q1::LUMINOUS_INTENSITY == Q2::LUMINOUS_INTENSITY;
    };

    using Length            = Quantity<1, 0, 0, 0, 0, 0, 0>;
    using Mass              = Quantity<0, 1, 0, 0, 0, 0, 0>;
    using Time              = Quantity<0, 0, 1, 0, 0, 0, 0>;
    using Current           = Quantity<0, 0, 0, 1, 0, 0, 0>;
    using Temperature       = Quantity<0, 0, 0, 0, 1, 0, 0>;
    using AmountOfSubstance = Quantity<0, 0, 0, 0, 0, 1, 0>;
    using LuminousIntensity = Quantity<0, 0, 0, 0, 0, 0, 1>;

    template<typename Derived, typename Quantity, typename ValueT = F64>
    class Unit
    {
    public:
        using ValueType     = ValueT;
        using quantity_type = Quantity;

        constexpr Unit()
            : value({}) {};
        constexpr Unit(ValueType value)
            : value(value) {}

        // Arithmetic operators with the same unit type
        constexpr Derived operator+(const Derived& other)
        {
            return Derived(value + other.value);
        }

        constexpr Derived operator-(const Derived& other)
        {
            return Derived(value - other.value);
        }

        // Scalar multiplication and division
        constexpr Derived operator*(ValueType scalar)
        {
            return Derived(value * scalar);
        }

        constexpr Derived operator/(ValueType scalar)
        {
            return Derived(value / scalar);
        }

        // Comparison operators
        constexpr bool operator==(const Derived& other)
        {
            return value == other.value;
        }

        constexpr bool operator!=(const Derived& other)
        {
            return !(*this == other);
        }

        // Access the value
        constexpr ValueType GetValue() const
        {
            return value;
        }

        // Output representation
        friend std::ostream& operator<<(std::ostream& os, const Derived& unit)
        {
            return os << unit.value << Derived::Symbol();
        }

    protected:
        ValueType value;

    private:
        // Private conversion methods
        static constexpr ValueType ConvertToBase(ValueType value)
        {
            return Derived::ToBase(value);
        }

        static constexpr ValueType ConvertFromBase(ValueType baseValue)
        {
            return Derived::FromBase(baseValue);
        }

        // Grant access to UnitCast
        template<typename ToUnit, typename FromUnit>
        friend constexpr ToUnit UnitCast(const FromUnit& from);
    };

    // UnitCast function
    template<typename ToUnit, typename FromUnit>
    constexpr ToUnit UnitCast(const FromUnit& from)
    {
        static_assert(IsSameQuantity<typename ToUnit::quantity_type, typename FromUnit::quantity_type>::value,
                      "Units must have the same quantity for conversion");

        auto baseValue = FromUnit::ConvertToBase(from.GetValue());
        auto toValue   = ToUnit::ConvertFromBase(baseValue);
        return ToUnit(toValue);
    }

#pragma endregion
#pragma region Time Units
    // Base unit: Seconds
    struct Seconds : Unit<Seconds, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "s";
        }

    private:
        friend class Unit<Seconds, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value;// Base unit
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value;// Base unit
        }
    };

    // Milliseconds
    struct Milliseconds : Unit<Milliseconds, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "ms";
        }

    private:
        friend class Unit<Milliseconds, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value / 1E+03;
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value * 1E+03;
        }
    };

    // Microseconds
    struct Microseconds : Unit<Microseconds, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "us";
        }

    private:
        friend class Unit<Microseconds, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value / 1E+06;
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value * 1E+06;
        }
    };

    // Nanoseconds
    struct Nanoseconds : Unit<Nanoseconds, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "ns";
        }

    private:
        friend class Unit<Nanoseconds, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value / 1E+09;
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value * 1E+09;
        }
    };

    // Minutes
    struct Minutes : Unit<Minutes, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "m";
        }

    private:
        friend class Unit<Minutes, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value * 60.0;
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value / 60.0;
        }
    };

    // Hours
    struct Hours : Unit<Hours, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "h";
        }

    private:
        friend class Unit<Hours, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value * 3600.0;
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value / 3600.0;
        }
    };

    // Days
    struct Days : Unit<Days, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "d";
        }

    private:
        friend class Unit<Days, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value * 86400.0;// 24 * 3600
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value / 86400.0;
        }
    };

    struct Weeks : Unit<Weeks, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "wk";
        }

    private:
        friend class Unit<Weeks, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value * 604800.0;// 7 days
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value / 604800.0;
        }
    };

    // Fun Time Unit: Fortnights
    struct Fortnights : Unit<Fortnights, Time>
    {
        using Unit::Unit;

        static constexpr std::string_view Symbol()
        {
            return "fn";
        }

    private:
        friend class Unit<Fortnights, Time>;

        static constexpr ValueType ToBase(ValueType value)
        {
            return value * 1209600.0;// 14 days
        }

        static constexpr ValueType FromBase(ValueType value)
        {
            return value / 1209600.0;
        }
    };


#pragma endregion

    template<typename UnitType, typename QuantityType>
    concept IsUnitOf = requires {
        typename UnitType::quantity_type;                                                  // Check that UnitType has a quantity_type
        std::is_base_of<Unit<UnitType, typename UnitType::quantity_type>, UnitType>::value;// Must derive from Unit
        std::is_same<typename UnitType::quantity_type, QuantityType>::value;               // Check the quantity matches
    };
}// namespace NGIN
