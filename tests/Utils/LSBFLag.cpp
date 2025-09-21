/// @file test_lsb_flag.cpp
/// @brief Enhanced unit tests for NGIN::Utilities::LSBFlag using Catch2.

#include <NGIN/Utilities/LSBFlag.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

using namespace NGIN::Utilities;

TEST_CASE("LSBFlag default construction", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t> flag;
    CHECK(flag.GetValue() == 0U);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetRaw() == 0U);
}

TEST_CASE("LSBFlag stores value and flag", "[Utilities][LSBFlag]")
{
    constexpr uint32_t input = 703710U;
    LSBFlag<uint32_t>  flag(input, true);
    CHECK(flag.GetValue() == input);
    CHECK(flag.GetFlag());
    CHECK(flag.GetRaw() == ((input << 1) | LSBFlag<uint32_t>::flagMask));

    LSBFlag<uint16_t> off(4660U, false);
    CHECK(off.GetValue() == 4660U);
    CHECK_FALSE(off.GetFlag());
    CHECK(off.GetRaw() == static_cast<uint16_t>(4660U << 1));
}

TEST_CASE("LSBFlag SetValue keeps flag", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t> flag(7U, true);
    flag.SetValue(42U);
    CHECK(flag.GetValue() == 42U);
    CHECK(flag.GetFlag());
}

TEST_CASE("LSBFlag SetFlag keeps value", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t> flag(99U, false);
    flag.SetFlag(true);
    CHECK(flag.GetFlag());
    CHECK(flag.GetValue() == 99U);
    flag.SetFlag(false);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetValue() == 99U);
}

TEST_CASE("LSBFlag Set updates raw state", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t> flag;
    flag.Set(77U, true);
    CHECK(flag.GetValue() == 77U);
    CHECK(flag.GetFlag());
    CHECK(flag.GetRaw() == ((77U << 1) | LSBFlag<uint32_t>::flagMask));

    flag.Set(88U, false);
    CHECK(flag.GetValue() == 88U);
    CHECK_FALSE(flag.GetFlag());
}

TEST_CASE("LSBFlag SetRaw interprets bits", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t> flag;
    uint32_t          odd = 2021U * 2U + 1U;
    flag.SetRaw(odd);
    CHECK(flag.GetRaw() == odd);
    CHECK(flag.GetFlag());
    CHECK(flag.GetValue() == (odd >> 1));

    uint32_t even = 2022U * 2U;
    flag.SetRaw(even);
    CHECK_FALSE(flag.GetFlag());
    CHECK(flag.GetValue() == (even >> 1));
}

TEST_CASE("LSBFlag equality compares raw state", "[Utilities][LSBFlag]")
{
    LSBFlag<uint16_t> a(100U, true);
    LSBFlag<uint16_t> b(100U, true);
    LSBFlag<uint16_t> c(100U, false);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(b != c);
}

TEST_CASE("LSBFlag reports maximum value", "[Utilities][LSBFlag]")
{
    constexpr auto maxValue = LSBFlag<uint32_t>::MaxValue();
    constexpr auto expected = std::numeric_limits<uint32_t>::max() >> 1;
    CHECK(maxValue == expected);
}

TEST_CASE("LSBFlag formats output", "[Utilities][LSBFlag]")
{
    LSBFlag<uint32_t>  flag(42U, true);
    std::ostringstream stream;
    stream << flag;
    CHECK(stream.str() == std::string {"Value=42, Flag=true"});
}

TEST_CASE("LSBFlag supports multiple widths", "[Utilities][LSBFlag]")
{
    LSBFlag<uint8_t>  flag8(5U, true);
    LSBFlag<uint64_t> flag64(12345ULL, false);
    CHECK(flag8.GetValue() == 5U);
    CHECK(flag8.GetFlag());
    CHECK(flag64.GetValue() == 12345ULL);
    CHECK_FALSE(flag64.GetFlag());
}
