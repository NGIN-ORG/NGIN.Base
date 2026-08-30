#include <NGIN/Benchmark.hpp>
#include <NGIN/Math/AffineMatrix.hpp>
#include <NGIN/Math/Matrix.hpp>

#ifndef NGIN_BENCH_HAS_GLM
#define NGIN_BENCH_HAS_GLM 0
#endif

#if NGIN_BENCH_HAS_GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#endif

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

using NGIN::Benchmark;
using NGIN::BenchmarkConfig;
using NGIN::BenchmarkContext;
using NGIN::BenchmarkResult;
using NGIN::Math::AffineMatrix3F;
using NGIN::Math::Cross;
using NGIN::Math::Dot;
using NGIN::Math::Inverse;
using NGIN::Math::Matrix4F;
using NGIN::Math::Normalize;
using NGIN::Math::TransformPoint;
using NGIN::Math::TryInverse;
using NGIN::Math::TryNormalize;
using NGIN::Math::Vector3F;
using NGIN::Math::Vector4F;
using NGIN::Units::Nanoseconds;

namespace
{
    constexpr std::size_t VECTOR_COUNT  = 262'144;
    constexpr std::size_t MATRIX_COUNT  = 65'536;
    constexpr std::size_t INVERSE_COUNT = 16'384;

    constexpr BenchmarkConfig BENCHMARK_CONFIG = [] {
        BenchmarkConfig config;
        config.iterations       = 100;
        config.warmupIterations = 50;
        config.keepRawTimings   = true;
        return config;
    }();

    struct NginData
    {
        std::vector<Vector3F> vector3Left   = std::vector<Vector3F>(VECTOR_COUNT);
        std::vector<Vector3F> vector3Right  = std::vector<Vector3F>(VECTOR_COUNT);
        std::vector<Vector3F> vector3Output = std::vector<Vector3F>(VECTOR_COUNT);
        std::vector<Vector4F> vector4Left   = std::vector<Vector4F>(VECTOR_COUNT);
        std::vector<Vector4F> vector4Right  = std::vector<Vector4F>(VECTOR_COUNT);
        std::vector<Vector4F> vector4Output = std::vector<Vector4F>(VECTOR_COUNT);
        std::vector<float>    scalarOutput  = std::vector<float>(VECTOR_COUNT);
        std::vector<Matrix4F> matrixLeft    = std::vector<Matrix4F>(MATRIX_COUNT);
        std::vector<Matrix4F> matrixRight   = std::vector<Matrix4F>(MATRIX_COUNT);
        std::vector<Matrix4F> matrixOutput  = std::vector<Matrix4F>(MATRIX_COUNT);
        AffineMatrix3F        sharedAffine;
    };

#if NGIN_BENCH_HAS_GLM
    struct GlmData
    {
        std::vector<glm::vec3> vector3Left   = std::vector<glm::vec3>(VECTOR_COUNT);
        std::vector<glm::vec3> vector3Right  = std::vector<glm::vec3>(VECTOR_COUNT);
        std::vector<glm::vec3> vector3Output = std::vector<glm::vec3>(VECTOR_COUNT);
        std::vector<glm::vec4> vector4Left   = std::vector<glm::vec4>(VECTOR_COUNT);
        std::vector<glm::vec4> vector4Right  = std::vector<glm::vec4>(VECTOR_COUNT);
        std::vector<glm::vec4> vector4Output = std::vector<glm::vec4>(VECTOR_COUNT);
        std::vector<float>     scalarOutput  = std::vector<float>(VECTOR_COUNT);
        std::vector<glm::mat4> matrixLeft    = std::vector<glm::mat4>(MATRIX_COUNT);
        std::vector<glm::mat4> matrixRight   = std::vector<glm::mat4>(MATRIX_COUNT);
        std::vector<glm::mat4> matrixOutput  = std::vector<glm::mat4>(MATRIX_COUNT);
        glm::mat4              sharedAffine {1.0F};
    };

    [[nodiscard]] glm::mat4 ToGlm(const Matrix4F& source)
    {
        glm::mat4 result {0.0F};
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                result[column][row] = source(row, column);
        return result;
    }
#endif

    void Initialize(NginData& nginData
#if NGIN_BENCH_HAS_GLM
                    ,
                    GlmData& glmData
#endif
    )
    {
        for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
        {
            const float phase = static_cast<float>(index) * 0.001F;
            const float x     = 1.0F + std::sin(phase);
            const float y     = 2.0F + std::cos(phase * 1.7F);
            const float z     = 3.0F + std::sin(phase * 0.7F);
            const float w     = 1.0F + std::cos(phase * 0.3F);

            nginData.vector3Left[index]  = Vector3F {x, y, z};
            nginData.vector3Right[index] = Vector3F {z * 0.7F, x * 1.1F, y * 0.9F};
            nginData.vector4Left[index]  = Vector4F {x, y, z, w};
            nginData.vector4Right[index] = Vector4F {w * 0.8F, z * 0.7F, x * 1.1F, y * 0.9F};

#if NGIN_BENCH_HAS_GLM
            glmData.vector3Left[index]  = glm::vec3 {x, y, z};
            glmData.vector3Right[index] = glm::vec3 {z * 0.7F, x * 1.1F, y * 0.9F};
            glmData.vector4Left[index]  = glm::vec4 {x, y, z, w};
            glmData.vector4Right[index] = glm::vec4 {w * 0.8F, z * 0.7F, x * 1.1F, y * 0.9F};
#endif
        }

        for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
        {
            const float phase = static_cast<float>(index) * 0.002F;
            const float a     = 0.01F * std::sin(phase);
            const float b     = 0.01F * std::cos(phase * 0.7F);

            nginData.matrixLeft[index] = Matrix4F {
                    1.0F + a,
                    0.02F,
                    b,
                    phase,
                    -0.01F,
                    1.0F - b,
                    0.03F,
                    phase * 0.5F,
                    0.04F,
                    -0.02F,
                    1.0F + a,
                    phase * 0.25F,
                    0.0F,
                    0.0F,
                    0.0F,
                    1.0F,
            };
            nginData.matrixRight[index] = Matrix4F {
                    0.9F - b,
                    -0.03F,
                    0.01F,
                    -phase * 0.3F,
                    0.02F,
                    1.1F + a,
                    -0.04F,
                    phase * 0.2F,
                    -0.01F,
                    0.03F,
                    1.0F - a,
                    phase * 0.1F,
                    0.0F,
                    0.0F,
                    0.0F,
                    1.0F,
            };

#if NGIN_BENCH_HAS_GLM
            glmData.matrixLeft[index]  = ToGlm(nginData.matrixLeft[index]);
            glmData.matrixRight[index] = ToGlm(nginData.matrixRight[index]);
#endif
        }

        nginData.sharedAffine = AffineMatrix3F {
                nginData.matrixLeft[0](0, 0),
                nginData.matrixLeft[0](0, 1),
                nginData.matrixLeft[0](0, 2),
                nginData.matrixLeft[0](0, 3),
                nginData.matrixLeft[0](1, 0),
                nginData.matrixLeft[0](1, 1),
                nginData.matrixLeft[0](1, 2),
                nginData.matrixLeft[0](1, 3),
                nginData.matrixLeft[0](2, 0),
                nginData.matrixLeft[0](2, 1),
                nginData.matrixLeft[0](2, 2),
                nginData.matrixLeft[0](2, 3),
        };
#if NGIN_BENCH_HAS_GLM
        glmData.sharedAffine = glmData.matrixLeft[0];
#endif
    }

    template<class T>
    void ObserveOutput(BenchmarkContext& context, const std::vector<T>& output)
    {
        context.doNotOptimize(output.front());
        context.doNotOptimize(output.back());
        context.clobberMemory();
    }

    void RegisterNginBenchmarks(NginData& data)
    {
        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = data.vector3Left[index] + data.vector3Right[index];
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "NGIN/Vector3/Add");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.scalarOutput[index] = Dot(data.vector3Left[index], data.vector3Right[index]);
                    ObserveOutput(context, data.scalarOutput);
                    context.stop();
                },
                "NGIN/Vector3/Dot");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = Cross(data.vector3Left[index], data.vector3Right[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "NGIN/Vector3/Cross");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = Normalize(data.vector3Left[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "NGIN/Vector3/Normalize");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = *TryNormalize(data.vector3Left[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "NGIN/Vector3/NormalizeChecked");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector4Output[index] = data.vector4Left[index] + data.vector4Right[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "NGIN/Vector4/Add");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.scalarOutput[index] = Dot(data.vector4Left[index], data.vector4Right[index]);
                    ObserveOutput(context, data.scalarOutput);
                    context.stop();
                },
                "NGIN/Vector4/Dot");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.vector4Output[index] = data.matrixLeft[index] * data.vector4Left[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "NGIN/Matrix4/MultiplyVector4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    const Matrix4F& matrix = data.matrixLeft.front();
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector4Output[index] = matrix * data.vector4Left[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "NGIN/Matrix4/TransformVectorBatch");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = TransformPoint(data.sharedAffine, data.vector3Left[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "NGIN/Affine3x4/TransformPointBatch");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.vector4Output[index] = data.vector4Left[index] * data.matrixLeft[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "NGIN/Vector4/MultiplyMatrix4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.matrixOutput[index] = data.matrixLeft[index] * data.matrixRight[index];
                    ObserveOutput(context, data.matrixOutput);
                    context.stop();
                },
                "NGIN/Matrix4/MultiplyMatrix4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < INVERSE_COUNT; ++index)
                        data.matrixOutput[index] = Inverse(data.matrixLeft[index]);
                    ObserveOutput(context, data.matrixOutput);
                    context.stop();
                },
                "NGIN/Matrix4/Inverse");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < INVERSE_COUNT; ++index)
                        data.matrixOutput[index] = *TryInverse(data.matrixLeft[index]);
                    ObserveOutput(context, data.matrixOutput);
                    context.stop();
                },
                "NGIN/Matrix4/InverseChecked");
    }

#if NGIN_BENCH_HAS_GLM
    void RegisterGlmBenchmarks(GlmData& data)
    {
        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = data.vector3Left[index] + data.vector3Right[index];
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "GLM/Vector3/Add");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.scalarOutput[index] = glm::dot(data.vector3Left[index], data.vector3Right[index]);
                    ObserveOutput(context, data.scalarOutput);
                    context.stop();
                },
                "GLM/Vector3/Dot");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = glm::cross(data.vector3Left[index], data.vector3Right[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "GLM/Vector3/Cross");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = glm::normalize(data.vector3Left[index]);
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "GLM/Vector3/Normalize");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector4Output[index] = data.vector4Left[index] + data.vector4Right[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "GLM/Vector4/Add");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.scalarOutput[index] = glm::dot(data.vector4Left[index], data.vector4Right[index]);
                    ObserveOutput(context, data.scalarOutput);
                    context.stop();
                },
                "GLM/Vector4/Dot");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.vector4Output[index] = data.matrixLeft[index] * data.vector4Left[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "GLM/Matrix4/MultiplyVector4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    const glm::mat4& matrix = data.matrixLeft.front();
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector4Output[index] = matrix * data.vector4Left[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "GLM/Matrix4/TransformVectorBatch");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < VECTOR_COUNT; ++index)
                        data.vector3Output[index] = glm::vec3(data.sharedAffine * glm::vec4(data.vector3Left[index], 1.0F));
                    ObserveOutput(context, data.vector3Output);
                    context.stop();
                },
                "GLM/Matrix4/TransformPointBatch");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.vector4Output[index] = data.vector4Left[index] * data.matrixLeft[index];
                    ObserveOutput(context, data.vector4Output);
                    context.stop();
                },
                "GLM/Vector4/MultiplyMatrix4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < MATRIX_COUNT; ++index)
                        data.matrixOutput[index] = data.matrixLeft[index] * data.matrixRight[index];
                    ObserveOutput(context, data.matrixOutput);
                    context.stop();
                },
                "GLM/Matrix4/MultiplyMatrix4");

        Benchmark::Register(
                BENCHMARK_CONFIG,
                [&data](BenchmarkContext& context) {
                    context.start();
                    for (std::size_t index = 0; index < INVERSE_COUNT; ++index)
                        data.matrixOutput[index] = glm::inverse(data.matrixLeft[index]);
                    ObserveOutput(context, data.matrixOutput);
                    context.stop();
                },
                "GLM/Matrix4/Inverse");
    }
#endif

#if NGIN_BENCH_HAS_GLM
    [[nodiscard]] const BenchmarkResult<Nanoseconds>* FindResult(
            const std::vector<BenchmarkResult<Nanoseconds>>& results,
            std::string_view                                 name)
    {
        for (const BenchmarkResult<Nanoseconds>& result: results)
            if (result.name == name)
                return &result;
        return nullptr;
    }

    [[nodiscard]] std::optional<double> PrintComparison(
            const std::vector<BenchmarkResult<Nanoseconds>>& results,
            std::string_view                                 operation,
            std::string_view                                 nginName,
            std::string_view                                 glmName,
            std::size_t                                      operationsPerIteration)
    {
        const BenchmarkResult<Nanoseconds>* ngin = FindResult(results, nginName);
        const BenchmarkResult<Nanoseconds>* glm  = FindResult(results, glmName);
        if (ngin == nullptr || glm == nullptr)
            return std::nullopt;

        const double nginNanoseconds = ngin->medianTime.GetValue() / static_cast<double>(operationsPerIteration);
        const double glmNanoseconds  = glm->medianTime.GetValue() / static_cast<double>(operationsPerIteration);
        const double ratio           = nginNanoseconds / glmNanoseconds;
        std::cout << std::left << std::setw(30) << operation
                  << std::right << std::setw(12) << nginNanoseconds
                  << std::setw(12) << glmNanoseconds
                  << std::setw(12) << ratio << '\n';
        return ratio;
    }
#endif
}// namespace

int main()
{
    NginData nginData;
#if NGIN_BENCH_HAS_GLM
    GlmData glmData;
    Initialize(nginData, glmData);
#else
    Initialize(nginData);
#endif

    RegisterNginBenchmarks(nginData);
#if NGIN_BENCH_HAS_GLM
    RegisterGlmBenchmarks(glmData);
#endif

    const std::vector<std::string_view> benchmarkOrder {
            "NGIN/Vector3/Add",
            "GLM/Vector3/Add",
            "NGIN/Vector3/Dot",
            "GLM/Vector3/Dot",
            "NGIN/Vector3/Cross",
            "GLM/Vector3/Cross",
            "NGIN/Vector3/Normalize",
            "GLM/Vector3/Normalize",
            "NGIN/Vector3/NormalizeChecked",
            "NGIN/Vector4/Add",
            "GLM/Vector4/Add",
            "NGIN/Vector4/Dot",
            "GLM/Vector4/Dot",
            "NGIN/Matrix4/MultiplyVector4",
            "GLM/Matrix4/MultiplyVector4",
            "NGIN/Matrix4/TransformVectorBatch",
            "GLM/Matrix4/TransformVectorBatch",
            "NGIN/Affine3x4/TransformPointBatch",
            "GLM/Matrix4/TransformPointBatch",
            "NGIN/Vector4/MultiplyMatrix4",
            "GLM/Vector4/MultiplyMatrix4",
            "NGIN/Matrix4/MultiplyMatrix4",
            "GLM/Matrix4/MultiplyMatrix4",
            "NGIN/Matrix4/Inverse",
            "GLM/Matrix4/Inverse",
            "NGIN/Matrix4/InverseChecked",
    };
    const std::vector<BenchmarkResult<Nanoseconds>> results =
            Benchmark::RunAllInOrder<Nanoseconds>(benchmarkOrder);
    Benchmark::PrintSummaryTable(std::cout, results);

#if NGIN_BENCH_HAS_GLM
    std::cout << "\nMedian nanoseconds per operation (ratio = NGIN / GLM; lower is better)\n";
    std::cout << std::left << std::setw(30) << "Operation"
              << std::right << std::setw(12) << "NGIN"
              << std::setw(12) << "GLM"
              << std::setw(12) << "Ratio" << '\n';
    double      apiRatioProduct         = 1.0;
    std::size_t apiComparisonCount      = 0;
    double      workloadRatioProduct    = 1.0;
    std::size_t workloadComparisonCount = 0;
    const auto  compare                 = [&](std::string_view operation,
                             std::string_view nginName,
                             std::string_view glmName,
                             std::size_t      operationsPerIteration) {
        return PrintComparison(results, operation, nginName, glmName, operationsPerIteration);
    };
    const auto addRatio = [](const std::optional<double>& ratio, double& product, std::size_t& count) {
        if (ratio)
        {
            product *= *ratio;
            ++count;
        }
    };

    addRatio(compare("Vector3 add", "NGIN/Vector3/Add", "GLM/Vector3/Add", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Vector3 dot", "NGIN/Vector3/Dot", "GLM/Vector3/Dot", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Vector3 cross", "NGIN/Vector3/Cross", "GLM/Vector3/Cross", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Vector3 normalize", "NGIN/Vector3/Normalize", "GLM/Vector3/Normalize", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Vector4 add", "NGIN/Vector4/Add", "GLM/Vector4/Add", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Vector4 dot", "NGIN/Vector4/Dot", "GLM/Vector4/Dot", VECTOR_COUNT), apiRatioProduct, apiComparisonCount);
    const std::optional<double> matrixVectorRatio =
            compare("Matrix4 * Vector4", "NGIN/Matrix4/MultiplyVector4", "GLM/Matrix4/MultiplyVector4", MATRIX_COUNT);
    addRatio(matrixVectorRatio, apiRatioProduct, apiComparisonCount);
    addRatio(matrixVectorRatio, workloadRatioProduct, workloadComparisonCount);
    addRatio(compare("Vector4 * Matrix4", "NGIN/Vector4/MultiplyMatrix4", "GLM/Vector4/MultiplyMatrix4", MATRIX_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Matrix4 * Matrix4", "NGIN/Matrix4/MultiplyMatrix4", "GLM/Matrix4/MultiplyMatrix4", MATRIX_COUNT), apiRatioProduct, apiComparisonCount);
    addRatio(compare("Matrix4 inverse", "NGIN/Matrix4/Inverse", "GLM/Matrix4/Inverse", INVERSE_COUNT), apiRatioProduct, apiComparisonCount);

    std::cout << "\nGame-engine transform throughput\n";
    addRatio(
            compare("Shared Matrix4 * vectors", "NGIN/Matrix4/TransformVectorBatch", "GLM/Matrix4/TransformVectorBatch", VECTOR_COUNT),
            workloadRatioProduct,
            workloadComparisonCount);
    addRatio(
            compare("Affine3x4 transform points", "NGIN/Affine3x4/TransformPointBatch", "GLM/Matrix4/TransformPointBatch", VECTOR_COUNT),
            workloadRatioProduct,
            workloadComparisonCount);

    if (apiComparisonCount > 0)
        std::cout << "Unweighted API geometric mean NGIN / GLM: "
                  << std::pow(apiRatioProduct, 1.0 / static_cast<double>(apiComparisonCount)) << '\n';
    if (workloadComparisonCount > 0)
        std::cout << "Transform-workload geometric mean NGIN / GLM: "
                  << std::pow(workloadRatioProduct, 1.0 / static_cast<double>(workloadComparisonCount)) << '\n';
#endif
    return 0;
}
