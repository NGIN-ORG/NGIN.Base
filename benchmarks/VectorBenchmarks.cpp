#include <NGIN/Benchmark.hpp>
#include <NGIN/Containers/Vector.hpp>
#include <vector>
#include <numeric>
#include <random>
#include <iostream>

using NGIN::Benchmark;
using NGIN::BenchmarkContext;
using NGIN::Containers::Vector;
using NGIN::Units::Milliseconds;

namespace
{
    constexpr std::size_t N      = 20000;// elements per test
    constexpr std::size_t SmallN = 512;  // for smaller ops
    constexpr int Seed           = 12345;
}// namespace

int main()
{
    // Configuration
    Benchmark::defaultConfig.iterations       = 50;
    Benchmark::defaultConfig.warmupIterations = 3;

    // PushBack sequential ints ------------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        std::vector<int> v;
        v.reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.push_back(static_cast<int>(i));
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "std::vector<int> push_back N");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        Vector<int> v;
        v.Reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.PushBack(static_cast<int>(i));
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "NGIN::Vector<int> push_back N");

    // EmplaceBack objects ------------------------------------------------------
    struct Obj
    {
        int a;
        double b;
        Obj(int x, double y) : a(x), b(y) {}
    };
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        std::vector<Obj> v;
        v.reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.emplace_back(static_cast<int>(i), i * 0.5);
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "std::vector<Obj> emplace_back N");

    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        Vector<Obj> v;
        v.Reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.EmplaceBack(static_cast<int>(i), i * 0.5);
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "NGIN::Vector<Obj> emplace_back N");

    // Random access summation --------------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<int> v(N);
        std::iota(v.begin(), v.end(), 0);
        std::mt19937 rng(Seed);
        std::uniform_int_distribution<std::size_t> dist(0, N - 1);
        ctx.start();
        long long sum = 0;
        for (std::size_t i = 0; i < N; ++i)
            sum += v[dist(rng)];
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::vector<int> random access sum N");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Vector<int> v;
        v.Reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.PushBack(static_cast<int>(i));
        std::mt19937 rng(Seed);
        std::uniform_int_distribution<std::size_t> dist(0, N - 1);
        ctx.start();
        long long sum = 0;
        for (std::size_t i = 0; i < N; ++i)
            sum += v[dist(rng)];
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::Vector<int> random access sum N");

    // Insert at middle (PushAt) -----------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<int> v;
        v.reserve(SmallN * 2);
        for (std::size_t i = 0; i < SmallN; ++i)
            v.push_back((int) i);
        ctx.start();
        for (std::size_t i = 0; i < SmallN; ++i)
            v.insert(v.begin() + v.size() / 2, (int) i);
        ctx.doNotOptimize(v.size());
        ctx.stop();
    },
                        "std::vector<int> middle insert SmallN");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Vector<int> v;
        v.Reserve(SmallN * 2);
        for (std::size_t i = 0; i < SmallN; ++i)
            v.PushBack((int) i);
        ctx.start();
        for (std::size_t i = 0; i < SmallN; ++i)
            v.PushAt(v.Size() / 2, (int) i);
        ctx.doNotOptimize(v.Size());
        ctx.stop();
    },
                        "NGIN::Vector<int> middle PushAt SmallN");

    // Erase from middle -------------------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<int> v;
        v.reserve(SmallN * 2);
        for (std::size_t i = 0; i < SmallN * 2; ++i)
            v.push_back((int) i);
        ctx.start();
        for (std::size_t i = 0; i < SmallN; ++i)
            v.erase(v.begin() + v.size() / 2);
        ctx.doNotOptimize(v.size());
        ctx.stop();
    },
                        "std::vector<int> middle erase SmallN");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Vector<int> v;
        v.Reserve(SmallN * 2);
        for (std::size_t i = 0; i < SmallN * 2; ++i)
            v.PushBack((int) i);
        ctx.start();
        for (std::size_t i = 0; i < SmallN; ++i)
            v.Erase(v.Size() / 2);
        ctx.doNotOptimize(v.Size());
        ctx.stop();
    },
                        "NGIN::Vector<int> middle Erase SmallN");

    // Clear + reuse -----------------------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<int> v;
        v.reserve(N);
        ctx.start();
        for (int r = 0; r < 5; ++r)
        {
            v.clear();
            for (std::size_t i = 0; i < N; ++i)
                v.push_back((int) i);
        }
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "std::vector<int> clear+refill 5x");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Vector<int> v;
        v.Reserve(N);
        ctx.start();
        for (int r = 0; r < 5; ++r)
        {
            v.Clear();
            for (std::size_t i = 0; i < N; ++i)
                v.PushBack((int) i);
        }
        ctx.doNotOptimize(v.data());
        ctx.stop();
    },
                        "NGIN::Vector<int> Clear+refill 5x");

    // ShrinkToFit -------------------------------------------------------------
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<int> v;
        for (std::size_t i = 0; i < N; ++i)
            v.push_back((int) i);
        v.reserve(N * 2);
        ctx.start();
        v.shrink_to_fit();
        ctx.stop();
    },
                        "std::vector<int> shrink_to_fit N");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Vector<int> v;
        for (std::size_t i = 0; i < N; ++i)
            v.PushBack((int) i);
        v.Reserve(N * 2);
        ctx.start();
        v.ShrinkToFit();
        ctx.stop();
    },
                        "NGIN::Vector<int> ShrinkToFit N");

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
