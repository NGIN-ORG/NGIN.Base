#include <NGIN/Benchmark.hpp>
#include <NGIN/Math/BigInt.hpp>
#include <iostream>
#include <string>

using NGIN::Benchmark;
using NGIN::BenchmarkContext;
using NGIN::Math::BigInt;
using NGIN::Units::Microseconds;

int main()
{
    Benchmark::defaultConfig.iterations       = 200;
    Benchmark::defaultConfig.warmupIterations = 10;

    const BigInt singleLimbDividend(std::string(10000, '9'));
    const BigInt singleLimbDivisor("999999937");
    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        BigInt quotient = singleLimbDividend / singleLimbDivisor;
        context.doNotOptimize(quotient);
        context.stop();
    },
                        "BigInt single-limb division, 10000 digits");

    const BigInt multiLimbDividend(std::string(1800, '9'));
    const BigInt multiLimbDivisor(std::string(900, '7'));
    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        BigInt quotient = multiLimbDividend / multiLimbDivisor;
        context.doNotOptimize(quotient);
        context.stop();
    },
                        "BigInt multi-limb division, 1800/900 digits");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        std::pair<BigInt, BigInt> quotientAndRemainder {
                multiLimbDividend / multiLimbDivisor,
                multiLimbDividend % multiLimbDivisor,
        };
        context.doNotOptimize(quotientAndRemainder);
        context.stop();
    },
                        "BigInt separate division and modulo, 1800/900 digits");

    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        auto quotientAndRemainder = multiLimbDividend.DivRem(multiLimbDivisor);
        context.doNotOptimize(quotientAndRemainder);
        context.stop();
    },
                        "BigInt fused DivRem, 1800/900 digits");

    const BigInt adverseDividend("1000000000000000");
    const BigInt adverseDivisor("1000000000");
    Benchmark::Register([&](BenchmarkContext& context) {
        context.start();
        BigInt quotient = adverseDividend / adverseDivisor;
        context.doNotOptimize(quotient);
        context.stop();
    },
                        "BigInt formerly repeated-subtraction division");

    const std::vector<NGIN::BenchmarkResult<Microseconds>> results = Benchmark::RunAll<Microseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
