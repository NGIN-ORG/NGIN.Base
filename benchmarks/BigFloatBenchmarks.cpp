#include <NGIN/Benchmark.hpp>
#include <NGIN/Math/BigFloat.hpp>
#include <iostream>
#include <vector>

using NGIN::Benchmark;
using NGIN::BenchmarkContext;
using NGIN::Math::BigFloat;
using NGIN::Units::Microseconds;

int main()
{
    using Real = BigFloat<256>;

    Benchmark::defaultConfig.iterations       = 500;
    Benchmark::defaultConfig.warmupIterations = 20;

    const Real left("3.1415926535897932384626433832795028841971693993751");
    const Real right("2.7182818284590452353602874713526624977572470936999");
    const Real addend("-1.4142135623730950488016887242096980785696718753769");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        Real result = left * right;
        context.doNotOptimize(result);
        context.stop();
    },
                        "BigFloat<256> multiplication");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        Real result = left / right;
        context.doNotOptimize(result);
        context.stop();
    },
                        "BigFloat<256> division");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        Real result = NGIN::Math::Fma(left, right, addend);
        context.doNotOptimize(result);
        context.stop();
    },
                        "BigFloat<256> fused multiply-add");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        Real result = NGIN::Math::Sqrt(left);
        context.doNotOptimize(result);
        context.stop();
    },
                        "BigFloat<256> square root");

    const std::vector<NGIN::BenchmarkResult<Microseconds>> results = Benchmark::RunAll<Microseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
