/// @file ThreadSafeAllocatorTests.cpp
/// @brief Tests focused on ThreadSafeAllocator wrapper behavior.

#include <boost/ut.hpp>
#include <thread>
#include <vector>
#include <atomic>

#include <NGIN/Memory/ThreadSafeAllocator.hpp>
#include <NGIN/Memory/BumpArena.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>

using namespace boost::ut;

suite<"NGIN::Memory::ThreadSafeAllocator"> threadSafeAllocatorSuite = [] {
    "BasicAllocateDeallocate"_test = [] {
        using Arena = NGIN::Memory::BumpArena<>;
        using TS    = NGIN::Memory::ThreadSafeAllocator<Arena>;
        TS ts {Arena {256}};
        void* p1 = ts.Allocate(32, 8);
        void* p2 = ts.Allocate(32, 8);
        expect(p1 != nullptr && p2 != nullptr);
        ts.Deallocate(p1, 32, 8);
        ts.Deallocate(p2, 32, 8);
    };

    "OwnershipProxy"_test = [] {
        using Arena = NGIN::Memory::BumpArena<>;
        using TS    = NGIN::Memory::ThreadSafeAllocator<Arena>;
        TS ts {Arena {128}};
        void* p = ts.Allocate(16, 8);
        expect(p != nullptr);
        expect(ts.Owns(p));
        ts.Deallocate(p, 16, 8);
    };

    "ConcurrentStress"_test = [] {
        using Arena = NGIN::Memory::BumpArena<>;
        using TS    = NGIN::Memory::ThreadSafeAllocator<Arena>;
        // Larger arena to allow many small allocations.
        TS ts {Arena {8 * 1024}};
        constexpr int threads    = 8;
        constexpr int iterations = 1000;
        std::atomic<int> allocCount {0};
        std::vector<std::thread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back([&] {
                for (int i = 0; i < iterations; ++i)
                {
                    void* p = ts.Allocate(8, alignof(std::max_align_t));
                    if (p)
                    {
                        ++allocCount;
                        ts.Deallocate(p, 8, alignof(std::max_align_t));
                    }
                }
            });
        }
        for (auto& th: workers)
            th.join();
        expect(allocCount.load() > 0_i);
    };

    "TrackingDecoratorThreadSafe"_test = [] {
        using Arena   = NGIN::Memory::BumpArena<>;
        using Tracked = NGIN::Memory::Tracking<Arena>;
        using TS      = NGIN::Memory::ThreadSafeAllocator<Tracked>;
        TS ts {Tracked {Arena {512}}};
        void* a = ts.Allocate(64, 16);
        void* b = ts.Allocate(32, 8);
        expect(a != nullptr && b != nullptr);
        auto& innerTracked = ts.InnerAllocator();
        auto stats         = innerTracked.GetStats();
        expect(stats.currentBytes == 96_u);
        ts.Deallocate(a, 64, 16);
        ts.Deallocate(b, 32, 8);
        stats = innerTracked.GetStats();
        expect(stats.currentBytes == 0_u);
    };
};
