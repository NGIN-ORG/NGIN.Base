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
    constexpr std::size_t kMathValueCount = 8192;
    constexpr int         kMathRepeats    = 4;

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
                                    Op&&             op)
    {
        const std::string name = std::string("SIMD/") + std::string(backendLabel) + "/" + std::string(policyLabel) + " " + std::string(opLabel);

        Benchmark::Register(
                kMathBenchConfig,
                [op = std::forward<Op>(op)](BenchmarkContext& ctx) {
                    using LocalVec = Vec<float, Backend>;

                    constexpr std::size_t laneCount = static_cast<std::size_t>(LocalVec::lanes);
                    static_assert(kMathValueCount % laneCount == 0);

                    std::vector<float> inputs(kMathValueCount);
                    for (std::size_t i = 0; i < inputs.size(); ++i)
                    {
                        const float angle = static_cast<float>(i) * 0.001F;
                        inputs[i]         = 1.0F + 0.25F * std::sin(angle);// strictly positive for log/sqrt
                    }

                    LocalVec accum {0.0F};

                    ctx.start();
                    for (int repeat = 0; repeat < kMathRepeats; ++repeat)
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

    template<class Op>
    void RegisterPlainLoopBenchmark(std::string_view opLabel, Op&& op)
    {
        const std::string name = std::string("SIMD/PlainLoop/Strict ") + std::string(opLabel);

        Benchmark::Register(
                kMathBenchConfig,
                [op = std::forward<Op>(op)](BenchmarkContext& ctx) {
                    std::vector<float> inputs(kMathValueCount);
                    for (std::size_t i = 0; i < inputs.size(); ++i)
                    {
                        const float angle = static_cast<float>(i) * 0.001F;
                        inputs[i]         = 1.0F + 0.25F * std::sin(angle);
                    }

                    float accum = 0.0F;
                    ctx.start();
                    for (int repeat = 0; repeat < kMathRepeats; ++repeat)
                    {
                        for (const float value: inputs)
                        {
                            accum += op(value);
                        }
                    }
                    ctx.doNotOptimize(accum);
                    ctx.stop();
                },
                name);
    }

    void RegisterPlainLoopSet()
    {
        RegisterPlainLoopBenchmark("Exp", [](float value) { return std::exp(value); });
        RegisterPlainLoopBenchmark("Log", [](float value) { return std::log(value); });
        RegisterPlainLoopBenchmark("Sin", [](float value) { return std::sin(value); });
        RegisterPlainLoopBenchmark("Cos", [](float value) { return std::cos(value); });
        RegisterPlainLoopBenchmark("Sqrt", [](float value) { return std::sqrt(value); });
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
    RegisterPlainLoopSet();
    RegisterStrict<ScalarTag>("Scalar");

#if NGIN_SIMD_HAS_SSE2
    RegisterStrictAndFast<SSE2Tag>("SSE2");
#endif

#if NGIN_SIMD_HAS_AVX2
    RegisterStrictAndFast<AVX2Tag>("AVX2");
#endif

#if NGIN_SIMD_HAS_AVX512
    RegisterStrictAndFast<AVX512Tag>("AVX-512");
#endif

#if NGIN_SIMD_HAS_NEON
    RegisterStrict<NeonTag>("NEON");
#endif

    const auto results = Benchmark::RunAll<Units::Microseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
