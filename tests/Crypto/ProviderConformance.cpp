#include "ProviderVectors/AeadVectors.hpp"
#include "ProviderVectors/HashVectors.hpp"
#include "ProviderVectors/HmacVectors.hpp"
#include "ProviderVectors/KdfVectors.hpp"
#include "ProviderVectors/KeyAgreementVectors.hpp"
#include "ProviderVectors/SignatureVectors.hpp"

#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/Rsa.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Hashing/Hash.hpp>
#include <NGIN/Crypto/Kdf/Argon2id.hpp>
#include <NGIN/Crypto/Kdf/Hkdf.hpp>
#include <NGIN/Crypto/Kdf/Pbkdf2.hpp>
#include <NGIN/Crypto/Mac/Mac.hpp>
#include <NGIN/Crypto/Random/RandomBytes.hpp>
#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace
{
    struct ProviderCase
    {
        std::string_view name;
        NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> (*create)() noexcept;
    };

    [[nodiscard]] NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> CreateBest() noexcept
    {
        return NGIN::Crypto::Backend::CreateBestAvailableContext();
    }

    [[nodiscard]] NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> CreatePlatform() noexcept
    {
        return NGIN::Crypto::Backend::CreatePlatformContext();
    }

    [[nodiscard]] NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> CreateOpenSslPackage() noexcept
    {
        return NGIN::Crypto::Backend::CreatePackageContext("openssl");
    }

    [[nodiscard]] NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> CreateBoringSslPackage() noexcept
    {
        return NGIN::Crypto::Backend::CreatePackageContext("boringssl");
    }

    [[nodiscard]] NGIN::Crypto::CryptoExpected<NGIN::Crypto::Backend::CryptoContext> CreateLibsodiumPackage() noexcept
    {
        return NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DecodeHex(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        auto decoded = NGIN::Crypto::Encoding::DecodeHex(text);
        REQUIRE(decoded.HasValue());
        return decoded.Value();
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] NGIN::Crypto::FixedBytes<Size> DecodeFixedHex(std::string_view text)
    {
        auto decoded = DecodeHex(text);
        REQUIRE(decoded.Size() == Size);

        NGIN::Crypto::FixedBytes<Size> output {};
        std::copy(decoded.begin(), decoded.end(), output.begin());
        return output;
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char> {text.data(), text.size()});
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(const NGIN::Crypto::ByteBuffer& bytes) noexcept
    {
        return NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()};
    }

    [[nodiscard]] NGIN::Crypto::ByteSpan MutableBytes(NGIN::Crypto::ByteBuffer& bytes) noexcept
    {
        return NGIN::Crypto::ByteSpan {bytes.data(), bytes.Size()};
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr NGIN::Crypto::FixedBytes<Size> ZeroBytes() noexcept
    {
        return {};
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] NGIN::Crypto::Memory::FixedSecret<Size> ZeroSecret()
    {
        return NGIN::Crypto::Memory::FixedSecret<Size>::FromValue(ZeroBytes<Size>());
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        REQUIRE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
    }

    void RequireUnsupportedHash(const NGIN::Crypto::Backend::CryptoContext& context, NGIN::Crypto::HashAlgorithm algorithm)
    {
        if (context.Supports(algorithm))
        {
            return;
        }

        constexpr std::string_view MESSAGE {"unsupported"};
        auto                       result = NGIN::Crypto::Hashing::Hash(context, algorithm, Bytes(MESSAGE));
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }

    void RequireUnsupportedMac(const NGIN::Crypto::Backend::CryptoContext& context, NGIN::Crypto::MacAlgorithm algorithm)
    {
        if (context.Supports(algorithm))
        {
            return;
        }

        auto key    = DecodeHex(NGIN::Crypto::Tests::ProviderVectors::RFC_4231_TEST_CASE_1_KEY_HEX);
        auto result = NGIN::Crypto::Mac::ComputeMac(
                context,
                algorithm,
                NGIN::Crypto::Memory::SecretView {Bytes(key)},
                Bytes("unsupported"));
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }

    void RequireUnsupportedKdf(const NGIN::Crypto::Backend::CryptoContext& context, NGIN::Crypto::KdfAlgorithm algorithm)
    {
        if (context.Supports(algorithm))
        {
            return;
        }

        auto secret = DecodeHex("01020304");
        auto salt   = DecodeHex("0a0b0c0d");

        NGIN::Crypto::Kdf::HkdfParameters hkdf {
                .inputKeyMaterial = NGIN::Crypto::Memory::SecretView {Bytes(secret)},
                .salt             = Bytes(salt),
                .info             = NGIN::Crypto::ConstByteSpan {},
        };
        NGIN::Crypto::Kdf::Pbkdf2Parameters pbkdf2 {
                .password   = NGIN::Crypto::Memory::SecretView {Bytes(secret)},
                .salt       = Bytes(salt),
                .iterations = 2,
        };
        NGIN::Crypto::Kdf::Argon2idParameters argon2id {
                .password    = NGIN::Crypto::Memory::SecretView {Bytes(secret)},
                .salt        = Bytes(salt),
                .memoryKiB   = 32,
                .iterations  = 1,
                .parallelism = 1,
        };

        switch (algorithm)
        {
            case NGIN::Crypto::KdfAlgorithm::HkdfSha256:
            case NGIN::Crypto::KdfAlgorithm::HkdfSha512: {
                auto result = NGIN::Crypto::Kdf::DeriveKey(
                        context,
                        NGIN::Crypto::Kdf::KeyDerivationParameters {algorithm, hkdf},
                        32);
                REQUIRE_FALSE(result.HasValue());
                REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
                return;
            }
            case NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256:
            case NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512: {
                auto result = NGIN::Crypto::Kdf::DeriveKey(
                        context,
                        NGIN::Crypto::Kdf::KeyDerivationParameters {algorithm, pbkdf2},
                        32);
                REQUIRE_FALSE(result.HasValue());
                REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
                return;
            }
            case NGIN::Crypto::KdfAlgorithm::Argon2id: {
                auto result = NGIN::Crypto::Kdf::DeriveKey(
                        context,
                        NGIN::Crypto::Kdf::KeyDerivationParameters {argon2id},
                        32);
                REQUIRE_FALSE(result.HasValue());
                REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
                return;
            }
        }
    }

    void RequireUnsupportedAead(const NGIN::Crypto::Backend::CryptoContext& context, NGIN::Crypto::AeadAlgorithm algorithm)
    {
        if (context.Supports(algorithm))
        {
            return;
        }

        auto keyBytes = NGIN::Crypto::Symmetric::AeadKeySize(algorithm) == 16 ? DecodeHex("00000000000000000000000000000000")
                                                                              : DecodeHex("0000000000000000000000000000000000000000000000000000000000000000");
        auto nonce    = NGIN::Crypto::Symmetric::AeadNonceSize(algorithm) == 24
                                ? DecodeHex("000000000000000000000000000000000000000000000000")
                                : DecodeHex("000000000000000000000000");
        auto plain    = DecodeHex("00000000000000000000000000000000");
        auto cipher   = NGIN::Crypto::MakeByteBuffer(plain.Size());
        auto tag      = NGIN::Crypto::MakeByteBuffer(NGIN::Crypto::Symmetric::AeadTagSize(algorithm));

        NGIN::Crypto::Symmetric::AeadSealInput input {
                .key            = NGIN::Crypto::Memory::SecretView {Bytes(keyBytes)},
                .nonce          = Bytes(nonce),
                .plaintext      = Bytes(plain),
                .associatedData = NGIN::Crypto::ConstByteSpan {},
        };

        auto result = NGIN::Crypto::Symmetric::SealInto(
                context,
                algorithm,
                input,
                MutableBytes(cipher),
                MutableBytes(tag));
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }

    void RunRandomConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        if (!context.SupportsRandom())
        {
            return;
        }

        auto bytes = NGIN::Crypto::MakeByteBuffer(32);
        auto fill  = context.FillRandom(MutableBytes(bytes));
        REQUIRE(fill.HasValue());
    }

    void RunHashConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::HASH_VECTORS)
        {
            if (!context.Supports(vector.algorithm))
            {
                continue;
            }

            auto digest   = NGIN::Crypto::Hashing::Hash(context, vector.algorithm, Bytes(vector.message));
            auto expected = DecodeHex(vector.expectedHex);

            REQUIRE(digest.HasValue());
            RequireBytesEqual(Bytes(digest.Value()), Bytes(expected));
        }

        RequireUnsupportedHash(context, NGIN::Crypto::HashAlgorithm::Sha3_256);
        RequireUnsupportedHash(context, NGIN::Crypto::HashAlgorithm::Sha3_512);
        RequireUnsupportedHash(context, NGIN::Crypto::HashAlgorithm::Blake3);
    }

    void RunMacConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::HMAC_VECTORS)
        {
            if (!context.Supports(vector.algorithm))
            {
                continue;
            }

            auto key = DecodeHex(vector.keyHex);
            auto mac = NGIN::Crypto::Mac::ComputeMac(
                    context,
                    vector.algorithm,
                    NGIN::Crypto::Memory::SecretView {Bytes(key)},
                    Bytes(vector.message));
            auto expected = DecodeHex(vector.expectedHex);

            REQUIRE(mac.HasValue());
            RequireBytesEqual(Bytes(mac.Value()), Bytes(expected));

            auto verified = NGIN::Crypto::Mac::VerifyMac(
                    context,
                    vector.algorithm,
                    NGIN::Crypto::Memory::SecretView {Bytes(key)},
                    Bytes(vector.message),
                    Bytes(expected));
            REQUIRE(verified.HasValue());
        }

        RequireUnsupportedMac(context, NGIN::Crypto::MacAlgorithm::HmacSha256);
        RequireUnsupportedMac(context, NGIN::Crypto::MacAlgorithm::HmacSha512);
    }

    void RunKdfConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::HKDF_VECTORS)
        {
            if (!context.Supports(vector.algorithm))
            {
                continue;
            }

            auto inputKeyMaterial = DecodeHex(vector.inputKeyMaterialHex);
            auto salt             = DecodeHex(vector.saltHex);
            auto info             = DecodeHex(vector.infoHex);
            auto expected         = DecodeHex(vector.expectedHex);

            NGIN::Crypto::Kdf::HkdfParameters hkdf {
                    .inputKeyMaterial = NGIN::Crypto::Memory::SecretView {Bytes(inputKeyMaterial)},
                    .salt             = Bytes(salt),
                    .info             = Bytes(info),
            };

            auto output = NGIN::Crypto::Kdf::DeriveKey(
                    context,
                    NGIN::Crypto::Kdf::KeyDerivationParameters {vector.algorithm, hkdf},
                    expected.Size());
            REQUIRE(output.HasValue());
            RequireBytesEqual(Bytes(output.Value()), Bytes(expected));
        }

        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::PBKDF2_VECTORS)
        {
            if (!context.Supports(vector.algorithm))
            {
                continue;
            }

            auto                                expected = DecodeHex(vector.expectedHex);
            NGIN::Crypto::Kdf::Pbkdf2Parameters pbkdf2 {
                    .password   = NGIN::Crypto::Memory::SecretView {Bytes(vector.password)},
                    .salt       = Bytes(vector.salt),
                    .iterations = vector.iterations,
            };

            auto output = NGIN::Crypto::Kdf::DeriveKey(
                    context,
                    NGIN::Crypto::Kdf::KeyDerivationParameters {vector.algorithm, pbkdf2},
                    expected.Size());
            REQUIRE(output.HasValue());
            RequireBytesEqual(Bytes(output.Value()), Bytes(expected));
        }

        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::ARGON2ID_VECTORS)
        {
            if (!context.Supports(NGIN::Crypto::KdfAlgorithm::Argon2id))
            {
                continue;
            }

            auto salt     = DecodeHex(vector.saltHex);
            auto expected = DecodeHex(vector.expectedHex);

            NGIN::Crypto::Kdf::Argon2idParameters argon2id {
                    .password    = NGIN::Crypto::Memory::SecretView {Bytes(vector.password)},
                    .salt        = Bytes(salt),
                    .memoryKiB   = vector.memoryKiB,
                    .iterations  = vector.iterations,
                    .parallelism = vector.parallelism,
            };

            auto output = NGIN::Crypto::Kdf::DeriveKey(
                    context,
                    NGIN::Crypto::Kdf::KeyDerivationParameters {argon2id},
                    expected.Size());
            REQUIRE(output.HasValue());
            RequireBytesEqual(Bytes(output.Value()), Bytes(expected));
        }

        RequireUnsupportedKdf(context, NGIN::Crypto::KdfAlgorithm::HkdfSha256);
        RequireUnsupportedKdf(context, NGIN::Crypto::KdfAlgorithm::HkdfSha512);
        RequireUnsupportedKdf(context, NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256);
        RequireUnsupportedKdf(context, NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512);
        RequireUnsupportedKdf(context, NGIN::Crypto::KdfAlgorithm::Argon2id);
    }

    void RunAeadConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        for (const auto& vector: NGIN::Crypto::Tests::ProviderVectors::AEAD_SEAL_VECTORS)
        {
            if (!context.Supports(vector.algorithm))
            {
                continue;
            }

            auto key            = DecodeHex(vector.keyHex);
            auto nonce          = DecodeHex(vector.nonceHex);
            auto plaintext      = DecodeHex(vector.plaintextHex);
            auto associatedData = DecodeHex(vector.associatedDataHex);
            auto expectedCipher = DecodeHex(vector.ciphertextHex);
            auto expectedTag    = DecodeHex(vector.tagHex);

            NGIN::Crypto::Symmetric::AeadSealInput sealInput {
                    .key            = NGIN::Crypto::Memory::SecretView {Bytes(key)},
                    .nonce          = Bytes(nonce),
                    .plaintext      = Bytes(plaintext),
                    .associatedData = Bytes(associatedData),
            };

            auto sealed = NGIN::Crypto::Symmetric::Seal(context, vector.algorithm, sealInput);
            REQUIRE(sealed.HasValue());
            RequireBytesEqual(Bytes(sealed.Value().ciphertext), Bytes(expectedCipher));
            RequireBytesEqual(
                    NGIN::Crypto::ConstByteSpan {sealed.Value().tag.data(), sealed.Value().tag.size()},
                    Bytes(expectedTag));

            NGIN::Crypto::Symmetric::AeadOpenInput openInput {
                    .key            = NGIN::Crypto::Memory::SecretView {Bytes(key)},
                    .nonce          = Bytes(nonce),
                    .ciphertext     = Bytes(sealed.Value().ciphertext),
                    .associatedData = Bytes(associatedData),
                    .tag            = NGIN::Crypto::ConstByteSpan {sealed.Value().tag.data(), sealed.Value().tag.size()},
            };

            auto opened = NGIN::Crypto::Symmetric::Open(context, vector.algorithm, openInput);
            REQUIRE(opened.HasValue());
            RequireBytesEqual(Bytes(opened.Value()), Bytes(plaintext));

            auto badTag = sealed.Value().tag;
            badTag[0] ^= NGIN::Byte {0x01};
            openInput.tag = NGIN::Crypto::ConstByteSpan {badTag.data(), badTag.size()};

            auto rejected = NGIN::Crypto::Symmetric::Open(context, vector.algorithm, openInput);
            REQUIRE_FALSE(rejected.HasValue());
            REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
        }

        RequireUnsupportedAead(context, NGIN::Crypto::AeadAlgorithm::Aes128Gcm);
        RequireUnsupportedAead(context, NGIN::Crypto::AeadAlgorithm::Aes256Gcm);
        RequireUnsupportedAead(context, NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305);
        RequireUnsupportedAead(context, NGIN::Crypto::AeadAlgorithm::XChaCha20Poly1305);
    }

    void RunSignatureConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        const auto& ed25519Vector  = NGIN::Crypto::Tests::ProviderVectors::ED25519_RFC_8032_TEST_1;
        auto        ed25519Message = DecodeHex(ed25519Vector.messageHex);

        if (!context.Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519))
        {
            auto privateKey =
                    NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromBytes(DecodeFixedHex<32>(ed25519Vector.privateKeyHex));
            auto result = NGIN::Crypto::Asymmetric::SignEd25519(context, privateKey, Bytes(ed25519Message));
            REQUIRE_FALSE(result.HasValue());
            REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
        }
        else
        {
            auto privateKey =
                    NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromBytes(DecodeFixedHex<32>(ed25519Vector.privateKeyHex));
            auto publicKey =
                    NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(DecodeFixedHex<32>(ed25519Vector.publicKeyHex));
            auto expectedSignature = DecodeFixedHex<64>(ed25519Vector.signatureHex);

            auto signature = NGIN::Crypto::Asymmetric::SignEd25519(context, privateKey, Bytes(ed25519Message));
            REQUIRE(signature.HasValue());
            RequireBytesEqual(signature.Value(), expectedSignature);

            auto verified =
                    NGIN::Crypto::Asymmetric::VerifyEd25519(context, publicKey, Bytes(ed25519Message), expectedSignature);
            REQUIRE(verified.HasValue());

            auto tamperedSignature = expectedSignature;
            tamperedSignature[0] ^= NGIN::Byte {0x01};
            auto rejected =
                    NGIN::Crypto::Asymmetric::VerifyEd25519(context, publicKey, Bytes(ed25519Message), tamperedSignature);
            REQUIRE_FALSE(rejected.HasValue());
            REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
        }

        const auto& ecdsaVector     = NGIN::Crypto::Tests::ProviderVectors::ECDSA_P256_SHA256_REGRESSION;
        auto        ecdsaPrivateKey = DecodeHex(ecdsaVector.privateKeyHex);
        auto        ecdsaPublicKey  = DecodeHex(ecdsaVector.publicKeyHex);
        auto        ecdsaMessage    = DecodeHex(ecdsaVector.messageHex);
        auto        ecdsaSignature  = DecodeHex(ecdsaVector.signatureHex);

        NGIN::Crypto::Signatures::SignInput ecdsaSignInput {
                .privateKey = NGIN::Crypto::Memory::SecretView {Bytes(ecdsaPrivateKey)},
                .message    = Bytes(ecdsaMessage),
        };
        NGIN::Crypto::Signatures::VerifyInput ecdsaVerifyInput {
                .publicKey = Bytes(ecdsaPublicKey),
                .message   = Bytes(ecdsaMessage),
                .signature = Bytes(ecdsaSignature),
        };

        if (!context.Supports(NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256))
        {
            auto result = NGIN::Crypto::Signatures::Sign(
                    context,
                    NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
                    ecdsaSignInput);
            REQUIRE_FALSE(result.HasValue());
            REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
        }
        else
        {
            auto verified = NGIN::Crypto::Signatures::Verify(
                    context,
                    NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
                    ecdsaVerifyInput);
            REQUIRE(verified.HasValue());

            auto signedMessage = NGIN::Crypto::Signatures::Sign(
                    context,
                    NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
                    ecdsaSignInput);
            REQUIRE(signedMessage.HasValue());
            REQUIRE(signedMessage.Value().Size() == 64);

            ecdsaVerifyInput.signature = Bytes(signedMessage.Value());
            auto generatedVerified     = NGIN::Crypto::Signatures::Verify(
                    context,
                    NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
                    ecdsaVerifyInput);
            REQUIRE(generatedVerified.HasValue());

            auto tamperedSignature = ecdsaSignature;
            tamperedSignature[0] ^= NGIN::Byte {0x01};
            ecdsaVerifyInput.signature = Bytes(tamperedSignature);
            auto rejected              = NGIN::Crypto::Signatures::Verify(
                    context,
                    NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256,
                    ecdsaVerifyInput);
            REQUIRE_FALSE(rejected.HasValue());
            REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
        }

        const auto& rsaVector        = NGIN::Crypto::Tests::ProviderVectors::RSA_PSS_SHA256_REGRESSION;
        auto        rsaPrivateKeyDer = DecodeHex(rsaVector.privateKeyDerHex);
        auto        rsaPublicKeyDer  = DecodeHex(rsaVector.publicKeyDerHex);
        auto        rsaMessage       = DecodeHex(rsaVector.messageHex);
        auto        rsaSignature     = DecodeHex(rsaVector.signatureHex);

        NGIN::Crypto::Asymmetric::RsaPssSha256SignInput rsaSignInput {
                .privateKeyDer = NGIN::Crypto::Memory::SecretView {Bytes(rsaPrivateKeyDer)},
                .message       = Bytes(rsaMessage),
        };
        NGIN::Crypto::Asymmetric::RsaPssSha256VerifyInput rsaVerifyInput {
                .publicKeyDer = Bytes(rsaPublicKeyDer),
                .message      = Bytes(rsaMessage),
                .signature    = Bytes(rsaSignature),
        };

        if (!context.Supports(NGIN::Crypto::SignatureAlgorithm::RsaPssSha256))
        {
            auto result = NGIN::Crypto::Asymmetric::SignRsaPssSha256(context, rsaSignInput);
            REQUIRE_FALSE(result.HasValue());
            REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
        }
        else
        {
            auto verified = NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(context, rsaVerifyInput);
            REQUIRE(verified.HasValue());

            auto generated = NGIN::Crypto::Asymmetric::SignRsaPssSha256(context, rsaSignInput);
            REQUIRE(generated.HasValue());
            REQUIRE(generated.Value().Size() == rsaSignature.Size());

            rsaVerifyInput.signature = Bytes(generated.Value());
            auto generatedVerified   = NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(context, rsaVerifyInput);
            REQUIRE(generatedVerified.HasValue());

            auto tamperedSignature = generated.Value();
            tamperedSignature[0] ^= NGIN::Byte {0x01};
            rsaVerifyInput.signature = Bytes(tamperedSignature);
            auto rejected            = NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(context, rsaVerifyInput);
            REQUIRE_FALSE(rejected.HasValue());
            REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
        }

        NGIN::Crypto::Asymmetric::RsaOaepSha256EncryptInput rsaEncryptInput {
                .publicKeyDer = Bytes(rsaPublicKeyDer),
                .plaintext    = Bytes(rsaMessage),
                .label        = Bytes("rsa-oaep-label"),
        };

        if (!context.Supports(NGIN::Crypto::AsymmetricEncryptionAlgorithm::RsaOaepSha256))
        {
            auto result = NGIN::Crypto::Asymmetric::EncryptRsaOaepSha256(context, rsaEncryptInput);
            REQUIRE_FALSE(result.HasValue());
            REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
        }
        else
        {
            auto ciphertext = NGIN::Crypto::Asymmetric::EncryptRsaOaepSha256(context, rsaEncryptInput);
            REQUIRE(ciphertext.HasValue());
            REQUIRE(ciphertext.Value().Size() == rsaSignature.Size());

            NGIN::Crypto::Asymmetric::RsaOaepSha256DecryptInput rsaDecryptInput {
                    .privateKeyDer = NGIN::Crypto::Memory::SecretView {Bytes(rsaPrivateKeyDer)},
                    .ciphertext    = Bytes(ciphertext.Value()),
                    .label         = rsaEncryptInput.label,
            };
            auto plaintext = NGIN::Crypto::Asymmetric::DecryptRsaOaepSha256(context, rsaDecryptInput);
            REQUIRE(plaintext.HasValue());
            RequireBytesEqual(Bytes(plaintext.Value()), Bytes(rsaMessage));

            rsaDecryptInput.label = Bytes("wrong-label");
            auto rejected         = NGIN::Crypto::Asymmetric::DecryptRsaOaepSha256(context, rsaDecryptInput);
            REQUIRE_FALSE(rejected.HasValue());
            REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
        }
    }

    void RunKeyAgreementConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        const auto& vector = NGIN::Crypto::Tests::ProviderVectors::X25519_RFC_7748_TEST_1;

        auto privateKey    = NGIN::Crypto::Asymmetric::X25519PrivateKey::FromBytes(DecodeFixedHex<32>(vector.privateKeyHex));
        auto peerPublicKey = NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(DecodeFixedHex<32>(vector.peerPublicKeyHex));

        if (!context.Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519))
        {
            auto result = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(context, privateKey, peerPublicKey);
            REQUIRE_FALSE(result.HasValue());
            REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
            return;
        }

        auto expectedSharedSecret = DecodeFixedHex<32>(vector.sharedSecretHex);
        auto sharedSecret         = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(context, privateKey, peerPublicKey);

        REQUIRE(sharedSecret.HasValue());
        RequireBytesEqual(sharedSecret.Value().Bytes(), expectedSharedSecret);
    }

    void RunOutputBufferConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        std::array<NGIN::Byte, 31> hashOutput {};
        auto                       hash = NGIN::Crypto::Hashing::HashInto(
                context,
                NGIN::Crypto::HashAlgorithm::Sha256,
                NGIN::Crypto::ConstByteSpan {},
                hashOutput);
        REQUIRE_FALSE(hash.HasValue());
        REQUIRE(hash.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);

        auto key = ZeroSecret<32>();
        auto mac = NGIN::Crypto::Mac::MacInto(
                context,
                NGIN::Crypto::MacAlgorithm::HmacSha256,
                NGIN::Crypto::Memory::SecretView {key.Bytes()},
                NGIN::Crypto::ConstByteSpan {},
                hashOutput);
        REQUIRE_FALSE(mac.HasValue());
        REQUIRE(mac.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);

        auto nonce     = ZeroBytes<12>();
        auto plaintext = ZeroBytes<16>();
        auto tag       = ZeroBytes<16>();
        auto tooSmall  = ZeroBytes<15>();

        NGIN::Crypto::Symmetric::AeadSealInput input {
                .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                .nonce          = nonce,
                .plaintext      = plaintext,
                .associatedData = NGIN::Crypto::ConstByteSpan {},
        };

        auto seal = NGIN::Crypto::Symmetric::SealInto(
                context,
                NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
                input,
                tooSmall,
                tag);
        REQUIRE_FALSE(seal.HasValue());
        REQUIRE(seal.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
    }

    void RunProviderConformance(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        INFO("provider=" << context.Info().Name());
        RunRandomConformance(context);
        RunHashConformance(context);
        RunMacConformance(context);
        RunKdfConformance(context);
        RunAeadConformance(context);
        RunSignatureConformance(context);
        RunKeyAgreementConformance(context);
        RunOutputBufferConformance(context);
    }
}// namespace

TEST_CASE("Configured providers satisfy crypto conformance vectors", "[Crypto][ProviderConformance]")
{
    constexpr std::array PROVIDERS {
            ProviderCase {.name = "best", .create = CreateBest},
            ProviderCase {.name = "platform", .create = CreatePlatform},
            ProviderCase {.name = "openssl-package", .create = CreateOpenSslPackage},
            ProviderCase {.name = "boringssl-package", .create = CreateBoringSslPackage},
            ProviderCase {.name = "libsodium-package", .create = CreateLibsodiumPackage},
    };

    for (const auto& provider: PROVIDERS)
    {
        DYNAMIC_SECTION("provider " << provider.name)
        {
            auto context = provider.create();
            if (!context.HasValue())
            {
                REQUIRE(
                        (context.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable ||
                         context.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedBackend));
                return;
            }

            RunProviderConformance(context.Value());
        }
    }
}
