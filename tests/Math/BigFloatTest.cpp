/// @file BigFloatTest.cpp
/// @brief Tests deterministic arbitrary-precision binary floating point.

#include <NGIN/Math/BigFloat.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <compare>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

using NGIN::Math::BigFloat;
using NGIN::Math::BigFloatClass;
using NGIN::Math::BigFloatRoundingMode;

TEST_CASE("NGIN::Math::BigFloat construction and formatting", "[Math][BigFloat]")
{
    using Real = BigFloat<256>;

    CHECK(Real().IsZero());
    CHECK(Real(-0.0).IsZero());
    CHECK(Real(-0.0).SignBit());
    CHECK(Real(42) == Real("42"));
    CHECK(Real(-42) == Real("-4.2e1"));
    CHECK(Real("1.25") == Real("125e-2"));
    CHECK(Real(".5") == Real("5e-1"));
    CHECK(Real("5.") == Real(5));

    const BigFloat<8> compact("1.5");
    CHECK(compact.ToHexString() == "0x1.80p+0");
    CHECK(BigFloat<8>(compact.ToHexString()) == compact);
    CHECK(compact.ToString(4) == "1.500e+0");

    const Real extreme("0x1.8p+200001");
    CHECK(extreme.ToString() == extreme.ToHexString());
    CHECK(Real(extreme.ToString()) == extreme);

    const Real        original("3.1415926535897932384626433832795028841971693993751");
    const std::string serialized = original.ToString();
    CHECK(Real(serialized) == original);

    std::ostringstream stream;
    stream << Real("-0.125");
    CHECK(stream.str().starts_with("-1.25"));

    CHECK_THROWS_AS(Real(""), std::invalid_argument);
    CHECK_THROWS_AS(Real("."), std::invalid_argument);
    CHECK_THROWS_AS(Real("1e"), std::invalid_argument);
    CHECK_THROWS_AS(Real("1.0 trailing"), std::invalid_argument);
    CHECK_THROWS_AS(Real(static_cast<const char*>(nullptr)), std::invalid_argument);
}

TEST_CASE("NGIN::Math::BigFloat arithmetic", "[Math][BigFloat]")
{
    using Real = BigFloat<256>;

    CHECK(Real("1.25") + Real("2.5") == Real("3.75"));
    CHECK(Real("1.25") - Real("2.5") == Real("-1.25"));
    CHECK(Real("1.25") * Real("2.5") == Real("3.125"));
    CHECK(Real("3.125") / Real("2.5") == Real("1.25"));
    CHECK(Real(-7) / Real(2) == Real("-3.5"));

    const Real third = Real(1) / Real(3);
    CHECK(static_cast<double>(third) == Catch::Approx(1.0 / 3.0).epsilon(1e-15));
    CHECK(static_cast<double>(third * Real(3)) == Catch::Approx(1.0).epsilon(1e-15));

    const Real rootTwo = NGIN::Math::Sqrt(Real(2));
    CHECK(static_cast<double>(rootTwo) == Catch::Approx(std::sqrt(2.0)).epsilon(1e-15));
    CHECK(NGIN::Math::Sqrt(Real(144)) == Real(12));
    CHECK(NGIN::Math::Sqrt(Real(-1)).IsNaN());

    CHECK(NGIN::Math::Fma(Real(2), Real(3), Real(4)) == Real(10));
    CHECK(NGIN::Math::Fma(Real("1.5"), Real("2.0"), Real("-3.0")).IsZero());

    using LowPrecision = BigFloat<4>;
    const LowPrecision fusedLeft("1.125");
    const LowPrecision fusedRight("1.125");
    const LowPrecision fusedAddend("-1.25");
    CHECK(fusedLeft * fusedRight + fusedAddend == LowPrecision(0));
    CHECK(NGIN::Math::Fma(fusedLeft, fusedRight, fusedAddend) == LowPrecision("0.015625"));

    Real accumulated = 1;
    accumulated += Real("0.5");
    accumulated *= Real(4);
    accumulated -= Real(2);
    accumulated /= Real(2);
    CHECK(accumulated == Real(2));
}

TEST_CASE("NGIN::Math::BigFloat rounding policies", "[Math][BigFloat]")
{
    using Wide    = BigFloat<32>;
    using Nearest = BigFloat<4>;
    using Up      = BigFloat<4, BigFloatRoundingMode::TowardPositive>;
    using Down    = BigFloat<4, BigFloatRoundingMode::TowardNegative>;
    using Zero    = BigFloat<4, BigFloatRoundingMode::TowardZero>;

    CHECK(Nearest(Wide("1.0625")) == Nearest(1));
    CHECK(Nearest(Wide("1.1875")) == Nearest("1.25"));
    CHECK(Up("1.01") > Up(1));
    CHECK(Down("1.01") == Down(1));
    CHECK(Down("-1.01") < Down(-1));
    CHECK(Zero("-1.01") == Zero(-1));

    const Nearest one(1);
    CHECK(one.NextUp() > one);
    CHECK(one.NextDown() < one);
    CHECK(one.NextUp().NextDown() == one);
    CHECK(one.NextDown().NextUp() == one);
}

TEST_CASE("NGIN::Math::BigFloat exponent boundaries honor rounding", "[Math][BigFloat]")
{
    using Nearest = BigFloat<4>;
    using Up      = BigFloat<4, BigFloatRoundingMode::TowardPositive>;
    using Down    = BigFloat<4, BigFloatRoundingMode::TowardNegative>;
    using Zero    = BigFloat<4, BigFloatRoundingMode::TowardZero>;

    CHECK(Nearest("0x1p+1000001").IsInfinity());
    CHECK(Zero("0x1p+1000001") == Zero::MaxFinite());
    CHECK(Up("-0x1p+1000001") == Up::MaxFinite(true));
    CHECK(Down("-0x1p+1000001").IsInfinity());
    CHECK(Down("-0x1p+1000001").SignBit());

    CHECK(Nearest("0x1p-1000001").IsZero());
    CHECK(Nearest("0x1.2p-1000001") == Nearest::MinPositive());
    CHECK(Up("0x1p-1000001") == Up::MinPositive());
    CHECK(Down("0x1p-1000001").IsZero());
    CHECK(Down("-0x1p-1000001") == -Down::MinPositive());
    CHECK(Zero("-0x1p-1000001").IsZero());
    CHECK(Zero("-0x1p-1000001").SignBit());
}

TEST_CASE("NGIN::Math::BigFloat special values and ordering", "[Math][BigFloat]")
{
    using Real = BigFloat<128>;

    const Real positiveInfinity = Real::Infinity();
    const Real negativeInfinity = Real::Infinity(true);
    const Real nan              = Real::QuietNaN();

    CHECK(positiveInfinity.Classify() == BigFloatClass::Infinity);
    CHECK(negativeInfinity < Real(0));
    CHECK(positiveInfinity > Real(0));
    CHECK(Real(1) > negativeInfinity);
    CHECK(Real(-1) < positiveInfinity);
    CHECK((positiveInfinity + negativeInfinity).IsNaN());
    CHECK((positiveInfinity * Real(0)).IsNaN());
    CHECK((Real(0) / Real(0)).IsNaN());
    CHECK((Real(1) / Real(0)).IsInfinity());
    CHECK((Real(-1) / Real(0)).SignBit());
    CHECK((Real(1) / positiveInfinity).IsZero());
    CHECK(!(nan == nan));
    const bool nanIsUnordered = (nan <=> Real(0)) == std::partial_ordering::unordered;
    CHECK(nanIsUnordered);
    CHECK(Real(-0.0) == Real(0.0));
    CHECK(Real(-0.0) < Real(1));
    CHECK(Real(-0.0) > Real(-1));
}

TEST_CASE("NGIN::Math::BigFloat native and numeric-limits interoperability", "[Math][BigFloat]")
{
    using Real = BigFloat<192>;

    CHECK(static_cast<double>(Real(0.1)) == 0.1);
    CHECK(static_cast<double>(Real(-123.75)) == -123.75);
    CHECK(std::isinf(static_cast<double>(Real::Infinity())));
    CHECK(std::isnan(static_cast<double>(Real::QuietNaN())));

    CHECK(std::numeric_limits<Real>::is_specialized);
    CHECK(std::numeric_limits<Real>::digits == 192);
    CHECK(std::numeric_limits<Real>::radix == 2);
    CHECK(std::numeric_limits<Real>::has_infinity);
    CHECK(std::numeric_limits<Real>::infinity().IsInfinity());
    CHECK(std::numeric_limits<Real>::quiet_NaN().IsNaN());
    CHECK(std::numeric_limits<Real>::epsilon() == Real(1).NextUp() - Real(1));
}

TEST_CASE("NGIN::Math::BigFloat agrees with binary64 arithmetic", "[Math][BigFloat]")
{
    using Real = BigFloat<53>;

    std::mt19937_64 random(0x424947464c4f4154ULL);
    for (int iteration = 0; iteration < 1024; ++iteration)
    {
        const auto sample = [&] {
            const double fraction = static_cast<double>((random() >> 11) | 1) / static_cast<double>(NGIN::UInt64 {1} << 53);
            const int    exponent = static_cast<int>(random() % 401) - 200;
            return std::ldexp((random() & 1U) != 0 ? -fraction : fraction, exponent);
        };

        const double left   = sample();
        const double right  = sample();
        const double addend = sample();
        CAPTURE(iteration, left, right, addend);

        CHECK(static_cast<double>(Real(left) + Real(right)) == left + right);
        CHECK(static_cast<double>(Real(left) - Real(right)) == left - right);
        CHECK(static_cast<double>(Real(left) * Real(right)) == left * right);
        CHECK(static_cast<double>(Real(left) / Real(right)) == left / right);
        CHECK(static_cast<double>(NGIN::Math::Fma(Real(left), Real(right), Real(addend))) ==
              std::fma(left, right, addend));
        CHECK(static_cast<double>(NGIN::Math::Sqrt(Real(std::fabs(left)))) == std::sqrt(std::fabs(left)));
    }
}
