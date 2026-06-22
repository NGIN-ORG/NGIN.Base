#include "ProviderVectors/SignatureVectors.hpp"

#include <NGIN/Crypto/Asymmetric/Ecdsa.hpp>
#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

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

    [[nodiscard]] NGIN::Crypto::ByteBuffer DecodeHexBytes(std::string_view text)
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeHex(text);
        REQUIRE(decoded.HasValue());
        return decoded.Value();
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(const NGIN::Crypto::ByteBuffer& bytes) noexcept
    {
        return NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()};
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Bytes(std::initializer_list<NGIN::UInt32> values)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(values.size());

        NGIN::UIntSize index = 0;
        for (auto value: values)
        {
            buffer[index++] = static_cast<NGIN::Byte>(value);
        }

        return buffer;
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

TEST_CASE("Signature metadata reports fixed signature sizes", "[Crypto][Signature]")
{
    REQUIRE(NGIN::Crypto::Signatures::SignatureSize(NGIN::Crypto::SignatureAlgorithm::Ed25519) == 64);
    REQUIRE(NGIN::Crypto::Signatures::SignatureSize(NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256) == 64);
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

TEST_CASE("Ed25519 generated keys sign and verify when backend supports them", "[Crypto][Signature]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519))
    {
        return;
    }

    auto keyPair = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context.Value());
    REQUIRE(keyPair.HasValue());

    auto message   = DecodeFixedHex<32>("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto signature = NGIN::Crypto::Asymmetric::SignEd25519(context.Value(), keyPair.Value().privateKey, message);
    REQUIRE(signature.HasValue());

    auto verified = NGIN::Crypto::Asymmetric::VerifyEd25519(
            context.Value(),
            keyPair.Value().publicKey,
            message,
            signature.Value());
    REQUIRE(verified.HasValue());

    auto tamperedMessage = message;
    tamperedMessage[0] ^= NGIN::Byte {0x01};
    auto rejectedMessage = NGIN::Crypto::Asymmetric::VerifyEd25519(
            context.Value(),
            keyPair.Value().publicKey,
            tamperedMessage,
            signature.Value());
    REQUIRE_FALSE(rejectedMessage.HasValue());
    REQUIRE(rejectedMessage.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);

    auto tamperedSignature = signature.Value();
    tamperedSignature[0] ^= NGIN::Byte {0x01};
    auto rejectedSignature = NGIN::Crypto::Asymmetric::VerifyEd25519(
            context.Value(),
            keyPair.Value().publicKey,
            message,
            tamperedSignature);
    REQUIRE_FALSE(rejectedSignature.HasValue());
    REQUIRE(rejectedSignature.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
}

TEST_CASE("Ed25519 matches RFC 8032 test vector when backend supports it", "[Crypto][Signature]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519))
    {
        return;
    }

    auto privateKey = NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromBytes(
            DecodeFixedHex<32>("9d61b19deffd5a60ba844af492ec2cc4"
                               "4449c5697b326919703bac031cae7f60"));
    auto publicKey = NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(
            DecodeFixedHex<32>("d75a980182b10ab7d54bfed3c964073a"
                               "0ee172f3daa62325af021a68f707511a"));
    auto expectedSignature = DecodeFixedHex<64>(
            "e5564300c360ac729086e2cc806e828a"
            "84877f1eb8e5d974d873e06522490155"
            "5fb8821590a33bacc61e39701cf9b46b"
            "d25bf5f0595bbe24655141438e7a100b");

    auto signature = NGIN::Crypto::Asymmetric::SignEd25519(context.Value(), privateKey, NGIN::Crypto::ConstByteSpan {});
    REQUIRE(signature.HasValue());
    RequireBytesEqual(signature.Value(), expectedSignature);

    auto verified = NGIN::Crypto::Asymmetric::VerifyEd25519(
            context.Value(),
            publicKey,
            NGIN::Crypto::ConstByteSpan {},
            expectedSignature);
    REQUIRE(verified.HasValue());
}

TEST_CASE("ECDSA P-256 SHA-256 converts fixed raw signatures to and from DER", "[Crypto][Signature]")
{
    const auto& vector = NGIN::Crypto::Tests::ProviderVectors::ECDSA_P256_SHA256_REGRESSION;

    auto rawSignature = DecodeFixedHex<64>(vector.signatureHex);
    auto derSignature = DecodeHexBytes(
            "3045022100d62c2d0fb80511496302798dfd800b35384feb5be149e7c035e3fcd47532ea49"
            "02203476f395b081744f4305707efde1d76c2899e23a443b6e6c447cb6389ddf29d1");

    auto encoded = NGIN::Crypto::Asymmetric::EncodeEcdsaP256Sha256SignatureDer(rawSignature);
    REQUIRE(encoded.HasValue());
    RequireBytesEqual(Bytes(encoded.Value()), Bytes(derSignature));

    auto parsed = NGIN::Crypto::Asymmetric::ParseEcdsaP256Sha256SignatureDer(Bytes(derSignature));
    REQUIRE(parsed.HasValue());
    RequireBytesEqual(parsed.Value(), rawSignature);
}

TEST_CASE("ECDSA P-256 SHA-256 DER parser rejects malformed signatures", "[Crypto][Signature]")
{
    auto validWithTrailing = DecodeHexBytes(
            "3006020101020101"
            "00");
    auto negativeInteger    = DecodeHexBytes("30060201ff020101");
    auto nonMinimalInteger  = DecodeHexBytes("300702020001020101");
    auto missingInteger     = DecodeHexBytes("3003020101");
    auto wrongTopLevel      = DecodeHexBytes("020101");
    auto oversizedComponent = Bytes({
            0x30,
            0x27,
            0x02,
            0x22,
            0x00,
            0x01,
            0x02,
            0x03,
            0x04,
            0x05,
            0x06,
            0x07,
            0x08,
            0x09,
            0x0a,
            0x0b,
            0x0c,
            0x0d,
            0x0e,
            0x0f,
            0x10,
            0x11,
            0x12,
            0x13,
            0x14,
            0x15,
            0x16,
            0x17,
            0x18,
            0x19,
            0x1a,
            0x1b,
            0x1c,
            0x1d,
            0x1e,
            0x1f,
            0x20,
            0x21,
            0x02,
            0x01,
            0x01,
    });

    const auto requireRejected = [](NGIN::Crypto::ConstByteSpan candidate) {
        auto parsed = NGIN::Crypto::Asymmetric::ParseEcdsaP256Sha256SignatureDer(candidate);
        REQUIRE_FALSE(parsed.HasValue());
        REQUIRE(parsed.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    };

    requireRejected(Bytes(validWithTrailing));
    requireRejected(Bytes(negativeInteger));
    requireRejected(Bytes(nonMinimalInteger));
    requireRejected(Bytes(missingInteger));
    requireRejected(Bytes(wrongTopLevel));
    requireRejected(Bytes(oversizedComponent));
}

TEST_CASE("ECDSA P-256 SHA-256 typed wrappers verify and sign when backend supports them", "[Crypto][Signature]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256))
    {
        return;
    }

    const auto& vector = NGIN::Crypto::Tests::ProviderVectors::ECDSA_P256_SHA256_REGRESSION;

    auto privateKey        = NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey::FromBytes(DecodeFixedHex<32>(vector.privateKeyHex));
    auto publicKey         = NGIN::Crypto::Asymmetric::EcdsaP256PublicKey::FromBytes(DecodeFixedHex<65>(vector.publicKeyHex));
    auto message           = DecodeHexBytes(vector.messageHex);
    auto expectedSignature = DecodeFixedHex<64>(vector.signatureHex);

    auto verified = NGIN::Crypto::Asymmetric::VerifyEcdsaP256Sha256(
            context.Value(),
            publicKey,
            Bytes(message),
            expectedSignature);
    REQUIRE(verified.HasValue());

    auto signature = NGIN::Crypto::Asymmetric::SignEcdsaP256Sha256(
            context.Value(),
            privateKey,
            Bytes(message));
    REQUIRE(signature.HasValue());

    auto generatedVerified = NGIN::Crypto::Asymmetric::VerifyEcdsaP256Sha256(
            context.Value(),
            publicKey,
            Bytes(message),
            signature.Value());
    REQUIRE(generatedVerified.HasValue());

    auto tampered = signature.Value();
    tampered[0] ^= NGIN::Byte {0x01};
    auto rejected = NGIN::Crypto::Asymmetric::VerifyEcdsaP256Sha256(
            context.Value(),
            publicKey,
            Bytes(message),
            tampered);
    REQUIRE_FALSE(rejected.HasValue());
    REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
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
