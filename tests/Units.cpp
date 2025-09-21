/// @file Units2Test.cpp
/// @brief Tests for NGIN::Units (Units2.hpp).
///
/// Covers construction, arithmetic, conversion, and type algebra for the new unit system.

#include <NGIN/Units.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#if defined(__cpp_lib_format)
#include <format>
#endif
#include <sstream>

using namespace NGIN::Units;
TEST_CASE("Units support default construction", "[Units]")
{
    Seconds seconds {0.0};
    CHECK(seconds.GetValue() == 0.0);
}

TEST_CASE("Units support arithmetic within the same dimension", "[Units]")
{
    Seconds a {2.0};
    Seconds b {3.0};
    auto    c = a + b;
    CHECK(c.GetValue() == 5.0);

    auto d = c - a;
    CHECK(d.GetValue() == 3.0);
}

TEST_CASE("Units scale with scalar multiplication and division", "[Units]")
{
    Seconds seconds {2.5};
    auto    scaled = seconds * 4.0;
    CHECK(scaled.GetValue() == 10.0);

    auto reduced = scaled / 2.0;
    CHECK(reduced.GetValue() == 5.0);
}

TEST_CASE("Units convert across compatible ratios and value types", "[Units]")
{
    Seconds      seconds {1.5};
    Milliseconds milliseconds = UnitCast<Milliseconds>(seconds);
    CHECK(milliseconds.GetValue() == 1500.0);

    Seconds secondsRoundTrip = UnitCast<Seconds>(milliseconds);
    CHECK(secondsRoundTrip.GetValue() == 1.5);

    auto millisecondsFloat = ValueCast<float>(milliseconds);
    CHECK(millisecondsFloat.GetValue() == 1500.0f);

    auto millisecondsDouble = ValueCast<double>(millisecondsFloat);
    CHECK(millisecondsDouble.GetValue() == 1500.0);
}

TEST_CASE("Units support algebraic multiplication", "[Units]")
{
    Seconds seconds {2.0};
    auto    squared = seconds * seconds;
    CHECK(squared.GetValue() == 4.0);
}

TEST_CASE("Derived units behave as expected", "[Units]")
{
    Meters  distance {10.0};
    Seconds time {2.0};
    auto    velocity = distance / time;
    CHECK(velocity.GetValue() == 5.0);

    using MetersPerSecond   = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), double, RatioPolicy<1, 1>>;
    using KilometersPerHour = Unit<AddExponents(LENGTH, QuantityExponents {0, 0, -1, 0, 0, 0, 0}), double, RatioPolicy<1000, 3600>>;

    MetersPerSecond   metersPerSecond {10.0};
    KilometersPerHour kilometersPerHour = UnitCast<KilometersPerHour>(metersPerSecond);
    CHECK(kilometersPerHour.GetValue() == 36.0);

    MetersPerSecond metersPerSecondRoundTrip = UnitCast<MetersPerSecond>(kilometersPerHour);
    CHECK(metersPerSecondRoundTrip.GetValue() == 10.0);
}

TEST_CASE("Unit equality compares values", "[Units]")
{
    Seconds a {1.0};
    Seconds b {1.0};
    Seconds c {2.0};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("Temperature conversions round-trip correctly", "[Units]")
{
    Celsius celsius {100.0};
    Kelvin  kelvin = UnitCast<Kelvin>(celsius);
    CHECK(kelvin.GetValue() == 373.15);

    Celsius celsiusRoundTrip = UnitCast<Celsius>(kelvin);
    CHECK(celsiusRoundTrip.GetValue() == 100.0);

    Fahrenheit fahrenheit {32.0};
    Kelvin     kelvinFromFahrenheit = UnitCast<Kelvin>(fahrenheit);
    CHECK(std::abs(kelvinFromFahrenheit.GetValue() - 273.15) < 1e-10);

    Fahrenheit fahrenheitRoundTrip = UnitCast<Fahrenheit>(kelvinFromFahrenheit);
    CHECK(std::abs(fahrenheitRoundTrip.GetValue() - 32.0) < 1e-10);
}

TEST_CASE("Users can extend units with custom quantities", "[Units]")
{
    static constexpr QuantityExponents myQuantity = [] {
        QuantityExponents exponents {};
        exponents.exponents = {1, 2, 3, 4, 5, 6, 7};
        return exponents;
    }();

    using MyUnit = Unit<myQuantity, float, RatioPolicy<42, 1>>;
    MyUnit custom {2.0f};
    CHECK(custom.GetValue() == 2.0f);
    CHECK(custom.ToBase() == 84.0f);
}

TEST_CASE("Units stream and format with symbols", "[Units]")
{
    std::stringstream stream;
    Seconds           seconds {42.0};
    stream << seconds;
    CHECK(stream.str() == "42 s");

#if defined(__cpp_lib_format)
    auto formatted = std::format("{:.1f}", seconds);
    CHECK(formatted == "42.0 s");
#endif
}
