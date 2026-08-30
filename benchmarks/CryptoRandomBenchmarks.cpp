#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Tokens/TokenGenerator.hpp>

#include <array>
#include <iostream>

int main()
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::Units::Milliseconds;

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    if (!context.has_value())
    {
        std::cerr << "CreateBestAvailableContext failed: " << context.error().Message() << '\n';
        return 1;
    }

    std::array<NGIN::Byte, 32>  random32 {};
    std::array<NGIN::Byte, 256> random256 {};

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = context.value().FillRandom(random32);
        ctx.doNotOptimize(result.has_value());
        ctx.stop();
    },
                        "Crypto FillRandom 32 bytes");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = context.value().FillRandom(random256);
        ctx.doNotOptimize(result.has_value());
        ctx.stop();
    },
                        "Crypto FillRandom 256 bytes");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto token = NGIN::Crypto::Tokens::GenerateBase64Url(context.value(), {
                                                                                      .byteLength          = 32,
                                                                                      .minimumEntropyBytes = 16,
                                                                                      .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Base64Url,
                                                                              });
        ctx.doNotOptimize(token.has_value());
        ctx.stop();
    },
                        "Crypto Base64Url token 32 bytes");

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
