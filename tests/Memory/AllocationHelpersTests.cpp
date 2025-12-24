/// @file AllocationHelpersTests.cpp
/// @brief Tests for AllocationHelpers object/array utilities.

#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <type_traits>

namespace
{
    struct alignas(64) OverAligned
    {
        std::byte payload[3] {};
        int       value {0};
    };

    static_assert(alignof(OverAligned) >= 64);
    static_assert(std::is_trivially_destructible_v<OverAligned>);
}

TEST_CASE("AllocationHelpers allocate/deallocate over-aligned arrays correctly", "[Memory][AllocationHelpers]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
    Tracked tracked {NGIN::Memory::SystemAllocator {}};

    OverAligned* ptr = NGIN::Memory::AllocateArray<OverAligned>(tracked, 8);
    REQUIRE(ptr != nullptr);
    CHECK((reinterpret_cast<std::uintptr_t>(ptr) % alignof(OverAligned)) == 0);

    NGIN::Memory::DeallocateArray(tracked, ptr);
    CHECK(tracked.GetStats().currentBytes == 0);
}

TEST_CASE("AllocationHelpers detects size overflow and throws", "[Memory][AllocationHelpers]")
{
    NGIN::Memory::SystemAllocator alloc {};

    const std::size_t tooLargeCount = (std::numeric_limits<std::size_t>::max() / sizeof(OverAligned)) + 1;
    REQUIRE_THROWS_AS(NGIN::Memory::AllocateArray<OverAligned>(alloc, tooLargeCount), std::bad_alloc);
}

TEST_CASE("AllocationHelpers throws on allocator exhaustion", "[Memory][AllocationHelpers]")
{
    NGIN::Memory::LinearAllocator arena {128};
    REQUIRE_THROWS_AS(NGIN::Memory::AllocateArray<OverAligned>(arena, 16), std::bad_alloc);
}

