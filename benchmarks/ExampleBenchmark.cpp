#include <NGIN/Benchmark.hpp>
#include <NGIN/Containers/HashMap.hpp>
#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <iostream>

int main()
{
    using namespace NGIN;


    // Benchmark: FlatHashMap insert and lookup
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
    }, "FlatHashMap<int,int> Insert+Get 1000");

    // Benchmark: ConcurrentHashMap insert and lookup
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
    }, "ConcurrentHashMap<int,int> Insert+Get 1000");

    // Run all benchmarks and print results
    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
