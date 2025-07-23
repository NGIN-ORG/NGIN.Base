/// @file BigIntTest.cpp
/// @brief Tests for NGIN::Math::BigInt using boost::ut
///
/// Covers construction, arithmetic, comparison, and edge cases for BigInt.

#include <NGIN/Math/BigInt.hpp>
#include <boost/ut.hpp>

using namespace boost::ut;
using namespace NGIN::Math;

suite<"NGIN::Math::BigInt"> bigIntTests = [] {
    "DefaultConstruction"_test = [] {
        BigInt a;
        expect(a == BigInt("0"));
        expect(a.IsZero());
    };

    "StringConstruction"_test = [] {
        BigInt a("12345");
        BigInt b("-67890");
        expect(a == BigInt("12345"));
        expect(b == BigInt("-67890"));
        expect(a != b);
    };

    "Addition"_test = [] {
        BigInt a("123");
        BigInt b("456");
        expect((a + b) == BigInt("579"));
        expect((BigInt("-123") + BigInt("-456")) == BigInt("-579"));
        expect((BigInt("123") + BigInt("-23")) == BigInt("100"));
        expect((BigInt("-123") + BigInt("23")) == BigInt("-100"));
    };

    "Subtraction"_test = [] {
        BigInt a("1000");
        BigInt b("1");
        expect((a - b) == BigInt("999"));
        expect((b - a) == BigInt("-999"));
        expect((BigInt("-1000") - BigInt("-1")) == BigInt("-999"));
        expect((BigInt("1000") - BigInt("-1")) == BigInt("1001"));
    };

    "Multiplication"_test = [] {
        BigInt a("123");
        BigInt b("456");
        expect((a * b) == BigInt("56088"));
        expect((BigInt("-123") * b) == BigInt("-56088"));
        expect((a * BigInt("-456")) == BigInt("-56088"));
        expect((BigInt("-123") * BigInt("-456")) == BigInt("56088"));
    };

    "Division"_test = [] {
        BigInt a("56088");
        BigInt b("456");
        expect((a / b) == BigInt("123"));
        expect((a % b) == BigInt("0"));
        expect((BigInt("56088") / BigInt("-456")) == BigInt("-123"));
        expect((BigInt("-56088") / BigInt("456")) == BigInt("-123"));
        expect((BigInt("-56088") / BigInt("-456")) == BigInt("123"));
    };

    "Modulo"_test = [] {
        BigInt a("1001");
        BigInt b("100");
        expect((a % b) == BigInt("1"));
        expect((BigInt("-1001") % b) == BigInt("-1"));
        expect((a % BigInt("-100")) == BigInt("1"));
    };

    "Comparison"_test = [] {
        BigInt a("123");
        BigInt b("456");
        expect(a < b);
        expect(b > a);
        expect(a <= b);
        expect(b >= a);
        expect(a != b);
        expect(a == BigInt("123"));
        expect(BigInt("-123") < a);
        expect(BigInt("-123") < BigInt("0"));
        expect(BigInt("0") > BigInt("-123"));
    };

    "EdgeCases"_test = [] {
        expect(BigInt("0") == BigInt());
        expect(BigInt("-0") == BigInt("0"));
        expect(BigInt("0") + BigInt("0") == BigInt("0"));
        expect(BigInt("0") - BigInt("0") == BigInt("0"));
        expect(BigInt("0") * BigInt("0") == BigInt("0"));
    };

    "OutputOperator"_test = [] {
        BigInt a("12345");
        BigInt b("-67890");
        std::stringstream ss;
        ss << a << " " << b;
        expect(ss.str() == "12345 -67890");
    };
};
