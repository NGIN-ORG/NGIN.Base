#include <NGIN/Benchmark.hpp>
#include <NGIN/Containers/HashMap.hpp>
#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <iostream>
#include <random>
#include <NGIN/Async/FiberScheduler.hpp>
#include <NGIN/Async/ThreadPoolScheduler.hpp>
#include <coroutine>

int main()
{
    using namespace NGIN;


    // --- FlatHashMap Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::FlatHashMap<int, int> map;
        for (int i = 0; i < 1000; ++i)
            map.Insert(i, i);
        int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += map.Get(i);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "FlatHashMap<int,int> Insert+Get 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::FlatHashMap<int, int> map;
        std::vector<int>                        keys(1000);
        for (int i = 0; i < 1000; ++i)
            keys[i] = i;
        std::mt19937 rng(42);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            map.Insert(keys[i], keys[i]);
        int sum = 0;
        rng.seed(43);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            sum += map.Get(keys[i]);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "FlatHashMap<int,int> RandomInsert+RandomGet 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::FlatHashMap<int, int> map;
        for (int i = 0; i < 1000; ++i)
            map.Insert(i, i);
        for (int i = 0; i < 1000; ++i)
            map.Remove(i);
        ctx.stop();
    },
                        "FlatHashMap<int,int> Insert+SequentialRemove 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::FlatHashMap<int, int> map;
        std::vector<int>                        keys(1000);
        for (int i = 0; i < 1000; ++i)
            keys[i] = i;
        for (int i = 0; i < 1000; ++i)
            map.Insert(i, i);
        std::mt19937 rng(43);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            map.Remove(keys[i]);
        ctx.stop();
    },
                        "FlatHashMap<int,int> Insert+RandomRemove 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::FlatHashMap<int, int> map;
        for (int i = 0; i < 1000; ++i)
            map.Insert(i, i);
        int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += map.Get(i);
        for (int i = 0; i < 1000; i += 2)
            map.Remove(i);
        for (int i = 0; i < 1000; ++i)
            if (map.Contains(i))
                sum += map.Get(i);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "FlatHashMap<int,int> MixedWorkload 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::ConcurrentHashMap<int, int> cmap;
        for (int i = 0; i < 1000; ++i)
            cmap.Insert(i, i);
        int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += cmap.Get(i);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "ConcurrentHashMap<int,int> Insert+Get 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::ConcurrentHashMap<int, int> cmap;
        std::vector<int>                              keys(1000);
        for (int i = 0; i < 1000; ++i)
            keys[i] = i;
        std::mt19937 rng(44);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            cmap.Insert(keys[i], keys[i]);
        int sum = 0;
        rng.seed(45);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            sum += cmap.Get(keys[i]);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "ConcurrentHashMap<int,int> RandomInsert+RandomGet 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::ConcurrentHashMap<int, int> cmap;
        for (int i = 0; i < 1000; ++i)
            cmap.Insert(i, i);
        for (int i = 0; i < 1000; ++i)
            cmap.Remove(i);
        ctx.stop();
    },
                        "ConcurrentHashMap<int,int> Insert+SequentialRemove 1000");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::ConcurrentHashMap<int, int> cmap;
        std::vector<int>                              keys(1000);
        for (int i = 0; i < 1000; ++i)
            keys[i] = i;
        for (int i = 0; i < 1000; ++i)
            cmap.Insert(i, i);
        std::mt19937 rng(45);
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 1000; ++i)
            cmap.Remove(keys[i]);
        ctx.stop();
    },
                        "ConcurrentHashMap<int,int> Insert+RandomRemove 1000");

    // --- Mixed workload: Insert, Get, Remove ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Containers::ConcurrentHashMap<int, int> cmap;
        for (int i = 0; i < 1000; ++i)
            cmap.Insert(i, i);
        int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += cmap.Get(i);
        for (int i = 0; i < 1000; i += 2)
            cmap.Remove(i);
        for (int i = 0; i < 1000; ++i)
            if (cmap.Contains(i))
                sum += cmap.Get(i);
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "ConcurrentHashMap<int,int> MixedWorkload 1000");


    // Run all benchmarks and print results
    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
