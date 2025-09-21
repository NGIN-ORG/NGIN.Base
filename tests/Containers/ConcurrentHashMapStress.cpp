/// @file ConcurrentHashMapStress.cpp
/// @brief Phase 5 concurrency & stress tests for ConcurrentHashMap using Catch2.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

using NGIN::Containers::ConcurrentHashMap;

TEST_CASE("ConcurrentHashMap builds collision chains", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(2);
    for (int i = 0; i < 10; ++i)
    {
        map.Insert(i * 2, i);
    }

    for (int i = 0; i < 10; ++i)
    {
        CHECK(map.Contains(i * 2));
    }
    CHECK(map.Size() == 10U);
}

TEST_CASE("ConcurrentHashMap resizes under concurrent inserts", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(8);
    constexpr int               threadCount      = 8;
    constexpr int               insertsPerThread = 4000;
    std::vector<std::thread>    workers;

    for (int t = 0; t < threadCount; ++t)
    {
        workers.emplace_back([t, &map] {
            const int base = t * insertsPerThread;
            for (int i = 0; i < insertsPerThread; ++i)
            {
                map.Insert(base + i, base + i);
            }
        });
    }

    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(map.Size() == static_cast<std::size_t>(threadCount * insertsPerThread));
    CHECK(map.Contains(0));
    CHECK(map.Contains((threadCount - 1) * insertsPerThread));
}

TEST_CASE("ConcurrentHashMap mixed read/write stress", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(32);
    constexpr int               writerThreads = 4;
    constexpr int               readerThreads = 8;
    constexpr int               opsPerWriter  = 5000;
    constexpr int               opsPerReader  = 10000;

    std::atomic<bool>        start {false};
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;

    for (int w = 0; w < writerThreads; ++w)
    {
        writers.emplace_back([w, &map, &start] {
            std::mt19937                       rng(1234 + w);
            std::uniform_int_distribution<int> keys(0, 20000);
            while (!start.load())
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < opsPerWriter; ++i)
            {
                int key = keys(rng);
                if ((i & 7) == 0)
                {
                    map.Remove(key);
                }
                map.Insert(key, key);
            }
        });
    }

    for (int r = 0; r < readerThreads; ++r)
    {
        readers.emplace_back([r, &map, &start] {
            std::mt19937                       rng(5678 + r);
            std::uniform_int_distribution<int> keys(0, 20000);
            while (!start.load())
            {
                std::this_thread::yield();
            }
            int dummy = 0;
            for (int i = 0; i < opsPerReader; ++i)
            {
                map.TryGet(keys(rng), dummy);
            }
        });
    }

    start.store(true);

    for (auto& writer: writers)
    {
        writer.join();
    }
    for (auto& reader: readers)
    {
        reader.join();
    }

    CHECK(map.Size() <= 20001U);
}
