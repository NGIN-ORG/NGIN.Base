/// @file ThreadSafeAllocatorTests.cpp
/// @brief Tests focused on ThreadSafeAllocator wrapper behavior.

#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/ThreadSafeAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("ThreadSafeAllocator allocates and deallocates", "[Memory][ThreadSafeAllocator]")
{
    using Arena      = NGIN::Memory::LinearAllocator<>;
    using ThreadSafe = NGIN::Memory::ThreadSafeAllocator<Arena>;

    ThreadSafe allocator {Arena {256}};
    void*      first  = allocator.Allocate(32, 8);
    void*      second = allocator.Allocate(32, 8);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    allocator.Deallocate(first, 32, 8);
    allocator.Deallocate(second, 32, 8);
}

TEST_CASE("ThreadSafeAllocator exposes ownership checks", "[Memory][ThreadSafeAllocator]")
{
    using Arena      = NGIN::Memory::LinearAllocator<>;
    using ThreadSafe = NGIN::Memory::ThreadSafeAllocator<Arena>;

    ThreadSafe allocator {Arena {128}};
    void*      pointer = allocator.Allocate(16, 8);
    REQUIRE(pointer != nullptr);
    CHECK(allocator.Owns(pointer));
    allocator.Deallocate(pointer, 16, 8);
}

TEST_CASE("ThreadSafeAllocator handles concurrent access", "[Memory][ThreadSafeAllocator]")
{
    using Arena      = NGIN::Memory::LinearAllocator<>;
    using ThreadSafe = NGIN::Memory::ThreadSafeAllocator<Arena>;

    ThreadSafe    allocator {Arena {8 * 1024}};
    constexpr int threadCount = 8;
    constexpr int iterations  = 1000;

    std::atomic<int>         allocationCount {0};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        workers.emplace_back([&] {
            for (int iteration = 0; iteration < iterations; ++iteration)
            {
                void* block = allocator.Allocate(8, alignof(std::max_align_t));
                if (block != nullptr)
                {
                    ++allocationCount;
                    allocator.Deallocate(block, 8, alignof(std::max_align_t));
                }
            }
        });
    }

    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(allocationCount.load() > 0);
}

TEST_CASE("ThreadSafeAllocator composes with tracking decorator", "[Memory][ThreadSafeAllocator]")
{
    using Arena      = NGIN::Memory::LinearAllocator<>;
    using Tracked    = NGIN::Memory::Tracking<Arena>;
    using ThreadSafe = NGIN::Memory::ThreadSafeAllocator<Tracked>;

    ThreadSafe allocator {Tracked {Arena {512}}};
    void*      first  = allocator.Allocate(64, 16);
    void*      second = allocator.Allocate(32, 8);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    auto& tracked = allocator.InnerAllocator();
    auto  stats   = tracked.GetStats();
    CHECK(stats.currentBytes == 96U);

    allocator.Deallocate(first, 64, 16);
    allocator.Deallocate(second, 32, 8);
    stats = tracked.GetStats();
    CHECK(stats.currentBytes == 0U);
}
