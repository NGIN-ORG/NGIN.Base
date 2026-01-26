#include <NGIN/Benchmark.hpp>
#include <NGIN/Text/String.hpp>
#include <string>
#include <iostream>
#include <random>

using NGIN::Benchmark;
using NGIN::BenchmarkContext;
using NGIN::Text::String;
using NGIN::Units::Milliseconds;

// Helper to generate random strings
template<typename StrType>
void fill_random_strings(std::vector<StrType>& vec, size_t count, size_t len)
{
    static const char                            charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::mt19937                          rng(123);
    static std::uniform_int_distribution<size_t> char_dist(0, sizeof(charset) - 2);
    vec.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        std::string s;
        s.reserve(len);
        for (size_t j = 0; j < len; ++j)
            s += charset[char_dist(rng)];
        vec[i] = StrType(s.c_str());
    }
}

int main()
{
    constexpr size_t N        = 10000;
    constexpr size_t shortLen = 8;
    constexpr size_t longLen  = 128;

    // --- Construction Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s("shortstr");
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s("shortstr");
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s(std::string(longLen, 'x'));
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string long construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s(std::string(longLen, 'x').c_str());
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String long construction");

    // --- Copy Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, "shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string short copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String("shortstr"));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String short copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, std::string(longLen, 'x'));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string long copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String(std::string(longLen, 'x').c_str()));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String long copy");

    // --- Append Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += "abc";
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string append short");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += "abc";
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String append short");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        std::string longstr(longLen, 'y');
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += longstr;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string append long");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        String longstr(std::string(longLen, 'y').c_str());
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += longstr;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String append long");

    // --- Random String Construction ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec;
        ctx.start();
        fill_random_strings(vec, N, shortLen);
        ctx.doNotOptimize(vec);
        ctx.stop();
    },
                        "std::string random short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec;
        ctx.start();
        fill_random_strings(vec, N, shortLen);
        ctx.doNotOptimize(vec);
        ctx.stop();
    },
                        "NGIN::String random short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec;
        ctx.start();
        ctx.doNotOptimize(vec);
        fill_random_strings(vec, N, longLen);
        ctx.clobberMemory();
        ctx.stop();
    },
                        "std::string random long construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec;
        ctx.start();
        ctx.doNotOptimize(vec);
        fill_random_strings(vec, N, longLen);
        ctx.clobberMemory();
        ctx.stop();
    },
                        "NGIN::String random long construction");

    // --- CStr Access ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec(N, "shortstr");
        size_t                   sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += vec[i].c_str()[0];
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string c_str() access");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec(N, String("shortstr"));
        size_t              sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += vec[i].CStr()[0];
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String CStr() access");

    // --- Reserve/Capacity ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.reserve(i % 256);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string reserve");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.Reserve(i % 256);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String Reserve");

    // --- Assignment ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        std::string t("shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = t;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string assignment");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        String t("shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = t;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String assignment");

    // --- Move Assignment ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, "shortstr");
        std::string              s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = std::move(src[i]);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string move assignment");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String("shortstr"));
        String              s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = std::move(src[i]);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String move assignment");

    // --- End: Run all and print ---
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
