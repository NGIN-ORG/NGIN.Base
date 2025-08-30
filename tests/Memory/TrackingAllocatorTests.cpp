/// @file TrackingAllocatorTests.cpp
/// @brief Tests for Tracking allocator decorator.

#include <boost/ut.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

using namespace boost::ut;

suite<"NGIN::Memory::TrackingAllocator"> trackingAllocatorSuite = [] {
    "AccumulatesAndResets"_test = [] {
        using Tracked = NGIN::Memory::Tracking<NGIN::Memory::LinearAllocator<>>;
        Tracked tracked {NGIN::Memory::LinearAllocator {256}};
        void*   a = tracked.Allocate(32, 8);
        void*   b = tracked.Allocate(16, 8);
        expect(a != nullptr && b != nullptr);
        auto stats = tracked.GetStats();
        expect(stats.currentBytes == 48_u);
        expect(stats.peakBytes == 48_u);
        tracked.Deallocate(a, 32, 8);
        stats = tracked.GetStats();
        expect(stats.currentBytes == 16_u);
        tracked.Deallocate(b, 16, 8);
        stats = tracked.GetStats();
        expect(stats.currentBytes == 0_u);
        expect(stats.peakBytes >= 48_u);
    };

    "SystemAllocatorBackend"_test = [] {
        using TrackedSys = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
        TrackedSys tracked {NGIN::Memory::SystemAllocator {}};
        void*      p1 = tracked.Allocate(64, alignof(std::max_align_t));
        void*      p2 = tracked.Allocate(32, alignof(std::max_align_t));
        expect(tracked.GetStats().currentBytes == 96_u);
        tracked.Deallocate(p1, 64, alignof(std::max_align_t));
        expect(tracked.GetStats().currentBytes == 32_u);
        tracked.Deallocate(p2, 32, alignof(std::max_align_t));
        expect(tracked.GetStats().currentBytes == 0_u);
    };
};
