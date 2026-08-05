/// @file ConcurrentHashMapCoverage.cpp
/// @brief Reclamation and integrity coverage for the rebuilt ConcurrentHashMap scaffold.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

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

    template<NGIN::Containers::ReclamationPolicy Policy>
    void VerifyPinnedReaderDefersReclamation()
    {
        CountingAllocatorStats stats;
        CountingAllocator      allocator {stats};
        CountingMap<Policy>    map(8, {}, {}, allocator);
        REQUIRE(map.Insert(1, 10));

        std::atomic<bool> readerPinned {false};
        std::atomic<bool> releaseReader {false};
        std::thread       reader([&]() {
            map.ForEach([&](const int&, const int&) {
                readerPinned.store(true, std::memory_order_release);
                while (!releaseReader.load(std::memory_order_acquire))
                    std::this_thread::yield();
            });
        });

        while (!readerPinned.load(std::memory_order_acquire))
            std::this_thread::yield();
        CHECK(map.ActiveReaders() == 1);
        const auto reclaimedBefore = map.ReclaimedRetired();
        CHECK_FALSE(map.InsertOrAssign(1, 20));
        CHECK(map.PendingRetired() > 0);
        CHECK(map.ReclaimedRetired() == reclaimedBefore);
        map.Reserve(256);
        CHECK(map.PendingRetired() > 0);

        releaseReader.store(true, std::memory_order_release);
        reader.join();
        CHECK(map.ActiveReaders() == 0);
        map.Quiesce();
        CHECK(map.PendingRetired() == 0);
        CHECK(map.ReclaimedRetired() > reclaimedBefore);
        CHECK(map.Get(1) == 20);
    }

    template<NGIN::Containers::ReclamationPolicy Policy>
    void VerifyDestructionWaitsForReader()
    {
        using Map = CountingMap<Policy>;
        auto map  = std::make_unique<Map>(8);
        REQUIRE(map->Insert(7, 70));
        Map* raw = map.get();

        std::atomic<bool> readerPinned {false};
        std::atomic<bool> releaseReader {false};
        std::atomic<bool> destroyed {false};
        std::thread       reader([&]() {
            raw->ForEach([&](const int&, const int&) {
                readerPinned.store(true, std::memory_order_release);
                while (!releaseReader.load(std::memory_order_acquire))
                    std::this_thread::yield();
            });
        });
        while (!readerPinned.load(std::memory_order_acquire))
            std::this_thread::yield();

        std::thread destructor([&]() {
            map.reset();
            destroyed.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK_FALSE(destroyed.load(std::memory_order_acquire));
        releaseReader.store(true, std::memory_order_release);
        reader.join();
        destructor.join();
        CHECK(destroyed.load(std::memory_order_acquire));
    }
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

TEST_CASE("ConcurrentHashMap automatic policies reclaim opportunistically", "[Containers][ConcurrentHashMap][Coverage]")
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

TEST_CASE("ConcurrentHashMap automatic policies defer storage held by pinned readers", "[Containers][ConcurrentHashMap][Coverage]")
{
    SECTION("LocalEpoch")
    {
        VerifyPinnedReaderDefersReclamation<NGIN::Containers::ReclamationPolicy::LocalEpoch>();
    }
    SECTION("HazardPointers")
    {
        VerifyPinnedReaderDefersReclamation<NGIN::Containers::ReclamationPolicy::HazardPointers>();
    }
}

TEST_CASE("ConcurrentHashMap automatic policy destruction waits for readers", "[Containers][ConcurrentHashMap][Coverage]")
{
    SECTION("LocalEpoch")
    {
        VerifyDestructionWaitsForReader<NGIN::Containers::ReclamationPolicy::LocalEpoch>();
    }
    SECTION("HazardPointers")
    {
        VerifyDestructionWaitsForReader<NGIN::Containers::ReclamationPolicy::HazardPointers>();
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
