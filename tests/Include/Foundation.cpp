#include <NGIN/NGIN.hpp>
#include <NGIN/Time/MonotonicClock.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Foundation umbrella compiles with focused time APIs")
{
    STATIC_REQUIRE(sizeof(NGIN::Byte) == 1);
}
