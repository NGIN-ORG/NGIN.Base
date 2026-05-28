#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace
{
    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr NGIN::Crypto::FixedBytes<Size> ZeroBytes() noexcept
    {
        return {};
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
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context.Value());

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
