#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <iostream>

namespace
{
    [[nodiscard]] NGIN::Crypto::ByteBuffer RepeatedByte(NGIN::UInt8 value, NGIN::UIntSize count)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(count);
        for (NGIN::UIntSize i = 0; i < count; ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(value);
        }
        return buffer;
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan View(const NGIN::Crypto::ByteBuffer& bytes) noexcept
    {
        return NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()};
    }
}// namespace

int main()
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::Crypto::Keys::KeyAlgorithm;
    using NGIN::Units::Milliseconds;

    const auto ed25519PublicKey = RepeatedByte(0x11, 32);
    const auto x25519PrivateKey = RepeatedByte(0x22, 32);

    auto ed25519Spki = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(KeyAlgorithm::Ed25519, View(ed25519PublicKey));
    if (!ed25519Spki.HasValue())
    {
        std::cerr << "Could not prepare Ed25519 SPKI benchmark fixture: " << ed25519Spki.Error().Message() << '\n';
        return 1;
    }

    auto x25519Pkcs8 = NGIN::Crypto::Keys::WritePrivateKeyInfo(KeyAlgorithm::X25519, View(x25519PrivateKey));
    if (!x25519Pkcs8.HasValue())
    {
        std::cerr << "Could not prepare X25519 PKCS#8 benchmark fixture: " << x25519Pkcs8.Error().Message() << '\n';
        return 1;
    }

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto parsed = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(View(ed25519Spki.Value()));
        if (parsed.HasValue())
        {
            auto imported = NGIN::Crypto::Keys::ImportEd25519PublicKey(parsed.Value());
            if (imported.HasValue())
            {
                auto exported = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(imported.Value());
                auto der      = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
                        exported.algorithm.algorithm,
                        NGIN::Crypto::ConstByteSpan {exported.publicKey.data(), exported.publicKey.Size()});
                ctx.doNotOptimize(der.HasValue());
            }
            ctx.doNotOptimize(imported.HasValue());
        }
        ctx.doNotOptimize(parsed.HasValue());
        ctx.stop();
    },
                        "Crypto Ed25519 SPKI parse/import/export/write");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(View(x25519Pkcs8.Value()));
        if (parsed.HasValue())
        {
            auto imported = NGIN::Crypto::Keys::ImportX25519PrivateKey(parsed.Value());
            if (imported.HasValue())
            {
                auto exported = NGIN::Crypto::Keys::ExportPrivateKeyInfo(imported.Value());
                auto der      = NGIN::Crypto::Keys::WritePrivateKeyInfo(
                        exported.algorithm.algorithm,
                        NGIN::Crypto::ConstByteSpan {exported.privateKey.data(), exported.privateKey.Size()});
                ctx.doNotOptimize(der.HasValue());
            }
            ctx.doNotOptimize(imported.HasValue());
        }
        ctx.doNotOptimize(parsed.HasValue());
        ctx.stop();
    },
                        "Crypto X25519 PKCS#8 parse/import/export/write");

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
