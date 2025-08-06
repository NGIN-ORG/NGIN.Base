/// @file Units2Test.cpp
/// @brief Tests for NGIN::Units (Units2.hpp) using boost::ut
///
/// Covers construction, arithmetic, conversion, and type algebra for the new unit system.

#include <NGIN/Units.hpp>
#include <boost/ut.hpp>

using namespace boost::ut;
using namespace NGIN::Units;

suite<"NGIN::Units"> unitsTests = [] {
    "DefaultConstruction"_test = [] {
        Seconds s {0.0};
        expect(s.GetValue() == 0.0);
    };

    "Arithmetic_SameUnit"_test = [] {
        Seconds a {2.0}, b {3.0};
        auto c = a + b;
        expect(c.GetValue() == 5.0);
        auto d = c - a;
        expect(d.GetValue() == 3.0);
    };

    "ScalarMultiplication"_test = [] {
        Seconds s {2.5};
        auto s2 = s * 4.0;
        expect(s2.GetValue() == 10.0);
        auto s3 = s2 / 2.0;
        expect(s3.GetValue() == 5.0);
    };


    "UnitConversion"_test = [] {
        Seconds s {1.5};
        Milliseconds ms = UnitCast<Milliseconds>(s);
        expect(ms.GetValue() == 1500.0);
        Seconds s2 = UnitCast<Seconds>(ms);
        expect(s2.GetValue() == 1.5);

        // ValueCast: float <-> double
        using MillisecondsF = Unit<TIME, float, RatioPolicy<1, 1000>>;
        auto msf            = ValueCast<float>(ms);// msf is MillisecondsF
        expect(msf.GetValue() == 1500.0f);
        auto msd = ValueCast<double>(msf);// msd is Milliseconds
        expect(msd.GetValue() == 1500.0);
    };


    "UnitAlgebra_Multiply"_test = [] {
        Seconds s {2.0};
        auto m = s * s;// Should yield Unit<QuantityExponents{0,0,2,...}, double, RatioPolicy<1,1>>
        expect(m.GetValue() == 4.0);
    };


    "UnitAlgebra_Derived"_test = [] {
        Meters dist {10.0};
        Seconds time {2.0};
        auto vel = dist / time;// Should yield Velocity (m/s)
        expect(vel.GetValue() == 5.0);

        // Define km/h and m/s units
        using MetersPerSecond   = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), double, RatioPolicy<1, 1>>;
        using KilometersPerHour = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), double, RatioPolicy<1000, 3600>>;

        MetersPerSecond v1 {10.0};
        KilometersPerHour v2 = UnitCast<KilometersPerHour>(v1);
        expect(v2.GetValue() == 36.0);
        MetersPerSecond v3 = UnitCast<MetersPerSecond>(v2);
        expect(v3.GetValue() == 10.0);
    };

    "EqualityAndInequality"_test = [] {
        Seconds a {1.0}, b {1.0}, c {2.0};
        expect(a == b);
        expect(a != c);
    };

    "TemperatureConversion"_test = [] {
        Celsius c {100.0};
        Kelvin k = UnitCast<Kelvin>(c);
        expect(k.GetValue() == 373.15);
        Celsius c2 = UnitCast<Celsius>(k);
        expect(c2.GetValue() == 100.0);

        Fahrenheit f {32.0};
        Kelvin k2 = UnitCast<Kelvin>(f);
        expect(std::abs(k2.GetValue() - 273.15) < 1e-10);
        Fahrenheit f2 = UnitCast<Fahrenheit>(k2);
        expect(std::abs(f2.GetValue() - 32.0) < 1e-10);
    };

    "UserExtensionExample"_test = [] {
        static constexpr QuantityExponents MyQ = [] {
            QuantityExponents q {};
            q.exponents = {1, 2, 3, 4, 5, 6, 7};
            return q;
        }();
        using MyUnit = Unit<MyQ, float, RatioPolicy<42, 1>>;
        MyUnit f {2.0f};
        expect(f.GetValue() == 2.0f);
        expect(f.ToBase() == 84.0f);
    };

    "StreamingAndFormatting"_test = [] {
        std::stringstream ss;
        Seconds s {42.0};
        ss << s;
        expect(ss.str() == "42 s");

        // std::format (C++20)
#if defined(__cpp_lib_format)
        auto formatted = std::format("{:.1f}", s);
        expect(formatted == "42.0 s");
#endif
    };
};
