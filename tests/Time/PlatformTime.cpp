/// @file PlatformTime.cpp
/// @brief Focused tests for platform-backed time helpers.

#include "../../src/NGIN/Time/PerformanceCounter.hpp"
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Timer.hpp>
#include <NGIN/Units.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Time performance counter conversion preserves long uptime and fractional ticks", "[Time][MonotonicClock]")
{
    using NGIN::Time::detail::PerformanceCounterNanoseconds;
    CHECK(PerformanceCounterNanoseconds(0, 10'000'000) == 0);
    CHECK(PerformanceCounterNanoseconds(1, 10'000'000) == 100);
    // Both sides of the old intermediate overflow, and one year of uptime.
    CHECK(PerformanceCounterNanoseconds(18'446'744'073, 10'000'000) == 1'844'674'407'300);
    CHECK(PerformanceCounterNanoseconds(18'446'744'074, 10'000'000) == 1'844'674'407'400);
    CHECK(PerformanceCounterNanoseconds(315'360'000'000'007, 10'000'000) == 31'536'000'000'000'700);
    CHECK(PerformanceCounterNanoseconds(10, 3) == 3'333'333'333);

    constexpr NGIN::UInt64 maximum = std::numeric_limits<NGIN::UInt64>::max();
    CHECK(PerformanceCounterNanoseconds(maximum - 1, maximum) == 999'999'999);
    CHECK(PerformanceCounterNanoseconds(maximum / 2, maximum) == 499'999'999);
    CHECK(PerformanceCounterNanoseconds(maximum, maximum) == 1'000'000'000);
    CHECK(PerformanceCounterNanoseconds(maximum, 1'000'000'000) == maximum);
    CHECK(PerformanceCounterNanoseconds(maximum, 1) == maximum);
    CHECK(PerformanceCounterNanoseconds(55'340'232'221'128'655, 3'000'000) == maximum);
}

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
