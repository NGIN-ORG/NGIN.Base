#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

#include <array>
#include <iostream>

namespace
{
    template<std::size_t Size>
    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(const std::array<NGIN::Byte, Size>& bytes) noexcept
    {
        return {bytes.data(), bytes.size()};
    }

    template<std::size_t Size>
    [[nodiscard]] NGIN::Crypto::ByteSpan MutableBytes(std::array<NGIN::Byte, Size>& bytes) noexcept
    {
        return {bytes.data(), bytes.size()};
    }

    template<std::size_t Size>
    void FillPattern(std::array<NGIN::Byte, Size>& bytes) noexcept
    {
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<NGIN::Byte>((i * 31U) & 0xffU);
        }
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

    std::array<NGIN::Byte, 32>   key {};
    std::array<NGIN::Byte, 24>   nonce {};
    std::array<NGIN::Byte, 1024> plaintext {};
    std::array<NGIN::Byte, 1024> ciphertext {};
    std::array<NGIN::Byte, 1024> opened {};
    std::array<NGIN::Byte, 16>   tag {};

    FillPattern(key);
    FillPattern(nonce);
    FillPattern(plaintext);

    auto registerAead = [&](NGIN::Crypto::AeadAlgorithm algorithm, std::string_view name) {
        if (!context.Value().Supports(algorithm))
        {
            return;
        }

        const auto                             sizes = NGIN::Crypto::Symmetric::GetAeadSizes(algorithm);
        NGIN::Crypto::Symmetric::AeadSealInput sealInput {
                .key            = NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), sizes.keySize}},
                .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), sizes.nonceSize},
                .plaintext      = Bytes(plaintext),
                .associatedData = {},
        };

        auto seal = NGIN::Crypto::Symmetric::SealInto(
                context.Value(),
                algorithm,
                sealInput,
                MutableBytes(ciphertext),
                MutableBytes(tag));
        if (!seal.HasValue())
        {
            return;
        }

        Benchmark::Register([&, algorithm, sealInput](BenchmarkContext& ctx) {
            ctx.start();
            auto result = NGIN::Crypto::Symmetric::SealInto(
                    context.Value(),
                    algorithm,
                    sealInput,
                    MutableBytes(ciphertext),
                    MutableBytes(tag));
            ctx.doNotOptimize(result.HasValue());
            ctx.stop();
        },
                            std::string {"Crypto "} + std::string {name} + " seal 1 KiB");

        NGIN::Crypto::Symmetric::AeadOpenInput openInput {
                .key            = sealInput.key,
                .nonce          = sealInput.nonce,
                .ciphertext     = Bytes(ciphertext),
                .associatedData = {},
                .tag            = Bytes(tag),
        };

        Benchmark::Register([&, algorithm, openInput](BenchmarkContext& ctx) {
            ctx.start();
            auto result = NGIN::Crypto::Symmetric::OpenInto(
                    context.Value(),
                    algorithm,
                    openInput,
                    MutableBytes(opened));
            ctx.doNotOptimize(result.HasValue());
            ctx.stop();
        },
                            std::string {"Crypto "} + std::string {name} + " open 1 KiB");
    };

    registerAead(NGIN::Crypto::AeadAlgorithm::Aes256Gcm, "AES-256-GCM");
    registerAead(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305, "ChaCha20-Poly1305");
    registerAead(NGIN::Crypto::AeadAlgorithm::XChaCha20Poly1305, "XChaCha20-Poly1305");

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
