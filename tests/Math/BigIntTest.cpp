/// @file BigIntTest.cpp
/// @brief Tests for NGIN::Math::BigInt using Catch2
///
/// Covers construction, arithmetic, comparison, and edge cases for BigInt.

#include <NGIN/Math/BigInt.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

using namespace NGIN::Math;

TEST_CASE("NGIN::Math::BigInt", "[Math][BigInt]")
{
    SECTION("DefaultConstruction") {
        BigInt a;
        CHECK(a == BigInt("0"));
        CHECK(a.IsZero());
    }

    SECTION("StringConstruction") {
        BigInt a("12345");
        BigInt b("-67890");
        CHECK(a == BigInt("12345"));
        CHECK(b == BigInt("-67890"));
        CHECK(a != b);
    }

    SECTION("Addition") {
        BigInt a("123");
        BigInt b("456");
        CHECK((a + b) == BigInt("579"));
        CHECK((BigInt("-123") + BigInt("-456")) == BigInt("-579"));
        CHECK((BigInt("123") + BigInt("-23")) == BigInt("100"));
        CHECK((BigInt("-123") + BigInt("23")) == BigInt("-100"));
    }

    SECTION("Subtraction") {
        BigInt a("1000");
        BigInt b("1");
        CHECK((a - b) == BigInt("999"));
        CHECK((b - a) == BigInt("-999"));
        CHECK((BigInt("-1000") - BigInt("-1")) == BigInt("-999"));
        CHECK((BigInt("1000") - BigInt("-1")) == BigInt("1001"));
    };
    
    SECTION("Multiplication")
    {
        BigInt a("123");
        BigInt b("456");
        CHECK((a * b) == BigInt("56088"));
        CHECK((BigInt("-123") * b) == BigInt("-56088"));
        CHECK((a * BigInt("-456")) == BigInt("-56088"));
        CHECK((BigInt("-123") * BigInt("-456")) == BigInt("56088"));
    }

    SECTION("Division")
    {
        // Single-limb division
        CHECK((BigInt("123456789") / BigInt("3")) == BigInt("41152263"));
        CHECK((BigInt("-123456789") / BigInt("3")) == BigInt("-41152263"));
        CHECK((BigInt("123456789") / BigInt("-3")) == BigInt("-41152263"));
        CHECK((BigInt("-123456789") / BigInt("-3")) == BigInt("41152263"));

        // Naive (tiny) division (<=4 limbs)
        BigInt a("56088");
        BigInt b("456");
        CHECK((a / b) == BigInt("123"));
        CHECK((BigInt("56088") / BigInt("-456")) == BigInt("-123"));
        CHECK((BigInt("-56088") / BigInt("456")) == BigInt("-123"));
        CHECK((BigInt("-56088") / BigInt("-456")) == BigInt("123"));

        // Knuth (medium) division (<256 limbs)
        std::string medA(200, '9');// 200 digits
        std::string medB(100, '9');// 100 digits
        BigInt      bigA(medA), bigB(medB);
        BigInt      q = bigA / bigB;
        BigInt      r = bigA % bigB;
        // bigA = bigB * q + r, 0 <= r < bigB
        CHECK(bigA == bigB * q + r);
        CHECK(r < bigB);

        // Burnikel–Ziegler (large) division (>=256 limbs)
        std::string largeA(3000, '7');// 3000 digits
        std::string largeB(1500, '3');// 1500 digits
        BigInt      bigLA(largeA), bigLB(largeB);
        BigInt      qL = bigLA / bigLB;
        BigInt      rL = bigLA % bigLB;
        CHECK(bigLA == bigLB * qL + rL);
        CHECK(rL < bigLB);

        // Additional simple division tests
        CHECK((BigInt("10") / BigInt("2")) == BigInt("5"));
        CHECK((BigInt("10") / BigInt("3")) == BigInt("3"));
        {
            const auto quotient = BigInt("10") / BigInt("-3");
            INFO("quotient: " << quotient);
            CHECK(quotient == BigInt("-3"));
        }
        CHECK((BigInt("-10") / BigInt("3")) == BigInt("-3"));
        CHECK((BigInt("-10") / BigInt("-3")) == BigInt("3"));
        CHECK((BigInt("0") / BigInt("1")) == BigInt("0"));
        CHECK((BigInt("1") / BigInt("1")) == BigInt("1"));
        CHECK((BigInt("-1") / BigInt("1")) == BigInt("-1"));
        CHECK((BigInt("1") / BigInt("-1")) == BigInt("-1"));
        CHECK((BigInt("-1") / BigInt("-1")) == BigInt("1"));
    }

    SECTION("Modulo")
    {
        // Single-limb modulo
        CHECK((BigInt("123456789") % BigInt("3")) == BigInt("0"));
        CHECK((BigInt("123456789") % BigInt("10")) == BigInt("9"));
        CHECK((BigInt("-123456789") % BigInt("10")) == BigInt("-9"));

        // Naive (tiny) modulo (<=4 limbs)
        BigInt a("1001");
        BigInt b("100");
        CHECK((a % b) == BigInt("1"));
        {
            const auto remainder = BigInt("-1001") % b;
            INFO("remainder: " << remainder);
            CHECK(remainder == BigInt("-1"));
        }
        CHECK((a % BigInt("-100")) == BigInt("1"));

        // Knuth (medium) modulo (<256 limbs)
        std::string medA(200, '9');
        std::string medB(100, '9');
        BigInt      bigA(medA), bigB(medB);
        BigInt      q = bigA / bigB;
        BigInt      r = bigA % bigB;
        CHECK(bigA == bigB * q + r);
        CHECK(r < bigB);

        // Burnikel–Ziegler (large) modulo (>=256 limbs)
        std::string largeA(3000, '7');
        std::string largeB(1500, '3');
        BigInt      bigLA(largeA), bigLB(largeB);
        BigInt      qL = bigLA / bigLB;
        BigInt      rL = bigLA % bigLB;
        CHECK(bigLA == bigLB * qL + rL);
        CHECK(rL < bigLB);

        // Additional modulo tests
        {
            const auto remainder = BigInt("10") % BigInt("3");
            INFO("remainder: " << remainder);
            CHECK(remainder == BigInt("1"));
        }
        CHECK((BigInt("10") % BigInt("-3")) == BigInt("1"));
        CHECK((BigInt("-10") % BigInt("3")) == BigInt("-1"));
        CHECK((BigInt("-10") % BigInt("-3")) == BigInt("-1"));
        CHECK((BigInt("10") % BigInt("2")) == BigInt("0"));
        CHECK((BigInt("-10") % BigInt("2")) == BigInt("0"));
        CHECK((BigInt("10") % BigInt("1")) == BigInt("0"));
        CHECK((BigInt("-10") % BigInt("1")) == BigInt("0"));
        CHECK((BigInt("0") % BigInt("1")) == BigInt("0"));
        CHECK((BigInt("0") % BigInt("100")) == BigInt("0"));
        CHECK((BigInt("0") % BigInt("-100")) == BigInt("0"));
        CHECK((BigInt("1") % BigInt("1")) == BigInt("0"));
        CHECK((BigInt("-1") % BigInt("1")) == BigInt("0"));
        CHECK((BigInt("1") % BigInt("-1")) == BigInt("0"));
        CHECK((BigInt("-1") % BigInt("-1")) == BigInt("0"));
    }

    SECTION("Comparison")
    {
        BigInt a("123");
        BigInt b("456");
        CHECK(a < b);
        CHECK(b > a);
        CHECK(a <= b);
        CHECK(b >= a);
        CHECK(a != b);
        CHECK(a == BigInt("123"));
        CHECK(BigInt("-123") < a);
        CHECK(BigInt("-123") < BigInt("0"));
        CHECK(BigInt("0") > BigInt("-123"));
    }

    SECTION("EdgeCases")
    {
        CHECK(BigInt("0") == BigInt());
        CHECK(BigInt("-0") == BigInt("0"));
        CHECK(BigInt("0") + BigInt("0") == BigInt("0"));
        CHECK(BigInt("0") - BigInt("0") == BigInt("0"));
        CHECK(BigInt("0") * BigInt("0") == BigInt("0"));
    }

    SECTION("OutputOperator")
    {
        BigInt            a("12345");
        BigInt            b("-67890");
        std::stringstream ss;
        ss << a << " " << b;
        INFO("formatted output: " << ss.str());
        CHECK(ss.str() == "12345 -67890");
    }

    SECTION("LargeNumbers")
    {
        // 1) Add + carry‐propagation across 1000 digits of 9
        std::string nines(1000, '9');
        BigInt      bigN(nines);
        BigInt      one("1");
        std::string oneWithZeros = "1" + std::string(1000, '0');
        BigInt      expectedAdd(oneWithZeros);
        INFO("bigN + one: " << (bigN + one));
        CHECK((bigN + one) == expectedAdd);

        // 2) Subtract back
        INFO("expectedAdd - one: " << (expectedAdd - one));
        CHECK((expectedAdd - one) == bigN);

        // 3) Multiplying by powers of BASE (limb‐shifts)
        //    (BASE=10^9) so shifting by limbs is 9 decimal zeros per limb.
        //    Here we build a 5000‑digit “1” followed by zeros:
        std::string zeros5000(5000, '0');
        BigInt      big10k("1" + zeros5000);
        // Multiply two such “shifts” to get 10000 zeros:
        BigInt prod = big10k * big10k;
        INFO("product: " << prod);
        CHECK(prod == BigInt("1" + std::string(10000, '0')));

        // 4) Division and modulo on those same large shifts:
        BigInt q = prod / big10k;
        BigInt r = prod % big10k;
        INFO("quotient q: " << q);
        CHECK(q == big10k);
        INFO("remainder r: " << r);
        CHECK(r == BigInt("0"));

        // 5) A medium‐sized random‑like 200‑digit multiplication
        std::string a = "12345678901234567890"
                        "98765432109876543210"
                        "11111111112222222222"
                        "33333333334444444444";
        std::string b = "99999999990000000000"
                        "88888888887777777777"
                        "66666666665555555555"
                        "44444444443333333333";
        BigInt      A(a), B(b);
        BigInt      C = A * B;
        // Check that high‐level properties hold:
        //   C / A == B,   and   C % A == 0
        INFO("C / A: " << (C / A));
        CHECK((C / A) == B);
        CHECK((C % A).IsZero());

        // 6) And reverse‑order subtraction: B - A must be correct magnitude
        if (B > A)
        {
            BigInt diff = B - A;
            // We know B > A so diff + A == B
            INFO("diff + A: " << (diff + A));
            CHECK((diff + A) == B);
        }
    }
}
