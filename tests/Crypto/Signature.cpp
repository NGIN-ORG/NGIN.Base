#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{
    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr NGIN::Crypto::FixedBytes<Size> ZeroBytes() noexcept
    {
        return {};
    }

    [[nodiscard]] NGIN::Crypto::Memory::SecretView TestSecret() noexcept
    {
        static constexpr auto SECRET = ZeroBytes<32>();
        return NGIN::Crypto::Memory::SecretView {SECRET};
    }
}// namespace

TEST_CASE("Signature metadata reports fixed Ed25519 signature size", "[Crypto][Signature]")
{
    REQUIRE(NGIN::Crypto::Signatures::SignatureSize(NGIN::Crypto::SignatureAlgorithm::Ed25519) == 64);
    REQUIRE(NGIN::Crypto::Signatures::SignatureSize(NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256) == 0);
    REQUIRE(NGIN::Crypto::Signatures::SignatureSize(NGIN::Crypto::SignatureAlgorithm::RsaPssSha256) == 0);
}

TEST_CASE("SignInto validates key and output sizes before backend support", "[Crypto][Signature]")
{
    static constexpr auto SHORT_SECRET = ZeroBytes<31>();

    auto message   = ZeroBytes<8>();
    auto signature = ZeroBytes<64>();
    auto tooSmall  = ZeroBytes<63>();

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    NGIN::Crypto::Signatures::SignInput invalidKeyInput {
            .privateKey = NGIN::Crypto::Memory::SecretView {SHORT_SECRET},
            .message    = message,
    };
    auto invalidKey = NGIN::Crypto::Signatures::SignInto(
            context,
            NGIN::Crypto::SignatureAlgorithm::Ed25519,
            invalidKeyInput,
            signature);

    REQUIRE_FALSE(invalidKey.HasValue());
    REQUIRE(invalidKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    NGIN::Crypto::Signatures::SignInput validInput {
            .privateKey = TestSecret(),
            .message    = message,
    };
    auto invalidOutput = NGIN::Crypto::Signatures::SignInto(
            context,
            NGIN::Crypto::SignatureAlgorithm::Ed25519,
            validInput,
            tooSmall);

    REQUIRE_FALSE(invalidOutput.HasValue());
    REQUIRE(invalidOutput.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("Verify validates public key and signature sizes before backend support", "[Crypto][Signature]")
{
    auto shortPublicKey = ZeroBytes<31>();
    auto publicKey      = ZeroBytes<32>();
    auto message        = ZeroBytes<8>();
    auto shortSignature = ZeroBytes<63>();
    auto signature      = ZeroBytes<64>();

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto invalidKey = NGIN::Crypto::Signatures::Verify(
            context,
            NGIN::Crypto::SignatureAlgorithm::Ed25519,
            NGIN::Crypto::Signatures::VerifyInput {
                    .publicKey = shortPublicKey,
                    .message   = message,
                    .signature = signature,
            });

    REQUIRE_FALSE(invalidKey.HasValue());
    REQUIRE(invalidKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    auto invalidSignature = NGIN::Crypto::Signatures::Verify(
            context,
            NGIN::Crypto::SignatureAlgorithm::Ed25519,
            NGIN::Crypto::Signatures::VerifyInput {
                    .publicKey = publicKey,
                    .message   = message,
                    .signature = shortSignature,
            });

    REQUIRE_FALSE(invalidSignature.HasValue());
    REQUIRE(invalidSignature.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidTag);
}

TEST_CASE("Signature contract does not fake implementation even if capability is manually enabled", "[Crypto][Signature]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::SignatureAlgorithm::Ed25519);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "ed25519-capable-test"},
            capabilities,
    };

    auto publicKey  = NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(ZeroBytes<32>());
    auto privateKey = NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromBytes(ZeroBytes<32>());
    auto message    = ZeroBytes<8>();
    auto signature  = ZeroBytes<64>();

    auto signInto = NGIN::Crypto::Asymmetric::SignEd25519Into(context, privateKey, message, signature);
    auto sign     = NGIN::Crypto::Asymmetric::SignEd25519(context, privateKey, message);
    auto verify   = NGIN::Crypto::Asymmetric::VerifyEd25519(context, publicKey, message, signature);
    auto generate = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context);

    REQUIRE_FALSE(signInto.HasValue());
    REQUIRE(signInto.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(sign.HasValue());
    REQUIRE(sign.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(verify.HasValue());
    REQUIRE(verify.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(generate.HasValue());
    REQUIRE(generate.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}
