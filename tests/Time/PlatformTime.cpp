/// @file PlatformTime.cpp
/// @brief Focused tests for platform-backed time helpers.

#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Timer.hpp>
#include <NGIN/Units.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Time.MonotonicClock is monotonic across SleepFor", "[Time][MonotonicClock][Sleep]")
{
    const auto before = NGIN::Time::MonotonicClock::Now();
    NGIN::Time::SleepFor(NGIN::Units::Milliseconds(1.0));
    const auto after = NGIN::Time::MonotonicClock::Now();

    CHECK(after >= before);
}

TEST_CASE("Timer reset clears elapsed state and stops the timer", "[Time][Timer]")
{
    NGIN::Timer timer;
    timer.Start();
    NGIN::Time::SleepFor(NGIN::Units::Milliseconds(1.0));
    timer.Stop();

    CHECK_FALSE(timer.IsRunning());
    CHECK(timer.GetElapsed<NGIN::Units::Nanoseconds>().GetValue() > 0.0);

    timer.Reset();

    CHECK_FALSE(timer.IsRunning());
    CHECK(timer.GetElapsed<NGIN::Units::Nanoseconds>().GetValue() == 0.0);
}

TEST_CASE("Timer stop is idempotent", "[Time][Timer]")
{
    NGIN::Timer timer;
    timer.Start();
    timer.Stop();
    const auto elapsed = timer.GetElapsed<NGIN::Units::Nanoseconds>().GetValue();

    NGIN::Time::SleepFor(NGIN::Units::Milliseconds(1.0));
    timer.Stop();

    CHECK(timer.GetElapsed<NGIN::Units::Nanoseconds>().GetValue() == elapsed);
}
