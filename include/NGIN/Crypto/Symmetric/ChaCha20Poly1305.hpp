#pragma once

#include <NGIN/Crypto/Random/RandomBytes.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

namespace NGIN::Crypto::Symmetric
{
    using ChaCha20Poly1305Key   = NGIN::Crypto::Memory::FixedSecret<32>;
    using ChaCha20Poly1305Nonce = FixedBytes<12>;
    using ChaCha20Poly1305Tag   = StandardAeadTag;

    /// @brief Generates a random ChaCha20-Poly1305 key using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<ChaCha20Poly1305Key> GenerateChaCha20Poly1305Key() noexcept
    {
        return ChaCha20Poly1305Key::Generate();
    }

    /// @brief Generates a random ChaCha20-Poly1305 nonce using the platform secure random source.
    [[nodiscard]] inline CryptoExpected<ChaCha20Poly1305Nonce> GenerateChaCha20Poly1305Nonce() noexcept
    {
        return NGIN::Crypto::Random::RandomBytes<12>();
    }

    /// @brief Encrypts and authenticates with ChaCha20-Poly1305 using typed key and nonce storage.
    [[nodiscard]] inline CryptoExpected<AeadSealResult> SealChaCha20Poly1305(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const ChaCha20Poly1305Key&                  key,
            const ChaCha20Poly1305Nonce&                nonce,
            ConstByteSpan                               plaintext,
            ConstByteSpan                               associatedData = {})
    {
        return Seal(
                context,
                AeadAlgorithm::ChaCha20Poly1305,
                AeadSealInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .plaintext      = plaintext,
                        .associatedData = associatedData,
                });
    }

    /// @brief Authenticates and decrypts with ChaCha20-Poly1305 using typed key, nonce, and tag storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> OpenChaCha20Poly1305(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const ChaCha20Poly1305Key&                  key,
            const ChaCha20Poly1305Nonce&                nonce,
            ConstByteSpan                               ciphertext,
            const ChaCha20Poly1305Tag&                  tag,
            ConstByteSpan                               associatedData = {})
    {
        return Open(
                context,
                AeadAlgorithm::ChaCha20Poly1305,
                AeadOpenInput {
                        .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
                        .nonce          = nonce,
                        .ciphertext     = ciphertext,
                        .associatedData = associatedData,
                        .tag            = tag,
                });
    }
}// namespace NGIN::Crypto::Symmetric
