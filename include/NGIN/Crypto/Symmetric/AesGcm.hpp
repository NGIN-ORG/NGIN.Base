#pragma once

#include <NGIN/Crypto/Random/RandomBytes.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

namespace NGIN::Crypto::Symmetric
{
    using Aes128GcmKey = NGIN::Crypto::Memory::FixedSecret<16>;
    using Aes256GcmKey = NGIN::Crypto::Memory::FixedSecret<32>;
    using AesGcmNonce  = FixedBytes<12>;
    using AesGcmTag    = StandardAeadTag;

    /// @brief Generates a random AES-128-GCM key using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<Aes128GcmKey> GenerateAes128GcmKey() noexcept
    {
        return Aes128GcmKey::Generate();
    }

    /// @brief Generates a random AES-256-GCM key using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<Aes256GcmKey> GenerateAes256GcmKey() noexcept
    {
        return Aes256GcmKey::Generate();
    }

    /// @brief Generates a random AES-GCM nonce using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<AesGcmNonce> GenerateAesGcmNonce() noexcept
    {
        return NGIN::Crypto::Random::RandomBytes<12>();
    }

    /// @brief Encrypts and authenticates with AES-128-GCM using typed key and nonce storage.
    [[nodiscard]] inline CryptoExpected<AeadSealResult> SealAes128Gcm(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Aes128GcmKey&                         key,
            const AesGcmNonce&                          nonce,
            ConstByteSpan                               plaintext,
            ConstByteSpan                               associatedData = {})
    {
        return Seal(
                context,
                AeadAlgorithm::Aes128Gcm,
                AeadSealInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .plaintext      = plaintext,
                        .associatedData = associatedData,
                });
    }

    /// @brief Authenticates and decrypts with AES-128-GCM using typed key, nonce, and tag storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> OpenAes128Gcm(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Aes128GcmKey&                         key,
            const AesGcmNonce&                          nonce,
            ConstByteSpan                               ciphertext,
            const AesGcmTag&                            tag,
            ConstByteSpan                               associatedData = {})
    {
        return Open(
                context,
                AeadAlgorithm::Aes128Gcm,
                AeadOpenInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .ciphertext     = ciphertext,
                        .associatedData = associatedData,
                        .tag            = tag,
                });
    }

    /// @brief Encrypts and authenticates with AES-256-GCM using typed key and nonce storage.
    [[nodiscard]] inline CryptoExpected<AeadSealResult> SealAes256Gcm(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Aes256GcmKey&                         key,
            const AesGcmNonce&                          nonce,
            ConstByteSpan                               plaintext,
            ConstByteSpan                               associatedData = {})
    {
        return Seal(
                context,
                AeadAlgorithm::Aes256Gcm,
                AeadSealInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .plaintext      = plaintext,
                        .associatedData = associatedData,
                });
    }

    /// @brief Authenticates and decrypts with AES-256-GCM using typed key, nonce, and tag storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> OpenAes256Gcm(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Aes256GcmKey&                         key,
            const AesGcmNonce&                          nonce,
            ConstByteSpan                               ciphertext,
            const AesGcmTag&                            tag,
            ConstByteSpan                               associatedData = {})
    {
        return Open(
                context,
                AeadAlgorithm::Aes256Gcm,
                AeadOpenInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .ciphertext     = ciphertext,
                        .associatedData = associatedData,
                        .tag            = tag,
                });
    }
}// namespace NGIN::Crypto::Symmetric
