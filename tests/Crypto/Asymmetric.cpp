#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>

namespace
{
    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr NGIN::Crypto::FixedBytes<Size> ZeroBytes() noexcept
    {
        return {};
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] NGIN::Crypto::FixedBytes<Size> DecodeFixedHex(std::string_view text)
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeHex(text);
        REQUIRE(decoded.HasValue());
        REQUIRE(decoded.Value().Size() == Size);

        NGIN::Crypto::FixedBytes<Size> output {};
        std::copy(decoded.Value().begin(), decoded.Value().end(), output.begin());
        return output;
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        REQUIRE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
    }
}// namespace

TEST_CASE("Asymmetric key wrappers expose algorithm-specific fixed sizes", "[Crypto][Asymmetric]")
{
    auto edPublic  = NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(ZeroBytes<32>());
    auto edPrivate = NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromBytes(ZeroBytes<32>());
    auto xPublic   = NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(ZeroBytes<32>());
    auto xPrivate  = NGIN::Crypto::Asymmetric::X25519PrivateKey::FromBytes(ZeroBytes<32>());

    REQUIRE(edPublic.Bytes().size() == 32);
    REQUIRE(edPrivate.Bytes().size() == 32);
    REQUIRE(xPublic.Bytes().size() == 32);
    REQUIRE(xPrivate.Bytes().size() == 32);

    REQUIRE(NGIN::Crypto::Asymmetric::GetSignatureKeySizes(NGIN::Crypto::SignatureAlgorithm::Ed25519).signatureSize == 64);
    REQUIRE(
            NGIN::Crypto::Asymmetric::GetKeyAgreementSizes(NGIN::Crypto::KeyAgreementAlgorithm::X25519)
                    .sharedSecretSize == 32);
}

TEST_CASE("Private keys and key pairs are move-only", "[Crypto][Asymmetric]")
{
    STATIC_REQUIRE(std::is_copy_constructible_v<NGIN::Crypto::Asymmetric::Ed25519PublicKey>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<NGIN::Crypto::Asymmetric::Ed25519PrivateKey>);
    STATIC_REQUIRE(std::is_move_constructible_v<NGIN::Crypto::Asymmetric::Ed25519PrivateKey>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<NGIN::Crypto::Asymmetric::Ed25519KeyPair>);
    STATIC_REQUIRE(std::is_move_constructible_v<NGIN::Crypto::Asymmetric::Ed25519KeyPair>);
}

TEST_CASE("Ed25519 key generation is backend-gated", "[Crypto][Asymmetric]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("X25519 shared secret derivation validates output size before backend support", "[Crypto][Asymmetric]")
{
    auto privateKey = NGIN::Crypto::Asymmetric::X25519PrivateKey::FromBytes(ZeroBytes<32>());
    auto publicKey  = NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(ZeroBytes<32>());
    auto tooSmall   = ZeroBytes<31>();

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecretInto(context, privateKey, publicKey, tooSmall);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("X25519 generated key pairs derive matching shared secrets when backend supports them", "[Crypto][Asymmetric]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519))
    {
        return;
    }

    auto alice = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context.Value());
    auto bob   = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context.Value());
    REQUIRE(alice.HasValue());
    REQUIRE(bob.HasValue());

    auto aliceSecret = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(
            context.Value(),
            alice.Value().privateKey,
            bob.Value().publicKey);
    auto bobSecret = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(
            context.Value(),
            bob.Value().privateKey,
            alice.Value().publicKey);

    REQUIRE(aliceSecret.HasValue());
    REQUIRE(bobSecret.HasValue());
    RequireBytesEqual(aliceSecret.Value().Bytes(), bobSecret.Value().Bytes());
}

TEST_CASE("X25519 matches RFC 7748 test vector when backend supports it", "[Crypto][Asymmetric]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519))
    {
        return;
    }

    auto privateKey = NGIN::Crypto::Asymmetric::X25519PrivateKey::FromBytes(
            DecodeFixedHex<32>("77076d0a7318a57d3c16c17251b26645"
                               "df4c2f87ebc0992ab177fba51db92c2a"));
    auto peerPublicKey = NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(
            DecodeFixedHex<32>("de9edb7d7b7dc1b4d35b61c2ece43537"
                               "3f8343c85b78674dadfc7e146f882b4f"));
    auto expectedSharedSecret = DecodeFixedHex<32>("4a5d9d5ba4ce2de1728e3bf480350f25"
                                                   "e07e21c947d19e3376f09b3c1e161742");

    auto sharedSecret = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(context.Value(), privateKey, peerPublicKey);

    REQUIRE(sharedSecret.HasValue());
    RequireBytesEqual(sharedSecret.Value().Bytes(), expectedSharedSecret);
}

TEST_CASE("X25519 contract does not fake implementation even if capability is manually enabled", "[Crypto][Asymmetric]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::KeyAgreementAlgorithm::X25519);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "x25519-capable-test"},
            capabilities,
    };
    auto privateKey = NGIN::Crypto::Asymmetric::X25519PrivateKey::FromBytes(ZeroBytes<32>());
    auto publicKey  = NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(ZeroBytes<32>());
    auto output     = ZeroBytes<32>();

    auto deriveInto  = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecretInto(context, privateKey, publicKey, output);
    auto deriveOwned = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(context, privateKey, publicKey);
    auto generate    = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context);

    REQUIRE_FALSE(deriveInto.HasValue());
    REQUIRE(deriveInto.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(deriveOwned.HasValue());
    REQUIRE(deriveOwned.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(generate.HasValue());
    REQUIRE(generate.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}
