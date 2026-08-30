#include <NGIN/NGIN.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Foundation umbrella compiles")
{
    STATIC_REQUIRE(sizeof(NGIN::Byte) == 1);
}
