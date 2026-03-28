/// @file ConcurrentHashMapCoverage.cpp
/// @brief Reclamation and integrity coverage for the rebuilt ConcurrentHashMap scaffold.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>

namespace
{
    struct CountingAllocatorStats
    {
        std::atomic<int> allocations {0};
        std::atomic<int> deallocations {0};
    };

    struct CountingAllocator
    {
        NGIN::Memory::SystemAllocator inner {};
        CountingAllocatorStats*       stats {nullptr};

        CountingAllocator() = default;
        explicit CountingAllocator(CountingAllocatorStats& value) noexcept
            : stats(&value)
        {
        }

        void* Allocate(std::size_t bytes, std::size_t alignment) noexcept
        {
            if (stats)
            {
                stats->allocations.fetch_add(1, std::memory_order_relaxed);
            }
            return inner.Allocate(bytes, alignment);
        }

        void Deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept
        {
            if (stats)
            {
                stats->deallocations.fetch_add(1, std::memory_order_relaxed);
            }
            inner.Deallocate(pointer, bytes, alignment);
        }
    };

    template<NGIN::Containers::ReclamationPolicy Policy>
    using CountingMap =
            NGIN::Containers::ConcurrentHashMap<int, int, std::hash<int>, std::equal_to<int>, CountingAllocator, Policy, 4>;
}// namespace

TEST_CASE("ConcurrentHashMap manual quiesce defers reclamation", "[Containers][ConcurrentHashMap][Coverage]")
{
    CountingAllocatorStats                                          stats;
    CountingAllocator                                               allocator {stats};
    CountingMap<NGIN::Containers::ReclamationPolicy::ManualQuiesce> map(8, {}, {}, allocator);

    REQUIRE(map.Insert(1, 10));
    const int deallocationsAfterInsert = stats.deallocations.load(std::memory_order_relaxed);

    CHECK_FALSE(map.Insert(1, 20));
    CHECK(map.Remove(1));

    const int deallocationsBeforeQuiesce = stats.deallocations.load(std::memory_order_relaxed);
    CHECK(deallocationsBeforeQuiesce == deallocationsAfterInsert);

    map.Quiesce();

    CHECK(stats.deallocations.load(std::memory_order_relaxed) > deallocationsBeforeQuiesce);
}

TEST_CASE("ConcurrentHashMap manual quiesce defers clear reclamation", "[Containers][ConcurrentHashMap][Coverage]")
{
    CountingAllocatorStats                                          stats;
    CountingAllocator                                               allocator {stats};
    CountingMap<NGIN::Containers::ReclamationPolicy::ManualQuiesce> map(8, {}, {}, allocator);

    for (int i = 0; i < 32; ++i)
    {
        REQUIRE(map.Insert(i, i * 2));
    }

    const int deallocationsBeforeClear = stats.deallocations.load(std::memory_order_relaxed);
    map.Clear();

    CHECK(map.Empty());
    CHECK(stats.deallocations.load(std::memory_order_relaxed) == deallocationsBeforeClear);

    map.Quiesce();

    CHECK(stats.deallocations.load(std::memory_order_relaxed) > deallocationsBeforeClear);
}

TEST_CASE("ConcurrentHashMap automatic scaffold policies reclaim opportunistically", "[Containers][ConcurrentHashMap][Coverage]")
{
    SECTION("LocalEpoch")
    {
        CountingAllocatorStats                                       stats;
        CountingAllocator                                            allocator {stats};
        CountingMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(8, {}, {}, allocator);

        REQUIRE(map.Insert(1, 10));
        const int deallocationsAfterInsert = stats.deallocations.load(std::memory_order_relaxed);

        CHECK_FALSE(map.Insert(1, 20));
        CHECK(stats.deallocations.load(std::memory_order_relaxed) > deallocationsAfterInsert);
    }

    SECTION("HazardPointers")
    {
        CountingAllocatorStats                                           stats;
        CountingAllocator                                                allocator {stats};
        CountingMap<NGIN::Containers::ReclamationPolicy::HazardPointers> map(8, {}, {}, allocator);

        REQUIRE(map.Insert(1, 10));
        const int deallocationsAfterInsert = stats.deallocations.load(std::memory_order_relaxed);

        CHECK_FALSE(map.Insert(1, 20));
        CHECK(stats.deallocations.load(std::memory_order_relaxed) > deallocationsAfterInsert);
    }
}

TEST_CASE("ConcurrentHashMap preserves data across reserve and clear", "[Containers][ConcurrentHashMap][Coverage]")
{
    NGIN::Containers::ConcurrentHashMap<int, int> map(8);
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE(map.Insert(i, i * 3));
    }

    map.Reserve(1024);

    for (int i = 0; i < 256; ++i)
    {
        CHECK(map.Get(i) == i * 3);
    }

    map.Clear();
    map.Quiesce();
    CHECK(map.Empty());
    CHECK_FALSE(map.Contains(42));
}
