#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace
{
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

    [[nodiscard]] NGIN::Crypto::ByteBuffer RepeatedByte(NGIN::UInt8 value, NGIN::UIntSize count)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(count);
        for (NGIN::UIntSize i = 0; i < count; ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(value);
        }
        return buffer;
    }

    [[nodiscard]] NGIN::Containers::Vector<NGIN::UInt32> Oid(std::initializer_list<NGIN::UInt32> arcs)
    {
        NGIN::Containers::Vector<NGIN::UInt32> result;
        for (auto arc: arcs)
        {
            result.PushBack(arc);
        }
        return result;
    }

    void RequireBytesEqual(const NGIN::Crypto::ByteBuffer& bytes, std::initializer_list<NGIN::UInt32> expected)
    {
        REQUIRE(bytes.Size() == expected.size());

        NGIN::UIntSize index = 0;
        for (auto value: expected)
        {
            REQUIRE(bytes[index++] == static_cast<NGIN::Byte>(value));
        }
    }

    void RequireBytesEqual(const NGIN::Crypto::ByteBuffer& actual, const NGIN::Crypto::ByteBuffer& expected)
    {
        REQUIRE(actual.Size() == expected.Size());
        for (NGIN::UIntSize i = 0; i < expected.Size(); ++i)
        {
            REQUIRE(actual[i] == expected[i]);
        }
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, const NGIN::Crypto::ByteBuffer& expected)
    {
        REQUIRE(actual.size() == expected.Size());
        for (NGIN::UIntSize i = 0; i < expected.Size(); ++i)
        {
            REQUIRE(actual[i] == expected[i]);
        }
    }
}// namespace

TEST_CASE("SubjectPublicKeyInfo writes and parses Ed25519", "[Crypto][KeyFormat]")
{
    const auto publicKey = RepeatedByte(0x11, 32);

    auto der = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()});

    REQUIRE(der.HasValue());
    REQUIRE(der.Value().Size() == 44);
    RequireBytesEqual(
            der.Value(),
            {
                    0x30,
                    0x2a,
                    0x30,
                    0x05,
                    0x06,
                    0x03,
                    0x2b,
                    0x65,
                    0x70,
                    0x03,
                    0x21,
                    0x00,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
            });

    auto parsed = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.Value().data(), der.Value().Size()});

    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE_FALSE(parsed.Value().algorithm.hasParameters);
    RequireBytesEqual(parsed.Value().publicKey, publicKey);

    auto signatureAlgorithm = NGIN::Crypto::Keys::ToSignatureAlgorithm(parsed.Value().algorithm.algorithm);
    REQUIRE(signatureAlgorithm.HasValue());
    REQUIRE(signatureAlgorithm.Value() == NGIN::Crypto::SignatureAlgorithm::Ed25519);

    auto imported = NGIN::Crypto::Keys::ImportEd25519PublicKey(parsed.Value());
    REQUIRE(imported.HasValue());
    RequireBytesEqual(imported.Value().Bytes(), publicKey);

    auto exported = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(imported.Value());
    REQUIRE(exported.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE_FALSE(exported.algorithm.hasParameters);
    RequireBytesEqual(exported.publicKey, publicKey);
}

TEST_CASE("PrivateKeyInfo writes and parses X25519", "[Crypto][KeyFormat]")
{
    const auto privateKey = RepeatedByte(0x22, 32);

    auto der = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::X25519,
            NGIN::Crypto::ConstByteSpan {privateKey.data(), privateKey.Size()});

    REQUIRE(der.HasValue());

    auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.Value().data(), der.Value().Size()});

    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().version == 0);
    REQUIRE(parsed.Value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::X25519);
    REQUIRE_FALSE(parsed.Value().algorithm.hasParameters);
    RequireBytesEqual(parsed.Value().privateKey, privateKey);

    auto keyAgreementAlgorithm = NGIN::Crypto::Keys::ToKeyAgreementAlgorithm(parsed.Value().algorithm.algorithm);
    REQUIRE(keyAgreementAlgorithm.HasValue());
    REQUIRE(keyAgreementAlgorithm.Value() == NGIN::Crypto::KeyAgreementAlgorithm::X25519);

    auto imported = NGIN::Crypto::Keys::ImportX25519PrivateKey(parsed.Value());
    REQUIRE(imported.HasValue());
    RequireBytesEqual(imported.Value().Bytes(), privateKey);

    auto exported = NGIN::Crypto::Keys::ExportPrivateKeyInfo(imported.Value());
    REQUIRE(exported.version == 0);
    REQUIRE(exported.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::X25519);
    REQUIRE_FALSE(exported.algorithm.hasParameters);
    RequireBytesEqual(exported.privateKey, privateKey);
}

TEST_CASE("SubjectPublicKeyInfo preserves ECDSA P-256 algorithm parameters", "[Crypto][KeyFormat]")
{
    auto publicKey = RepeatedByte(0x33, 65);
    publicKey[0]   = NGIN::Byte {0x04};

    auto der = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()});
    REQUIRE(der.HasValue());

    auto parsed = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.Value().data(), der.Value().Size()});

    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(parsed.Value().algorithm.hasParameters);
    RequireBytesEqual(parsed.Value().algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(parsed.Value().publicKey, publicKey);

    auto signatureAlgorithm = NGIN::Crypto::Keys::ToSignatureAlgorithm(parsed.Value().algorithm.algorithm);
    REQUIRE(signatureAlgorithm.HasValue());
    REQUIRE(signatureAlgorithm.Value() == NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256);

    auto imported = NGIN::Crypto::Keys::ImportEcdsaP256PublicKey(parsed.Value());
    REQUIRE(imported.HasValue());
    RequireBytesEqual(imported.Value().Bytes(), publicKey);

    auto exported = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(imported.Value());
    REQUIRE(exported.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(exported.algorithm.hasParameters);
    RequireBytesEqual(exported.algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(exported.publicKey, publicKey);
}

TEST_CASE("PrivateKeyInfo imports and exports ECDSA P-256 private scalars", "[Crypto][KeyFormat]")
{
    const auto privateKey = RepeatedByte(0x55, 32);

    auto der = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {privateKey.data(), privateKey.Size()});
    REQUIRE(der.HasValue());

    auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.Value().data(), der.Value().Size()});
    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().version == 0);
    REQUIRE(parsed.Value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(parsed.Value().algorithm.hasParameters);
    RequireBytesEqual(parsed.Value().algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(parsed.Value().privateKey, privateKey);

    auto imported = NGIN::Crypto::Keys::ImportEcdsaP256PrivateKey(parsed.Value());
    REQUIRE(imported.HasValue());
    RequireBytesEqual(imported.Value().Bytes(), privateKey);

    auto exported = NGIN::Crypto::Keys::ExportPrivateKeyInfo(imported.Value());
    REQUIRE(exported.version == 0);
    REQUIRE(exported.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(exported.algorithm.hasParameters);
    RequireBytesEqual(exported.algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(exported.privateKey, privateKey);
}

TEST_CASE("EncryptedPrivateKeyInfo preserves algorithm parameters and encrypted payload", "[Crypto][KeyFormat]")
{
    const auto encryptedInfo = Bytes({
            0x30,
            0x0d,
            0x30,
            0x06,
            0x06,
            0x02,
            0x2a,
            0x03,
            0x05,
            0x00,
            0x04,
            0x03,
            0xaa,
            0xbb,
            0xcc,
    });

    auto parsed = NGIN::Crypto::Keys::ParseEncryptedPrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {encryptedInfo.data(), encryptedInfo.Size()});
    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().encryptionAlgorithm.objectIdentifier.Size() == 3);
    REQUIRE(parsed.Value().encryptionAlgorithm.objectIdentifier[0] == 1);
    REQUIRE(parsed.Value().encryptionAlgorithm.objectIdentifier[1] == 2);
    REQUIRE(parsed.Value().encryptionAlgorithm.objectIdentifier[2] == 3);
    REQUIRE(parsed.Value().encryptionAlgorithm.hasParameters);
    RequireBytesEqual(parsed.Value().encryptionAlgorithm.parameters, {0x05, 0x00});
    RequireBytesEqual(parsed.Value().encryptedData, {0xaa, 0xbb, 0xcc});

    auto written = NGIN::Crypto::Keys::WriteEncryptedPrivateKeyInfo(
            parsed.Value().encryptionAlgorithm,
            NGIN::Crypto::ConstByteSpan {parsed.Value().encryptedData.data(), parsed.Value().encryptedData.Size()});
    REQUIRE(written.HasValue());
    RequireBytesEqual(written.Value(), encryptedInfo);
}

TEST_CASE("EncryptedPrivateKeyInfo rejects malformed envelopes and invalid raw parameters", "[Crypto][KeyFormat]")
{
    const auto extraField = Bytes({
            0x30,
            0x0f,
            0x30,
            0x06,
            0x06,
            0x02,
            0x2a,
            0x03,
            0x05,
            0x00,
            0x04,
            0x01,
            0xaa,
            0x05,
            0x00,
    });
    auto parsedExtra = NGIN::Crypto::Keys::ParseEncryptedPrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {extraField.data(), extraField.Size()});
    REQUIRE_FALSE(parsedExtra.HasValue());
    REQUIRE(parsedExtra.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    auto malformedParameters = NGIN::Crypto::MakeByteBuffer(1);
    malformedParameters[0]   = NGIN::Byte {0x05};

    NGIN::Crypto::Keys::EncryptedPrivateKeyAlgorithmIdentifier invalidIdentifier {
            .objectIdentifier = Oid({1, 2, 3}),
            .parameters       = malformedParameters,
            .hasParameters    = true,
    };

    const auto encryptedData = Bytes({0xaa});
    auto       written       = NGIN::Crypto::Keys::WriteEncryptedPrivateKeyInfo(
            invalidIdentifier,
            NGIN::Crypto::ConstByteSpan {encryptedData.data(), encryptedData.Size()});
    REQUIRE_FALSE(written.HasValue());
    REQUIRE(written.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    invalidIdentifier.hasParameters = false;
    written                         = NGIN::Crypto::Keys::WriteEncryptedPrivateKeyInfo(
            invalidIdentifier,
            NGIN::Crypto::ConstByteSpan {encryptedData.data(), encryptedData.Size()});
    REQUIRE_FALSE(written.HasValue());
    REQUIRE(written.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("Key format parsers reject invalid algorithm parameters and versions", "[Crypto][KeyFormat]")
{
    const auto ed25519WithNullParameters = Bytes({
            0x30,
            0x2c,
            0x30,
            0x07,
            0x06,
            0x03,
            0x2b,
            0x65,
            0x70,
            0x05,
            0x00,
            0x03,
            0x21,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
    });

    auto invalidSpki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {ed25519WithNullParameters.data(), ed25519WithNullParameters.Size()});
    REQUIRE_FALSE(invalidSpki.HasValue());
    REQUIRE(invalidSpki.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto versionOnePrivateKeyInfo = Bytes({
            0x30,
            0x0e,
            0x02,
            0x01,
            0x01,
            0x30,
            0x05,
            0x06,
            0x03,
            0x2b,
            0x65,
            0x6e,
            0x04,
            0x02,
            0x22,
            0x22,
    });

    auto invalidPrivateKey = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {versionOnePrivateKeyInfo.data(), versionOnePrivateKeyInfo.Size()});
    REQUIRE_FALSE(invalidPrivateKey.HasValue());
    REQUIRE(invalidPrivateKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    auto unsupportedAgreement = NGIN::Crypto::Keys::ToKeyAgreementAlgorithm(NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE_FALSE(unsupportedAgreement.HasValue());
    REQUIRE(unsupportedAgreement.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);

    auto x25519PublicKey = RepeatedByte(0x44, 32);
    auto x25519Der       = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::X25519,
            NGIN::Crypto::ConstByteSpan {x25519PublicKey.data(), x25519PublicKey.Size()});
    REQUIRE(x25519Der.HasValue());

    auto x25519Spki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {x25519Der.Value().data(), x25519Der.Value().Size()});
    REQUIRE(x25519Spki.HasValue());
    auto mismatchedPublicKey = NGIN::Crypto::Keys::ImportEd25519PublicKey(x25519Spki.Value());
    REQUIRE_FALSE(mismatchedPublicKey.HasValue());
    REQUIRE(mismatchedPublicKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    auto compressedEcdsaPublicKey = RepeatedByte(0x33, 65);
    compressedEcdsaPublicKey[0]   = NGIN::Byte {0x03};
    auto compressedEcdsaDer       = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {compressedEcdsaPublicKey.data(), compressedEcdsaPublicKey.Size()});
    REQUIRE(compressedEcdsaDer.HasValue());
    auto compressedEcdsaSpki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {compressedEcdsaDer.Value().data(), compressedEcdsaDer.Value().Size()});
    REQUIRE(compressedEcdsaSpki.HasValue());
    auto rejectedCompressedEcdsa = NGIN::Crypto::Keys::ImportEcdsaP256PublicKey(compressedEcdsaSpki.Value());
    REQUIRE_FALSE(rejectedCompressedEcdsa.HasValue());
    REQUIRE(rejectedCompressedEcdsa.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    auto shortEcdsaPrivateKey = RepeatedByte(0x55, 31);
    auto shortEcdsaDer        = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {shortEcdsaPrivateKey.data(), shortEcdsaPrivateKey.Size()});
    REQUIRE(shortEcdsaDer.HasValue());
    auto shortEcdsaPrivateKeyInfo = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {shortEcdsaDer.Value().data(), shortEcdsaDer.Value().Size()});
    REQUIRE(shortEcdsaPrivateKeyInfo.HasValue());
    auto rejectedShortEcdsaPrivateKey = NGIN::Crypto::Keys::ImportEcdsaP256PrivateKey(shortEcdsaPrivateKeyInfo.Value());
    REQUIRE_FALSE(rejectedShortEcdsaPrivateKey.HasValue());
    REQUIRE(rejectedShortEcdsaPrivateKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);
}

TEST_CASE("Key format malformed corpus rejects truncated and extra structures", "[Crypto][KeyFormat]")
{
    for (auto bytes: {
                 Bytes({}),
                 Bytes({0x30}),
                 Bytes({0x30, 0x03, 0x30, 0x01}),
                 Bytes({0x30, 0x00}),
                 Bytes({0x04, 0x00}),
                 Bytes({0x30, 0x07, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70}),
         })
    {
        auto spki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
                NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()});
        REQUIRE_FALSE(spki.HasValue());
        REQUIRE(spki.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

        auto privateKey = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
                NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()});
        REQUIRE_FALSE(privateKey.HasValue());
        REQUIRE(privateKey.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}
