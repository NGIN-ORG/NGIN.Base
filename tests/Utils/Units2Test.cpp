/// @file Units2Test.cpp
/// @brief Tests for NGIN::Units (Units2.hpp) using boost::ut
///
/// Covers construction, arithmetic, conversion, and type algebra for the new unit system.

#include <NGIN/Units2.hpp>
#include <boost/ut.hpp>

using namespace boost::ut;
using namespace NGIN::Units;

suite<"NGIN::Units2"> units2Tests = [] {
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
    };

    "UnitAlgebra_Multiply"_test = [] {
        Seconds s {2.0};
        auto m = s * s;// Should yield Unit<QuantityExponents{0,0,2,...}, Ratio{1,1}, double>
        expect(m.GetValue() == 4.0);
    };

    "UnitAlgebra_Derived"_test = [] {
        auto dist = Unit<LENGTH, NGIN::Math::Ratio<1, 1>, double>(10.0);
        auto time = Seconds(2.0);
        auto vel  = dist / time;// Should yield Velocity
        expect(vel.GetValue() == 5.0);
    };

    "EqualityAndInequality"_test = [] {
        Seconds a {1.0}, b {1.0}, c {2.0};
        expect(a == b);
        expect(a != c);
    };

    "UserExtensionExample"_test = [] {
        struct FooUnit : Unit<QuantityExponents {1, 2, 3, 4, 5, 6, 7}, NGIN::Math::Ratio<42, 1>, float>
        {
            using Unit::Unit;
        };
        FooUnit f {2.0f};
        expect(f.GetValue() == 2.0f);
        expect(f.ToBase() == 84.0f);
    };
};
