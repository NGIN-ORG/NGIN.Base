#include <NGIN/Crypto/Hashing/Hash.hpp>

#include <NGIN/Crypto/Encoding/Hex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string_view>

namespace
{
    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        for (NGIN::UIntSize i = 0; i < actual.size(); ++i)
        {
            REQUIRE(actual[i] == expected[i]);
        }
    }
}// namespace

TEST_CASE("DigestSize reports fixed digest sizes", "[Crypto][Hash]")
{
    REQUIRE(NGIN::Crypto::Hashing::DigestSize(NGIN::Crypto::HashAlgorithm::Sha256) == 32);
    REQUIRE(NGIN::Crypto::Hashing::DigestSize(NGIN::Crypto::HashAlgorithm::Sha512) == 64);
    REQUIRE(NGIN::Crypto::Hashing::DigestSize(NGIN::Crypto::HashAlgorithm::Sha3_256) == 32);
    REQUIRE(NGIN::Crypto::Hashing::DigestSize(NGIN::Crypto::HashAlgorithm::Sha3_512) == 64);
    REQUIRE(NGIN::Crypto::Hashing::DigestSize(NGIN::Crypto::HashAlgorithm::Blake3) == 32);
}

TEST_CASE("Digest aliases expose fixed-size storage", "[Crypto][Hash]")
{
    NGIN::Crypto::Hashing::Sha256Digest sha256 {};
    NGIN::Crypto::Hashing::Sha512Digest sha512 {};

    REQUIRE(sha256.size() == 32);
    REQUIRE(sha512.size() == 64);
}

TEST_CASE("HashInto checks output size before backend support", "[Crypto][Hash]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };
    std::array<NGIN::Byte, 31> output {};

    auto result = NGIN::Crypto::Hashing::HashInto(
            context,
            NGIN::Crypto::HashAlgorithm::Sha256,
            NGIN::Crypto::ConstByteSpan {},
            output);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("Hash returns unsupported algorithm when context lacks capability", "[Crypto][Hash]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Hashing::Hash(context, NGIN::Crypto::HashAlgorithm::Sha256, NGIN::Crypto::ConstByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("Hash contract does not fake implementation even if capability is manually enabled", "[Crypto][Hash]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::HashAlgorithm::Sha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "hash-capable-test"},
            capabilities,
    };

    auto result = NGIN::Crypto::Hashing::Sha256(context, NGIN::Crypto::ConstByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("Hash computes SHA-256 and SHA-512 vectors when backend supports them", "[Crypto][Hash]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    if (!context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256) || !context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha512))
    {
        SUCCEED("SHA vector test requires a hash-capable backend such as optional OpenSSL.");
        return;
    }

    constexpr std::string_view MESSAGE {"abc"};
    auto                       input = std::as_bytes(std::span {MESSAGE.data(), MESSAGE.size()});

    auto sha256 = NGIN::Crypto::Hashing::Sha256(context.Value(), input);
    auto sha512 = NGIN::Crypto::Hashing::Sha512(context.Value(), input);

    auto expectedSha256 = NGIN::Crypto::Encoding::DecodeHex(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto expectedSha512 = NGIN::Crypto::Encoding::DecodeHex(
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    REQUIRE(sha256.HasValue());
    REQUIRE(sha512.HasValue());
    REQUIRE(expectedSha256.HasValue());
    REQUIRE(expectedSha512.HasValue());

    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {sha256.Value().data(), sha256.Value().size()},
            NGIN::Crypto::ConstByteSpan {expectedSha256.Value().data(), expectedSha256.Value().Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {sha512.Value().data(), sha512.Value().size()},
            NGIN::Crypto::ConstByteSpan {expectedSha512.Value().data(), expectedSha512.Value().Size()});
}
