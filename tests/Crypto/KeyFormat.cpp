#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Keys/KeyOperations.hpp>
#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <NGIN/Crypto/Kdf/Pbkdf2.hpp>
#include <NGIN/Crypto/Memory/SecureBuffer.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
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

    void AppendBytes(NGIN::Crypto::ByteBuffer& output, NGIN::Crypto::ConstByteSpan bytes)
    {
        for (NGIN::Byte byte: bytes)
        {
            output.PushBack(byte);
        }
    }

    void AppendBytes(NGIN::Crypto::ByteBuffer& output, const NGIN::Crypto::ByteBuffer& bytes)
    {
        AppendBytes(output, NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()});
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DerInteger(NGIN::UInt32 value)
    {
        NGIN::UInt8    encoded[5] {};
        NGIN::UIntSize count = 0;
        do
        {
            encoded[count++] = static_cast<NGIN::UInt8>(value & 0xffu);
            value >>= 8u;
        } while (value != 0);

        NGIN::Crypto::ByteBuffer valueBytes;
        if ((encoded[count - 1] & 0x80u) != 0)
        {
            valueBytes.PushBack(NGIN::Byte {0x00});
        }
        for (NGIN::UIntSize i = 0; i < count; ++i)
        {
            valueBytes.PushBack(static_cast<NGIN::Byte>(encoded[count - i - 1]));
        }

        auto integer = NGIN::Crypto::Encoding::EncodeDerInteger(
                NGIN::Crypto::ConstByteSpan {valueBytes.data(), valueBytes.Size()});
        REQUIRE(integer.has_value());
        return integer.value();
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DerOctetString(const NGIN::Crypto::ByteBuffer& bytes)
    {
        auto octets = NGIN::Crypto::Encoding::EncodeDerOctetString(
                NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()});
        REQUIRE(octets.has_value());
        return octets.value();
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DerOid(std::initializer_list<NGIN::UInt32> arcs)
    {
        std::array<NGIN::UInt32, 9> oidArcs {};
        REQUIRE(arcs.size() <= oidArcs.size());

        NGIN::UIntSize index = 0;
        for (auto arc: arcs)
        {
            oidArcs[index++] = arc;
        }

        auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::span<const NGIN::UInt32> {oidArcs.data(), index});
        REQUIRE(oid.has_value());
        return oid.value();
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DerSequence(std::initializer_list<const NGIN::Crypto::ByteBuffer*> children)
    {
        NGIN::Crypto::ByteBuffer encodedChildren;
        for (const auto* child: children)
        {
            AppendBytes(encodedChildren, *child);
        }

        auto sequence = NGIN::Crypto::Encoding::EncodeDerSequence(
                NGIN::Crypto::ConstByteSpan {encodedChildren.data(), encodedChildren.Size()});
        REQUIRE(sequence.has_value());
        return sequence.value();
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer AlgorithmIdentifier(
            std::initializer_list<NGIN::UInt32> oidArcs,
            const NGIN::Crypto::ByteBuffer*     parameters = nullptr)
    {
        auto oid = DerOid(oidArcs);
        if (parameters == nullptr)
        {
            return DerSequence({&oid});
        }
        return DerSequence({&oid, parameters});
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Pbes2Pbkdf2Sha256Aes256GcmParameters(
            const NGIN::Crypto::ByteBuffer& salt,
            NGIN::UInt32                    iterations,
            const NGIN::Crypto::ByteBuffer& nonce)
    {
        auto saltOctets       = DerOctetString(salt);
        auto iterationInteger = DerInteger(iterations);
        auto keyLengthInteger = DerInteger(32);
        auto nullParameters   = NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::MakeDerUniversalTag(NGIN::Crypto::Encoding::DerUniversalTag::Null),
                NGIN::Crypto::ConstByteSpan {});
        REQUIRE(nullParameters.has_value());

        auto prf              = AlgorithmIdentifier({1, 2, 840, 113549, 2, 9}, &nullParameters.value());
        auto pbkdf2Parameters = DerSequence({&saltOctets, &iterationInteger, &keyLengthInteger, &prf});
        auto keyDerivation    = AlgorithmIdentifier({1, 2, 840, 113549, 1, 5, 12}, &pbkdf2Parameters);

        auto nonceOctets      = DerOctetString(nonce);
        auto tagLength        = DerInteger(16);
        auto gcmParameters    = DerSequence({&nonceOctets, &tagLength});
        auto encryptionScheme = AlgorithmIdentifier({2, 16, 840, 1, 101, 3, 4, 1, 46}, &gcmParameters);

        return DerSequence({&keyDerivation, &encryptionScheme});
    }
}// namespace

TEST_CASE("SubjectPublicKeyInfo writes and parses Ed25519", "[Crypto][KeyFormat]")
{
    const auto publicKey = RepeatedByte(0x11, 32);

    auto der = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()});

    REQUIRE(der.has_value());
    REQUIRE(der.value().Size() == 44);
    RequireBytesEqual(
            der.value(),
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
            NGIN::Crypto::ConstByteSpan {der.value().data(), der.value().Size()});

    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE_FALSE(parsed.value().algorithm.hasParameters);
    RequireBytesEqual(parsed.value().publicKey, publicKey);

    auto signatureAlgorithm = NGIN::Crypto::Keys::ToSignatureAlgorithm(parsed.value().algorithm.algorithm);
    REQUIRE(signatureAlgorithm.has_value());
    REQUIRE(signatureAlgorithm.value() == NGIN::Crypto::SignatureAlgorithm::Ed25519);

    auto imported = NGIN::Crypto::Keys::ImportEd25519PublicKey(parsed.value());
    REQUIRE(imported.has_value());
    RequireBytesEqual(imported.value().Bytes(), publicKey);

    auto exported = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(imported.value());
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

    REQUIRE(der.has_value());

    auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.value().data(), der.value().Size()});

    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().version == 0);
    REQUIRE(parsed.value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::X25519);
    REQUIRE_FALSE(parsed.value().algorithm.hasParameters);
    RequireBytesEqual(parsed.value().privateKey, privateKey);

    auto keyAgreementAlgorithm = NGIN::Crypto::Keys::ToKeyAgreementAlgorithm(parsed.value().algorithm.algorithm);
    REQUIRE(keyAgreementAlgorithm.has_value());
    REQUIRE(keyAgreementAlgorithm.value() == NGIN::Crypto::KeyAgreementAlgorithm::X25519);

    auto imported = NGIN::Crypto::Keys::ImportX25519PrivateKey(parsed.value());
    REQUIRE(imported.has_value());
    RequireBytesEqual(imported.value().Bytes(), privateKey);

    auto exported = NGIN::Crypto::Keys::ExportPrivateKeyInfo(imported.value());
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
    REQUIRE(der.has_value());

    auto parsed = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.value().data(), der.value().Size()});

    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(parsed.value().algorithm.hasParameters);
    RequireBytesEqual(parsed.value().algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(parsed.value().publicKey, publicKey);

    auto signatureAlgorithm = NGIN::Crypto::Keys::ToSignatureAlgorithm(parsed.value().algorithm.algorithm);
    REQUIRE(signatureAlgorithm.has_value());
    REQUIRE(signatureAlgorithm.value() == NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256);

    auto imported = NGIN::Crypto::Keys::ImportEcdsaP256PublicKey(parsed.value());
    REQUIRE(imported.has_value());
    RequireBytesEqual(imported.value().Bytes(), publicKey);

    auto exported = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(imported.value());
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
    REQUIRE(der.has_value());

    auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {der.value().data(), der.value().Size()});
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().version == 0);
    REQUIRE(parsed.value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(parsed.value().algorithm.hasParameters);
    RequireBytesEqual(parsed.value().algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(parsed.value().privateKey, privateKey);

    auto imported = NGIN::Crypto::Keys::ImportEcdsaP256PrivateKey(parsed.value());
    REQUIRE(imported.has_value());
    RequireBytesEqual(imported.value().Bytes(), privateKey);

    auto exported = NGIN::Crypto::Keys::ExportPrivateKeyInfo(imported.value());
    REQUIRE(exported.version == 0);
    REQUIRE(exported.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256);
    REQUIRE(exported.algorithm.hasParameters);
    RequireBytesEqual(exported.algorithm.parameters, {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07});
    RequireBytesEqual(exported.privateKey, privateKey);
}

TEST_CASE("Parsed key operations reject mismatched algorithms before backend dispatch", "[Crypto][KeyFormat]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.has_value());

    const auto privateKey = RepeatedByte(0x11, 32);
    auto       privateDer = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            NGIN::Crypto::ConstByteSpan {privateKey.data(), privateKey.Size()});
    REQUIRE(privateDer.has_value());
    auto privateKeyInfo = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {privateDer.value().data(), privateDer.value().Size()});
    REQUIRE(privateKeyInfo.has_value());

    const auto publicKey = RepeatedByte(0x22, 32);
    auto       publicDer = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()});
    REQUIRE(publicDer.has_value());
    auto publicKeyInfo = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {publicDer.value().data(), publicDer.value().Size()});
    REQUIRE(publicKeyInfo.has_value());

    const auto message = Bytes({0x01, 0x02, 0x03});
    auto       sign    = NGIN::Crypto::Keys::SignPrivateKeyInfo(
            context.value(),
            NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
            privateKeyInfo.value(),
            NGIN::Crypto::ConstByteSpan {message.data(), message.Size()});
    REQUIRE_FALSE(sign.has_value());
    REQUIRE(sign.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    const auto signature = RepeatedByte(0x33, 64);
    auto       verify    = NGIN::Crypto::Keys::VerifySubjectPublicKeyInfo(
            context.value(),
            NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
            publicKeyInfo.value(),
            NGIN::Crypto::ConstByteSpan {message.data(), message.Size()},
            NGIN::Crypto::ConstByteSpan {signature.data(), signature.Size()});
    REQUIRE_FALSE(verify.has_value());
    REQUIRE(verify.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    const auto ciphertext = RepeatedByte(0x44, 16);
    auto       decrypt    = NGIN::Crypto::Keys::DecryptPrivateKeyInfoRsaOaepSha256(
            context.value(),
            privateKeyInfo.value(),
            NGIN::Crypto::Keys::RsaOaepPrivateKeyInfoDecryptInput {
                             .ciphertext = NGIN::Crypto::ConstByteSpan {ciphertext.data(), ciphertext.Size()},
                             .label      = {},
            });
    REQUIRE_FALSE(decrypt.has_value());
    REQUIRE(decrypt.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);
}

TEST_CASE("Parsed key operations sign and derive through provider-backed contexts", "[Crypto][KeyFormat]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.has_value());

    const auto message = Bytes({0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65});

    if (context.value().Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519))
    {
        auto keyPair = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context.value());
        REQUIRE(keyPair.has_value());

        auto privateKeyInfo = NGIN::Crypto::Keys::ExportPrivateKeyInfo(keyPair.value().privateKey);
        auto publicKeyInfo  = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(keyPair.value().publicKey);

        auto signature = NGIN::Crypto::Keys::SignPrivateKeyInfo(
                context.value(),
                NGIN::Crypto::SignatureAlgorithm::Ed25519,
                privateKeyInfo,
                NGIN::Crypto::ConstByteSpan {message.data(), message.Size()});
        REQUIRE(signature.has_value());

        auto verified = NGIN::Crypto::Keys::VerifySubjectPublicKeyInfo(
                context.value(),
                NGIN::Crypto::SignatureAlgorithm::Ed25519,
                publicKeyInfo,
                NGIN::Crypto::ConstByteSpan {message.data(), message.Size()},
                NGIN::Crypto::ConstByteSpan {signature.value().data(), signature.value().Size()});
        REQUIRE(verified.has_value());
    }

    if (context.value().Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519))
    {
        auto alice = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context.value());
        auto bob   = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context.value());
        REQUIRE(alice.has_value());
        REQUIRE(bob.has_value());

        auto alicePrivateKeyInfo = NGIN::Crypto::Keys::ExportPrivateKeyInfo(alice.value().privateKey);
        auto bobPrivateKeyInfo   = NGIN::Crypto::Keys::ExportPrivateKeyInfo(bob.value().privateKey);
        auto alicePublicKeyInfo  = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(alice.value().publicKey);
        auto bobPublicKeyInfo    = NGIN::Crypto::Keys::ExportSubjectPublicKeyInfo(bob.value().publicKey);

        auto aliceSecret = NGIN::Crypto::Keys::DeriveX25519SharedSecret(
                context.value(),
                alicePrivateKeyInfo,
                bobPublicKeyInfo);
        auto bobSecret = NGIN::Crypto::Keys::DeriveX25519SharedSecret(
                context.value(),
                bobPrivateKeyInfo,
                alicePublicKeyInfo);
        REQUIRE(aliceSecret.has_value());
        REQUIRE(bobSecret.has_value());
        REQUIRE(aliceSecret.value().Bytes().size() == bobSecret.value().Bytes().size());
        for (NGIN::UIntSize i = 0; i < aliceSecret.value().Bytes().size(); ++i)
        {
            REQUIRE(aliceSecret.value().Bytes()[i] == bobSecret.value().Bytes()[i]);
        }
    }
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
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().encryptionAlgorithm.objectIdentifier.Size() == 3);
    REQUIRE(parsed.value().encryptionAlgorithm.objectIdentifier[0] == 1);
    REQUIRE(parsed.value().encryptionAlgorithm.objectIdentifier[1] == 2);
    REQUIRE(parsed.value().encryptionAlgorithm.objectIdentifier[2] == 3);
    REQUIRE(parsed.value().encryptionAlgorithm.hasParameters);
    RequireBytesEqual(parsed.value().encryptionAlgorithm.parameters, {0x05, 0x00});
    RequireBytesEqual(parsed.value().encryptedData, {0xaa, 0xbb, 0xcc});

    auto written = NGIN::Crypto::Keys::WriteEncryptedPrivateKeyInfo(
            parsed.value().encryptionAlgorithm,
            NGIN::Crypto::ConstByteSpan {parsed.value().encryptedData.data(), parsed.value().encryptedData.Size()});
    REQUIRE(written.has_value());
    RequireBytesEqual(written.value(), encryptedInfo);
}

TEST_CASE("EncryptedPrivateKeyInfo decrypts PBES2 PBKDF2-SHA256 AES-256-GCM when provider supports it", "[Crypto][KeyFormat]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.has_value());
    if (!context.value().Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256) ||
        !context.value().Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm))
    {
        return;
    }

    const auto privateKey        = RepeatedByte(0x5a, 32);
    auto       privateKeyInfoDer = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            NGIN::Crypto::ConstByteSpan {privateKey.data(), privateKey.Size()});
    REQUIRE(privateKeyInfoDer.has_value());

    const auto password = Bytes({0x70, 0x61, 0x73, 0x73});
    const auto salt     = Bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    const auto nonce    = Bytes({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b});

    auto                                derivedKey = NGIN::Crypto::Memory::SecureBuffer {32};
    NGIN::Crypto::Kdf::Pbkdf2Parameters kdfParameters {
            .password = NGIN::Crypto::Memory::SecretView {
                    NGIN::Crypto::ConstByteSpan {password.data(), password.Size()}},
            .salt       = NGIN::Crypto::ConstByteSpan {salt.data(), salt.Size()},
            .iterations = 4096,
    };
    auto keyResult = NGIN::Crypto::Kdf::Pbkdf2Sha256Into(context.value(), kdfParameters, derivedKey.AsBytes());
    REQUIRE(keyResult.has_value());

    auto sealed = NGIN::Crypto::Symmetric::Seal(
            context.value(),
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            NGIN::Crypto::Symmetric::AeadSealInput {
                    .key            = NGIN::Crypto::Memory::SecretView {derivedKey.AsBytes()},
                    .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), nonce.Size()},
                    .plaintext      = NGIN::Crypto::ConstByteSpan {privateKeyInfoDer.value().data(), privateKeyInfoDer.value().Size()},
                    .associatedData = {},
            });
    REQUIRE(sealed.has_value());

    NGIN::Crypto::ByteBuffer encryptedData;
    AppendBytes(encryptedData, sealed.value().ciphertext);
    AppendBytes(encryptedData, sealed.value().tag);

    NGIN::Crypto::Keys::EncryptedPrivateKeyInfo encryptedInfo {
            .encryptionAlgorithm = NGIN::Crypto::Keys::EncryptedPrivateKeyAlgorithmIdentifier {
                    .objectIdentifier = Oid({1, 2, 840, 113549, 1, 5, 13}),
                    .parameters       = Pbes2Pbkdf2Sha256Aes256GcmParameters(salt, 4096, nonce),
                    .hasParameters    = true,
            },
            .encryptedData = encryptedData,
    };

    auto decrypted = NGIN::Crypto::Keys::DecryptEncryptedPrivateKeyInfo(
            context.value(),
            encryptedInfo,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {password.data(), password.Size()}});
    REQUIRE(decrypted.has_value());
    REQUIRE(decrypted.value().algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    RequireBytesEqual(decrypted.value().privateKey, privateKey);

    encryptedInfo.encryptedData[encryptedInfo.encryptedData.Size() - 1] =
            static_cast<NGIN::Byte>(std::to_integer<NGIN::UInt8>(encryptedInfo.encryptedData[encryptedInfo.encryptedData.Size() - 1]) ^ 0x01u);
    auto tampered = NGIN::Crypto::Keys::DecryptEncryptedPrivateKeyInfo(
            context.value(),
            encryptedInfo,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {password.data(), password.Size()}});
    REQUIRE_FALSE(tampered.has_value());
    REQUIRE(tampered.error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
}

TEST_CASE("EncryptedPrivateKeyInfo decryption enforces explicit PBES2 password policy", "[Crypto][KeyFormat]")
{
    const auto salt          = Bytes({0x01, 0x02, 0x03, 0x04});
    const auto nonce         = Bytes({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b});
    auto       encryptedData = RepeatedByte(0x00, 16);

    NGIN::Crypto::Keys::EncryptedPrivateKeyInfo encryptedInfo {
            .encryptionAlgorithm = NGIN::Crypto::Keys::EncryptedPrivateKeyAlgorithmIdentifier {
                    .objectIdentifier = Oid({1, 2, 840, 113549, 1, 5, 13}),
                    .parameters       = Pbes2Pbkdf2Sha256Aes256GcmParameters(salt, 2, nonce),
                    .hasParameters    = true,
            },
            .encryptedData = encryptedData,
    };

    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.has_value());

    const auto password = Bytes({0x70, 0x61, 0x73, 0x73});
    auto       rejected = NGIN::Crypto::Keys::DecryptEncryptedPrivateKeyInfo(
            context.value(),
            encryptedInfo,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {password.data(), password.Size()}},
            NGIN::Crypto::Keys::EncryptedPrivateKeyDecryptOptions {.minimumPbkdf2Iterations = 1000});
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE(rejected.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("EncryptedPrivateKeyInfo rejects malformed envelopes and invalid raw parameters", "[Crypto][KeyFormat]")
{
    const auto extraField  = Bytes({
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
    auto       parsedExtra = NGIN::Crypto::Keys::ParseEncryptedPrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {extraField.data(), extraField.Size()});
    REQUIRE_FALSE(parsedExtra.has_value());
    REQUIRE(parsedExtra.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

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
    REQUIRE_FALSE(written.has_value());
    REQUIRE(written.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    invalidIdentifier.hasParameters = false;
    written                         = NGIN::Crypto::Keys::WriteEncryptedPrivateKeyInfo(
            invalidIdentifier,
            NGIN::Crypto::ConstByteSpan {encryptedData.data(), encryptedData.Size()});
    REQUIRE_FALSE(written.has_value());
    REQUIRE(written.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
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
    REQUIRE_FALSE(invalidSpki.has_value());
    REQUIRE(invalidSpki.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

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
    REQUIRE_FALSE(invalidPrivateKey.has_value());
    REQUIRE(invalidPrivateKey.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    auto unsupportedAgreement = NGIN::Crypto::Keys::ToKeyAgreementAlgorithm(NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE_FALSE(unsupportedAgreement.has_value());
    REQUIRE(unsupportedAgreement.error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);

    auto x25519PublicKey = RepeatedByte(0x44, 32);
    auto x25519Der       = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::X25519,
            NGIN::Crypto::ConstByteSpan {x25519PublicKey.data(), x25519PublicKey.Size()});
    REQUIRE(x25519Der.has_value());

    auto x25519Spki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {x25519Der.value().data(), x25519Der.value().Size()});
    REQUIRE(x25519Spki.has_value());
    auto mismatchedPublicKey = NGIN::Crypto::Keys::ImportEd25519PublicKey(x25519Spki.value());
    REQUIRE_FALSE(mismatchedPublicKey.has_value());
    REQUIRE(mismatchedPublicKey.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    auto compressedEcdsaPublicKey = RepeatedByte(0x33, 65);
    compressedEcdsaPublicKey[0]   = NGIN::Byte {0x03};
    auto compressedEcdsaDer       = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {compressedEcdsaPublicKey.data(), compressedEcdsaPublicKey.Size()});
    REQUIRE(compressedEcdsaDer.has_value());
    auto compressedEcdsaSpki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(
            NGIN::Crypto::ConstByteSpan {compressedEcdsaDer.value().data(), compressedEcdsaDer.value().Size()});
    REQUIRE(compressedEcdsaSpki.has_value());
    auto rejectedCompressedEcdsa = NGIN::Crypto::Keys::ImportEcdsaP256PublicKey(compressedEcdsaSpki.value());
    REQUIRE_FALSE(rejectedCompressedEcdsa.has_value());
    REQUIRE(rejectedCompressedEcdsa.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    auto shortEcdsaPrivateKey = RepeatedByte(0x55, 31);
    auto shortEcdsaDer        = NGIN::Crypto::Keys::WritePrivateKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::EcdsaP256,
            NGIN::Crypto::ConstByteSpan {shortEcdsaPrivateKey.data(), shortEcdsaPrivateKey.Size()});
    REQUIRE(shortEcdsaDer.has_value());
    auto shortEcdsaPrivateKeyInfo = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
            NGIN::Crypto::ConstByteSpan {shortEcdsaDer.value().data(), shortEcdsaDer.value().Size()});
    REQUIRE(shortEcdsaPrivateKeyInfo.has_value());
    auto rejectedShortEcdsaPrivateKey = NGIN::Crypto::Keys::ImportEcdsaP256PrivateKey(shortEcdsaPrivateKeyInfo.value());
    REQUIRE_FALSE(rejectedShortEcdsaPrivateKey.has_value());
    REQUIRE(rejectedShortEcdsaPrivateKey.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);
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
        REQUIRE_FALSE(spki.has_value());
        REQUIRE(spki.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

        auto privateKey = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
                NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()});
        REQUIRE_FALSE(privateKey.has_value());
        REQUIRE(privateKey.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}
