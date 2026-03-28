/// @file ConcurrentHashMapStress.cpp
/// @brief Concurrency smoke tests for the rebuilt ConcurrentHashMap scaffold.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace
{
    using SmokeMap = NGIN::Containers::ConcurrentHashMap<int, int>;
}

TEST_CASE("ConcurrentHashMap handles concurrent disjoint inserts", "[Containers][ConcurrentHashMap][Stress]")
{
    SmokeMap                 map(64);
    constexpr int            threadCount      = 8;
    constexpr int            insertsPerThread = 2000;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back([threadIndex, &map]() {
            const int base = threadIndex * insertsPerThread;
            for (int i = 0; i < insertsPerThread; ++i)
            {
                (void) map.Insert(base + i, base + i);
            }
        });
    }

    for (auto& worker: workers)
    {
        worker.join();
    }

    REQUIRE(map.Size() == static_cast<std::size_t>(threadCount * insertsPerThread));
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        const int sampleKey = threadIndex * insertsPerThread + 17;
        CHECK(map.Get(sampleKey) == sampleKey);
    }
}

TEST_CASE("ConcurrentHashMap supports read-write contention without deadlock", "[Containers][ConcurrentHashMap][Stress]")
{
    SmokeMap          map(64);
    std::atomic<bool> start {false};
    std::atomic<bool> stopReaders {false};

    constexpr int readerCount = 4;
    constexpr int writerOps   = 1500;

    std::vector<std::thread> readers;
    readers.reserve(readerCount);

    for (int readerIndex = 0; readerIndex < readerCount; ++readerIndex)
    {
        readers.emplace_back([&]() {
            int sink = 0;
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            while (!stopReaders.load(std::memory_order_acquire))
            {
                (void) map.TryGet(17, sink);
                (void) map.Contains(42);
            }
        });
    }

    std::thread writer([&]() {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        for (int i = 0; i < writerOps; ++i)
        {
            (void) map.Insert(i, i * 2);
            if ((i & 7) == 0)
            {
                (void) map.Remove(i / 2);
            }
        }
    });

    start.store(true, std::memory_order_release);
    writer.join();
    stopReaders.store(true, std::memory_order_release);

    for (auto& reader: readers)
    {
        reader.join();
    }

    map.Quiesce();
    CHECK(map.Capacity() >= 64U);
}

TEST_CASE("ConcurrentHashMap reserve can run between write phases", "[Containers][ConcurrentHashMap][Stress]")
{
    SmokeMap map(8);

    for (int i = 0; i < 128; ++i)
    {
        REQUIRE(map.Insert(i, i + 1));
    }

    map.Reserve(2048);

    std::thread writer([&]() {
        for (int i = 128; i < 512; ++i)
        {
            (void) map.Insert(i, i + 1);
        }
    });

    std::thread reader([&]() {
        int sink = 0;
        for (int i = 0; i < 512; ++i)
        {
            (void) map.TryGet(i, sink);
        }
    });

    writer.join();
    reader.join();

    CHECK(map.Capacity() >= 2048U);
    CHECK(map.Contains(255));
}
