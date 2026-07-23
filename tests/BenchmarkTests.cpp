#include <NGIN/Benchmark.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("benchmark registry preserves per-benchmark configuration", "[benchmark]")
{
    using namespace NGIN;

    Int32                 invocations = 0;
    const BenchmarkConfig config {
            .iterations       = 3,
            .warmupIterations = 2,
    };
    Benchmark::Register(
            config,
            [&](BenchmarkContext& context) {
                ++invocations;
                context.doNotOptimize(invocations);
            },
            "custom configuration regression");

    const auto results = Benchmark::RunAll<Units::Nanoseconds>();
    const auto result  = std::find_if(results.begin(), results.end(), [](const auto& candidate) {
        return candidate.name == "custom configuration regression";
    });
    REQUIRE(result != results.end());
    CHECK(result->numIterations == 3);
    CHECK(invocations == 5);
}
