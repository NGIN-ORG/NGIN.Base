#include <NGIN/Utilities/MSBFlag.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

using namespace NGIN::Utilities;

TEST_CASE("MSBFlag default construction", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t> flag;
    CHECK(flag.GetValue() == 0U);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetRaw() == 0U);
}

TEST_CASE("MSBFlag construction with value and flag", "[Utilities][MSBFlag]")
{
    constexpr uint32_t input = 703710U;
    MSBFlag<uint32_t>  flag(input, true);
    CHECK(flag.GetValue() == input);
    CHECK(flag.GetFlag());
    CHECK(flag.GetRaw() == (input | MSBFlag<uint32_t>::flagMask));
}

TEST_CASE("MSBFlag stores value when flag false", "[Utilities][MSBFlag]")
{
    constexpr uint16_t input = 4660U;
    MSBFlag<uint16_t>  flag(input, false);
    CHECK(flag.GetValue() == input);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetRaw() == input);
}

TEST_CASE("MSBFlag SetValue preserves flag", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t> flag(5U, true);
    flag.SetValue(42U);
    CHECK(flag.GetValue() == 42U);
    CHECK(flag.GetFlag());
}

TEST_CASE("MSBFlag SetFlag preserves value", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t> flag(123U, false);
    flag.SetFlag(true);
    CHECK(flag.GetFlag());
    CHECK(flag.GetValue() == 123U);

    flag.SetFlag(false);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetValue() == 123U);
}

TEST_CASE("MSBFlag Set updates value and flag", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t> flag;
    flag.Set(85U, true);
    CHECK(flag.GetValue() == 85U);
    CHECK(flag.GetFlag());
    CHECK(flag.GetRaw() == (0x55U | MSBFlag<uint32_t>::flagMask));

    flag.Set(170U, false);
    CHECK(flag.GetValue() == 170U);
    CHECK_FALSE(flag.GetFlag());
}

TEST_CASE("MSBFlag SetRaw applies bit masks", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t> flag;
    uint32_t          raw = MSBFlag<uint32_t>::flagMask | 3405691582U;
    flag.SetRaw(raw);
    CHECK(flag.GetRaw() == raw);
    CHECK(flag.GetFlag());
    CHECK(flag.GetValue() == (raw & MSBFlag<uint32_t>::valueMask));

    flag.SetRaw(0U);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetValue() == 0U);
}

TEST_CASE("MSBFlag compares raw data", "[Utilities][MSBFlag]")
{
    MSBFlag<uint16_t> a(31U, true);
    MSBFlag<uint16_t> b(31U, true);
    MSBFlag<uint16_t> c(31U, false);
    MSBFlag<uint16_t> d(241U, true);

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a != d);
    CHECK(c != d);
}

TEST_CASE("MSBFlag max value excludes MSB", "[Utilities][MSBFlag]")
{
    constexpr auto maxValue = MSBFlag<uint32_t>::MaxValue();
    constexpr auto expected = std::numeric_limits<uint32_t>::max() >> 1;
    CHECK(maxValue == expected);
}

TEST_CASE("MSBFlag formatting", "[Utilities][MSBFlag]")
{
    MSBFlag<uint32_t>  flag(66U, true);
    std::ostringstream stream;
    stream << flag;
    CHECK(stream.str() == std::string {"Value=66, Flag=true"});
}

TEST_CASE("MSBFlag supports multiple widths", "[Utilities][MSBFlag]")
{
    MSBFlag<uint8_t>  flag8(127U, true);
    MSBFlag<uint64_t> flag64(4095ULL, false);
    CHECK(flag8.GetValue() == 127U);
    CHECK(flag8.GetFlag());
    CHECK(flag64.GetValue() == 4095ULL);
    CHECK_FALSE(flag64.GetFlag());
}
