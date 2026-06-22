#pragma once

#include <NGIN/Crypto/Random/RandomBytes.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

namespace NGIN::Crypto::Symmetric
{
    using XChaCha20Poly1305Key   = NGIN::Crypto::Memory::FixedSecret<32>;
    using XChaCha20Poly1305Nonce = FixedBytes<24>;
    using XChaCha20Poly1305Tag   = StandardAeadTag;

    /// @brief Generates a random XChaCha20-Poly1305 key using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<XChaCha20Poly1305Key> GenerateXChaCha20Poly1305Key() noexcept
    {
        return XChaCha20Poly1305Key::Generate();
    }

    /// @brief Generates a random XChaCha20-Poly1305 nonce using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<XChaCha20Poly1305Nonce> GenerateXChaCha20Poly1305Nonce() noexcept
    {
        return NGIN::Crypto::Random::RandomBytes<24>();
    }

    /// @brief Encrypts and authenticates with XChaCha20-Poly1305 using typed key and nonce storage.
    [[nodiscard]] inline CryptoExpected<AeadSealResult> SealXChaCha20Poly1305(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const XChaCha20Poly1305Key&                 key,
            const XChaCha20Poly1305Nonce&               nonce,
            ConstByteSpan                               plaintext,
            ConstByteSpan                               associatedData = {})
    {
        return Seal(
                context,
                AeadAlgorithm::XChaCha20Poly1305,
                AeadSealInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .plaintext      = plaintext,
                        .associatedData = associatedData,
                });
    }

    /// @brief Authenticates and decrypts with XChaCha20-Poly1305 using typed key, nonce, and tag storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> OpenXChaCha20Poly1305(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const XChaCha20Poly1305Key&                 key,
            const XChaCha20Poly1305Nonce&               nonce,
            ConstByteSpan                               ciphertext,
            const XChaCha20Poly1305Tag&                 tag,
            ConstByteSpan                               associatedData = {})
    {
        return Open(
                context,
                AeadAlgorithm::XChaCha20Poly1305,
                AeadOpenInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .ciphertext     = ciphertext,
                        .associatedData = associatedData,
                        .tag            = tag,
                });
    }
}// namespace NGIN::Crypto::Symmetric
