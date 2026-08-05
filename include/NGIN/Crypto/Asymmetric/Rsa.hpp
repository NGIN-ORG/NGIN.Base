/// @file Rsa.hpp
/// @brief RSA-PSS SHA-256 signing and RSA-OAEP SHA-256 encryption.
#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Asymmetric
{
    /// @brief RSA-PSS/SHA-256 signing input using DER-encoded PKCS#8 private keys.
    struct RsaPssSha256SignInput
    {
        NGIN::Crypto::Memory::SecretView privateKeyDer;
        ConstByteSpan                    message;
    };

    /// @brief RSA-PSS/SHA-256 verification input using DER-encoded SPKI public keys.
    struct RsaPssSha256VerifyInput
    {
        ConstByteSpan publicKeyDer;
        ConstByteSpan message;
        ConstByteSpan signature;
    };

    /// @brief RSA-OAEP/SHA-256 encryption input using DER-encoded SPKI public keys.
    struct RsaOaepSha256EncryptInput
    {
        ConstByteSpan publicKeyDer;
        ConstByteSpan plaintext;
        ConstByteSpan label;
    };

    /// @brief RSA-OAEP/SHA-256 decryption input using DER-encoded PKCS#8 private keys.
    struct RsaOaepSha256DecryptInput
    {
        NGIN::Crypto::Memory::SecretView privateKeyDer;
        ConstByteSpan                    ciphertext;
        ConstByteSpan                    label;
    };

    /// @brief Signs a message using RSA-PSS SHA-256 and a DER PKCS#8 private key.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> SignRsaPssSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaPssSha256SignInput&                input);

    /// @brief Verifies an RSA-PSS SHA-256 signature using a DER SPKI public key.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> VerifyRsaPssSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaPssSha256VerifyInput&              input) noexcept;

    /// @brief Encrypts plaintext using RSA-OAEP SHA-256 and a DER SPKI public key.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> EncryptRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaOaepSha256EncryptInput&            input);

    /// @brief Decrypts ciphertext using RSA-OAEP SHA-256 and a DER PKCS#8 private key.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> DecryptRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaOaepSha256DecryptInput&            input);
}// namespace NGIN::Crypto::Asymmetric
