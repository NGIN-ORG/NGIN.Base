#include <NGIN/Crypto/Hashing/Hash.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

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
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Hashing::Hash(context.Value(), NGIN::Crypto::HashAlgorithm::Sha256, NGIN::Crypto::ConstByteSpan {});

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
