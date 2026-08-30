#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>

#include <iostream>

int main()
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::Crypto::AeadAlgorithm;
    using NGIN::Crypto::HashAlgorithm;
    using NGIN::Crypto::KdfAlgorithm;
    using NGIN::Crypto::MacAlgorithm;
    using NGIN::Crypto::Backend::CreateBestAvailableContext;
    using NGIN::Crypto::Backend::CreatePlatformContext;
    using NGIN::Units::Milliseconds;

    auto bestAvailable = CreateBestAvailableContext();
    auto platform      = CreatePlatformContext();

    if (!bestAvailable.has_value())
    {
        std::cerr << "CreateBestAvailableContext failed: " << bestAvailable.error().Message() << '\n';
        return 1;
    }

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto context = CreateBestAvailableContext();
        ctx.doNotOptimize(context.has_value());
        ctx.stop();
    },
                        "Crypto create best available context");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto context = CreatePlatformContext();
        ctx.doNotOptimize(context.has_value());
        ctx.stop();
    },
                        "Crypto create platform context");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        const auto& context = bestAvailable.value();
        ctx.start();
        auto hashSupport = context.DescribeSupport(HashAlgorithm::Sha256);
        auto macSupport  = context.DescribeSupport(MacAlgorithm::HmacSha256);
        auto kdfSupport  = context.DescribeSupport(KdfAlgorithm::Pbkdf2Sha256);
        auto aeadSupport = context.DescribeSupport(AeadAlgorithm::Aes256Gcm);
        ctx.doNotOptimize(hashSupport.supported);
        ctx.doNotOptimize(macSupport.supported);
        ctx.doNotOptimize(kdfSupport.supported);
        ctx.doNotOptimize(aeadSupport.supported);
        ctx.stop();
    },
                        "Crypto best available capability inspection");

    if (platform.has_value())
    {
        Benchmark::Register([&](BenchmarkContext& ctx) {
            const auto& context = platform.value();
            ctx.start();
            auto randomSupport = context.DescribeRandomSupport();
            auto hashSupport   = context.DescribeSupport(HashAlgorithm::Sha256);
            ctx.doNotOptimize(randomSupport.supported);
            ctx.doNotOptimize(hashSupport.supported);
            ctx.stop();
        },
                            "Crypto platform capability inspection");
    }

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
