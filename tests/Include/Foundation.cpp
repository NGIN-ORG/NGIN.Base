#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Foundation public headers compile together")
{
    STATIC_REQUIRE(sizeof(NGIN::Byte) == 1);
}
