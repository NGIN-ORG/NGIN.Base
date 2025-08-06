/// @file BigIntTest.cpp
/// @brief Tests for NGIN::Math::BigInt using boost::ut
///
/// Covers construction, arithmetic, comparison, and edge cases for BigInt.

#include <NGIN/Math/BigInt.hpp>
#include <boost/ut.hpp>

using namespace boost::ut;
using namespace NGIN::Math;

suite<"NGIN::Math::BigInt"> bigIntTests = [] {
    /*
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
*/
    "Multiplication"_test = [] {
        BigInt a("123");
        BigInt b("456");
        expect((a * b) == BigInt("56088"));
        expect((BigInt("-123") * b) == BigInt("-56088"));
        expect((a * BigInt("-456")) == BigInt("-56088"));
        expect((BigInt("-123") * BigInt("-456")) == BigInt("56088"));
    };

    "Division"_test = [] {
        // Single-limb division
        expect((BigInt("123456789") / BigInt("3")) == BigInt("41152263"));
        expect((BigInt("-123456789") / BigInt("3")) == BigInt("-41152263"));
        expect((BigInt("123456789") / BigInt("-3")) == BigInt("-41152263"));
        expect((BigInt("-123456789") / BigInt("-3")) == BigInt("41152263"));

        // Naive (tiny) division (<=4 limbs)
        BigInt a("56088");
        BigInt b("456");
        expect((a / b) == BigInt("123"));
        expect((BigInt("56088") / BigInt("-456")) == BigInt("-123"));
        expect((BigInt("-56088") / BigInt("456")) == BigInt("-123"));
        expect((BigInt("-56088") / BigInt("-456")) == BigInt("123"));

        // Knuth (medium) division (<256 limbs)
        std::string medA(200, '9');// 200 digits
        std::string medB(100, '9');// 100 digits
        BigInt bigA(medA), bigB(medB);
        BigInt q = bigA / bigB;
        BigInt r = bigA % bigB;
        // bigA = bigB * q + r, 0 <= r < bigB
        expect(bigA == bigB * q + r);
        expect(r < bigB);

        // Burnikel–Ziegler (large) division (>=256 limbs)
        std::string largeA(3000, '7');// 3000 digits
        std::string largeB(1500, '3');// 1500 digits
        BigInt bigLA(largeA), bigLB(largeB);
        BigInt qL = bigLA / bigLB;
        BigInt rL = bigLA % bigLB;
        expect(bigLA == bigLB * qL + rL);
        expect(rL < bigLB);

        // Additional simple division tests
        expect((BigInt("10") / BigInt("2")) == BigInt("5"));
        expect((BigInt("10") / BigInt("3")) == BigInt("3"));
        expect((BigInt("10") / BigInt("-3")) == BigInt("-3")) << (BigInt("10") / BigInt("-3"));
        expect((BigInt("-10") / BigInt("3")) == BigInt("-3"));
        expect((BigInt("-10") / BigInt("-3")) == BigInt("3"));
        expect((BigInt("0") / BigInt("1")) == BigInt("0"));
        expect((BigInt("1") / BigInt("1")) == BigInt("1"));
        expect((BigInt("-1") / BigInt("1")) == BigInt("-1"));
        expect((BigInt("1") / BigInt("-1")) == BigInt("-1"));
        expect((BigInt("-1") / BigInt("-1")) == BigInt("1"));
    };

    "Modulo"_test = [] {
        // Single-limb modulo
        expect((BigInt("123456789") % BigInt("3")) == BigInt("0"));
        expect((BigInt("123456789") % BigInt("10")) == BigInt("9"));
        expect((BigInt("-123456789") % BigInt("10")) == BigInt("-9"));

        // Naive (tiny) modulo (<=4 limbs)
        BigInt a("1001");
        BigInt b("100");
        expect((a % b) == BigInt("1"));
        expect((BigInt("-1001") % b) == BigInt("-1")) << (BigInt("-1001") % b);
        expect((a % BigInt("-100")) == BigInt("1"));

        // Knuth (medium) modulo (<256 limbs)
        std::string medA(200, '9');
        std::string medB(100, '9');
        BigInt bigA(medA), bigB(medB);
        BigInt q = bigA / bigB;
        BigInt r = bigA % bigB;
        expect(bigA == bigB * q + r);
        expect(r < bigB);

        // Burnikel–Ziegler (large) modulo (>=256 limbs)
        std::string largeA(3000, '7');
        std::string largeB(1500, '3');
        BigInt bigLA(largeA), bigLB(largeB);
        BigInt qL = bigLA / bigLB;
        BigInt rL = bigLA % bigLB;
        expect(bigLA == bigLB * qL + rL);
        expect(rL < bigLB);

        // Additional modulo tests
        expect((BigInt("10") % BigInt("3")) == BigInt("1")) << (BigInt("10") % BigInt("3"));
        expect((BigInt("10") % BigInt("-3")) == BigInt("1"));
        expect((BigInt("-10") % BigInt("3")) == BigInt("-1"));
        expect((BigInt("-10") % BigInt("-3")) == BigInt("-1"));
        expect((BigInt("10") % BigInt("2")) == BigInt("0"));
        expect((BigInt("-10") % BigInt("2")) == BigInt("0"));
        expect((BigInt("10") % BigInt("1")) == BigInt("0"));
        expect((BigInt("-10") % BigInt("1")) == BigInt("0"));
        expect((BigInt("0") % BigInt("1")) == BigInt("0"));
        expect((BigInt("0") % BigInt("100")) == BigInt("0"));
        expect((BigInt("0") % BigInt("-100")) == BigInt("0"));
        expect((BigInt("1") % BigInt("1")) == BigInt("0"));
        expect((BigInt("-1") % BigInt("1")) == BigInt("0"));
        expect((BigInt("1") % BigInt("-1")) == BigInt("0"));
        expect((BigInt("-1") % BigInt("-1")) == BigInt("0"));
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
        expect(ss.str() == "12345 -67890") << ss.str();
    };

    "LargeNumbers"_test = [] {
        // 1) Add + carry‐propagation across 1000 digits of 9
        std::string nines(1000, '9');
        BigInt bigN(nines);
        BigInt one("1");
        std::string oneWithZeros = "1" + std::string(1000, '0');
        BigInt expectedAdd(oneWithZeros);
        expect((bigN + one) == expectedAdd) << "9...9 + 1 should roll over to 1 followed by 1000 zeros";

        // 2) Subtract back
        expect((expectedAdd - one) == bigN) << "1 000…000 - 1 should give back the 1000 nines";

        // 3) Multiplying by powers of BASE (limb‐shifts)
        //    (BASE=10^9) so shifting by limbs is 9 decimal zeros per limb.
        //    Here we build a 5000‑digit “1” followed by zeros:
        std::string zeros5000(5000, '0');
        BigInt big10k("1" + zeros5000);
        // Multiply two such “shifts” to get 10000 zeros:
        BigInt prod = big10k * big10k;
        expect(prod == BigInt("1" + std::string(10000, '0'))) << "Big limb‐shifts: (1e5000)*(1e5000) == 1e10000";

        // 4) Division and modulo on those same large shifts:
        BigInt q = prod / big10k;
        BigInt r = prod % big10k;
        expect(q == big10k) << " (1e10000)/(1e5000) == 1e5000 ";
        expect(r == BigInt("0")) << " (1e10000) % (1e5000) == 0";

        // 5) A medium‐sized random‑like 200‑digit multiplication
        std::string a = "12345678901234567890"
                        "98765432109876543210"
                        "11111111112222222222"
                        "33333333334444444444";
        std::string b = "99999999990000000000"
                        "88888888887777777777"
                        "66666666665555555555"
                        "44444444443333333333";
        BigInt A(a), B(b);
        BigInt C = A * B;
        // Check that high‐level properties hold:
        //   C / A == B,   and   C % A == 0
        expect((C / A) == B) << "C/A should recover B";
        expect((C % A).IsZero()) << "C%A should be zero";

        // 6) And reverse‑order subtraction: B - A must be correct magnitude
        if (B > A)
        {
            BigInt diff = B - A;
            // We know B > A so diff + A == B
            expect((diff + A) == B) << " (B-A)+A==B ";
        }
    };
};
