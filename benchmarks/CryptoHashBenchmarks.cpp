#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Hashing/Hash.hpp>

#include <array>
#include <iostream>

namespace
{
    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(const std::array<NGIN::Byte, 4096>& bytes) noexcept
    {
        return {bytes.data(), bytes.size()};
    }
}// namespace

int main()
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::Units::Milliseconds;

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    if (!context.HasValue())
    {
        std::cerr << "CreateBestAvailableContext failed: " << context.Error().Message() << '\n';
        return 1;
    }

    std::array<NGIN::Byte, 4096> input {};
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] = static_cast<NGIN::Byte>(i & 0xffU);
    }

    if (context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256))
    {
        NGIN::Crypto::Hashing::Sha256Digest digest {};

        Benchmark::Register([&](BenchmarkContext& ctx) {
            ctx.start();
            auto result = NGIN::Crypto::Hashing::Sha256Into(context.Value(), Bytes(input), digest);
            ctx.doNotOptimize(result.HasValue());
            ctx.stop();
        },
                            "Crypto SHA-256 into 4 KiB");

        Benchmark::Register([&](BenchmarkContext& ctx) {
            ctx.start();
            auto result = NGIN::Crypto::Hashing::Sha256(context.Value(), Bytes(input));
            ctx.doNotOptimize(result.HasValue());
            ctx.stop();
        },
                            "Crypto SHA-256 owned 4 KiB");
    }

    if (context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha512))
    {
        NGIN::Crypto::Hashing::Sha512Digest digest {};

        Benchmark::Register([&](BenchmarkContext& ctx) {
            ctx.start();
            auto result = NGIN::Crypto::Hashing::Sha512Into(context.Value(), Bytes(input), digest);
            ctx.doNotOptimize(result.HasValue());
            ctx.stop();
        },
                            "Crypto SHA-512 into 4 KiB");
    }

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
