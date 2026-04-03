/// @file PlatformTime.cpp
/// @brief Focused tests for platform-backed time helpers.

#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Time.MonotonicClock is monotonic across SleepFor", "[Time][MonotonicClock][Sleep]")
{
    const auto before = NGIN::Time::MonotonicClock::Now();
    NGIN::Time::SleepFor(NGIN::Units::Milliseconds(1.0));
    const auto after = NGIN::Time::MonotonicClock::Now();

    CHECK(after >= before);
}
