#include <NGIN/Benchmark.hpp>
#include <NGIN/SIMD.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace NGIN;
using namespace NGIN::SIMD;

namespace
{
    constexpr BenchmarkConfig kMathBenchConfig = [] {
        BenchmarkConfig cfg;
        cfg.iterations       = 20;
        cfg.warmupIterations = 2;
        return cfg;
    }();

    template<class Backend, class Policy, class Op>
    void RegisterUnaryMathBenchmark(std::string_view backendLabel,
                                    std::string_view policyLabel,
                                    std::string_view opLabel,
                                    Op&& op)
    {
        using VecF = Vec<float, Backend>;

        const std::string name = std::string("SIMD/") + std::string(backendLabel) + "/" + std::string(policyLabel) + " " + std::string(opLabel);

        Benchmark::Register(
                kMathBenchConfig,
                [op = std::forward<Op>(op)](BenchmarkContext& ctx) {
                    using LocalVec = Vec<float, Backend>;

                    const std::size_t laneCount   = static_cast<std::size_t>(LocalVec::lanes);
                    const std::size_t totalValues = laneCount * 1024;// multiple of lanes

                    std::vector<float> inputs(totalValues);
                    for (std::size_t i = 0; i < inputs.size(); ++i)
                    {
                        const float angle = static_cast<float>(i) * 0.001F;
                        inputs[i]         = 1.0F + 0.25F * std::sin(angle);// strictly positive for log/sqrt
                    }

                    LocalVec accum {0.0F};
                    constexpr int repeats = 4;

                    ctx.start();
                    for (int repeat = 0; repeat < repeats; ++repeat)
                    {
                        for (std::size_t offset = 0; offset < inputs.size(); offset += laneCount)
                        {
                            const auto value    = LocalVec::Load(inputs.data() + offset);
                            const auto computed = op(value);
                            accum               = accum + computed;
                        }
                    }
                    ctx.doNotOptimize(ReduceAdd(accum));
                    ctx.stop();
                },
                name);
    }

    template<class Backend, class Policy>
    void RegisterPolicySet(std::string_view backendLabel, std::string_view policyLabel)
    {
        RegisterUnaryMathBenchmark<Backend, Policy>(backendLabel, policyLabel, "Exp", [](const auto& v) {
            return Exp<Policy>(v);
        });

        RegisterUnaryMathBenchmark<Backend, Policy>(backendLabel, policyLabel, "Log", [](const auto& v) {
            return Log<Policy>(v);
        });

        RegisterUnaryMathBenchmark<Backend, Policy>(backendLabel, policyLabel, "Sin", [](const auto& v) {
            return Sin<Policy>(v);
        });

        RegisterUnaryMathBenchmark<Backend, Policy>(backendLabel, policyLabel, "Cos", [](const auto& v) {
            return Cos<Policy>(v);
        });

        RegisterUnaryMathBenchmark<Backend, Policy>(backendLabel, policyLabel, "Sqrt", [](const auto& v) {
            return Sqrt<Policy>(v);
        });
    }

    template<class Backend>
    void RegisterStrict(std::string_view label)
    {
        RegisterPolicySet<Backend, StrictMathPolicy>(label, "Strict");
    }

    template<class Backend>
    void RegisterStrictAndFast(std::string_view label)
    {
        RegisterPolicySet<Backend, StrictMathPolicy>(label, "Strict");
        RegisterPolicySet<Backend, FastMathPolicy>(label, "Fast");
    }

}// namespace

int main()
{
    RegisterStrict<ScalarTag>("Scalar");

#if defined(__SSE2__)
    RegisterStrictAndFast<SSE2Tag>("SSE2");
#endif

#if defined(__AVX2__)
    RegisterStrictAndFast<AVX2Tag>("AVX2");
#endif

    const auto results = Benchmark::RunAll<Units::Microseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
