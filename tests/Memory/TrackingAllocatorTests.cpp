/// @file TrackingAllocatorTests.cpp
/// @brief Tests for Tracking allocator decorator.

#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Tracking allocator accumulates and resets statistics", "[Memory][TrackingAllocator]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::LinearAllocator<>>;
    Tracked tracked {NGIN::Memory::LinearAllocator {256}};

    void* first  = tracked.Allocate(32, 8);
    void* second = tracked.Allocate(16, 8);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    auto stats = tracked.GetStats();
    CHECK(stats.currentBytes == 48U);
    CHECK(stats.peakBytes == 48U);

    tracked.Deallocate(first, 32, 8);
    stats = tracked.GetStats();
    CHECK(stats.currentBytes == 16U);

    tracked.Deallocate(second, 16, 8);
    stats = tracked.GetStats();
    CHECK(stats.currentBytes == 0U);
    CHECK(stats.peakBytes >= 48U);
}

TEST_CASE("Tracking allocator works with system allocator backend", "[Memory][TrackingAllocator]")
{
    using TrackedSys = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
    TrackedSys tracked {NGIN::Memory::SystemAllocator {}};

    void* first  = tracked.Allocate(64, alignof(std::max_align_t));
    void* second = tracked.Allocate(32, alignof(std::max_align_t));

    CHECK(tracked.GetStats().currentBytes == 96U);

    tracked.Deallocate(first, 64, alignof(std::max_align_t));
    CHECK(tracked.GetStats().currentBytes == 32U);

    tracked.Deallocate(second, 32, alignof(std::max_align_t));
    CHECK(tracked.GetStats().currentBytes == 0U);
}
