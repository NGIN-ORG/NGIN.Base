#include <NGIN/Benchmark.hpp>
#include <iostream>

int main()
{
    using namespace NGIN;

    // Register a simple benchmark
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        // Example code to benchmark: sum numbers
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += i;
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "Sum 0..999");

    // Run all benchmarks and print results
    auto results = Benchmark::RunAll<Nanoseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
