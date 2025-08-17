/// @file ConcurrentHashMapStress.cpp
/// @brief Phase 5 concurrency & stress tests for ConcurrentHashMap.
///
/// Focus: chaining, online resize under concurrent insertion, mixed read/write workload.
/// These are intentionally lightweight to keep CI fast while still exercising key paths.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <boost/ut.hpp>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>

using namespace boost::ut;

suite<"ConcurrentHashMapStress"> concurrent_hashmap_stress = [] {
    // Force creation of chain beyond a single VirtualBucket (uses integer hash = identity)
    "ChainingMultipleNodes"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map(2);// very small, 2 * 8 slots threshold before resize
        // Threshold for resize start (load factor > 0.75 of total slots = 2*8*0.75 = 12)
        // Insert 10 even numbers -> all map to bucket index 0 (mask=1) before resize kicks in.
        for (int i = 0; i < 10; ++i)
            map.Insert(i * 2, i);
        for (int i = 0; i < 10; ++i)
            expect(map.Contains(i * 2));
        expect(map.Size() == 10_ul);
    };

    // Concurrent insertion causing multiple cooperative resizes.
    "ConcurrentOnlineResize"_test = [] {
        using Map = NGIN::Containers::ConcurrentHashMap<int, int>;
        Map                      map(8);
        constexpr int            threads   = 8;
        constexpr int            perThread = 4000;// 32k inserts total -> several resizes
        std::vector<std::thread> ts;
        for (int t = 0; t < threads; ++t)
        {
            ts.emplace_back([t, &map] {
                int base = t * perThread;
                for (int i = 0; i < perThread; ++i)
                    map.Insert(base + i, base + i);
            });
        }
        for (auto& th: ts)
            th.join();
        expect(map.Size() == static_cast<std::size_t>(threads * perThread));
        // Ensure any in-progress cooperative resize is fully drained before spot checks
        //   map.DebugDrainResize();

        // spot check a few keys from different threads
        expect(map.Contains(0));
        expect(map.Contains((threads - 1) * perThread));
    };

    // Mixed workload stress (bounded) – 80% reads, 20% writes
    "MixedReadWriteStressBasic"_test = [] {
        using Map = NGIN::Containers::ConcurrentHashMap<int, int>;
        Map                      map(32);
        constexpr int            writerThreads = 4;
        constexpr int            readerThreads = 8;
        constexpr int            opsPerWriter  = 5000;
        constexpr int            opsPerReader  = 10000;
        std::atomic<bool>        start {false};
        std::vector<std::thread> writers;
        std::vector<std::thread> readers;
        for (int w = 0; w < writerThreads; ++w)
        {
            writers.emplace_back([w, &map, &start] {
                std::mt19937                       rng(1234 + w);
                std::uniform_int_distribution<int> keyDist(0, 20000);
                while (!start.load())
                    std::this_thread::yield();
                for (int i = 0; i < opsPerWriter; ++i)
                {
                    int k = keyDist(rng);
                    if ((i & 7) == 0)// ~12.5% removes (subset of 20% writes with reinserts)
                        map.Remove(k);
                    map.Insert(k, k);
                }
            });
        }
        for (int r = 0; r < readerThreads; ++r)
        {
            readers.emplace_back([r, &map, &start] {
                std::mt19937                       rng(5678 + r);
                std::uniform_int_distribution<int> keyDist(0, 20000);
                while (!start.load())
                    std::this_thread::yield();
                int dummy = 0;
                for (int i = 0; i < opsPerReader; ++i)
                {
                    int k = keyDist(rng);
                    map.TryGet(k, dummy);// ignore result
                }
            });
        }
        start.store(true);
        for (auto& th: writers)
            th.join();
        for (auto& th: readers)
            th.join();
        // Sanity: no crash and size not absurdly large; upper bound is number of distinct keys touched.
        expect(map.Size() <= 20001_ul);
    };
};
