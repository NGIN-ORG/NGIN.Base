// SPDX-License-Identifier: Apache-2.0

#include "NGIN/Benchmark.hpp"
#include "NGIN/SIMD.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

using namespace NGIN;
using namespace NGIN::SIMD;

namespace
{
    constexpr std::size_t kScanBytes = 1024 * 1024;

    constexpr BenchmarkConfig kScanConfig = [] {
        BenchmarkConfig config;
        config.iterations       = 100;
        config.warmupIterations = 10;
        return config;
    }();

    template<class Search>
    void RegisterScanBenchmark(std::string_view name, Search&& search)
    {
        Benchmark::Register(
                kScanConfig,
                [search = std::forward<Search>(search)](BenchmarkContext& context) {
                    std::vector<std::uint8_t> bytes(kScanBytes, static_cast<std::uint8_t>('a'));
                    bytes.back() = static_cast<std::uint8_t>('z');

                    context.start();
                    const std::size_t found = search(bytes.data(), bytes.size());
                    context.doNotOptimize(found);
                    context.stop();
                },
                name);
    }
}// namespace

int main()
{
    std::cout << "Selected runtime SIMD backend: " << RuntimeBackendName(GetRuntimeBackend()) << '\n';

    RegisterScanBenchmark("SIMD/Scan/Scalar", [](const std::uint8_t* data, std::size_t size) {
        return FindEqByte<ScalarTag>(data, size, static_cast<std::uint8_t>('z'));
    });
    RegisterScanBenchmark("SIMD/Scan/RuntimeDispatch", [](const std::uint8_t* data, std::size_t size) {
        return FindEqByteRuntime(data, size, static_cast<std::uint8_t>('z'));
    });

    const auto results = Benchmark::RunAll<Units::Microseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
