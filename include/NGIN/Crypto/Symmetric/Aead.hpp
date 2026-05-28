#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Symmetric/AeadAlgorithm.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Symmetric
{
    /// @brief Fixed-size AEAD authentication tag bytes.
    template<NGIN::UIntSize Size>
    using AeadTag = FixedBytes<Size>;

    using StandardAeadTag = AeadTag<16>;

    struct AeadSizes
    {
        NGIN::UIntSize keySize {0};
        NGIN::UIntSize nonceSize {0};
        NGIN::UIntSize tagSize {0};
    };

    struct AeadSealInput
    {
        NGIN::Crypto::Memory::SecretView key {};
        ConstByteSpan                    nonce {};
        ConstByteSpan                    plaintext {};
        ConstByteSpan                    associatedData {};
    };

    struct AeadOpenInput
    {
        NGIN::Crypto::Memory::SecretView key {};
        ConstByteSpan                    nonce {};
        ConstByteSpan                    ciphertext {};
        ConstByteSpan                    associatedData {};
        ConstByteSpan                    tag {};
    };

    struct AeadSealResult
    {
        ByteBuffer      ciphertext {};
        StandardAeadTag tag {};
    };

    /// @brief Returns key, nonce, and tag sizes for an AEAD algorithm.
    [[nodiscard]] constexpr AeadSizes GetAeadSizes(AeadAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case AeadAlgorithm::Aes128Gcm:
                return AeadSizes {.keySize = 16, .nonceSize = 12, .tagSize = 16};
            case AeadAlgorithm::Aes256Gcm:
                return AeadSizes {.keySize = 32, .nonceSize = 12, .tagSize = 16};
            case AeadAlgorithm::ChaCha20Poly1305:
                return AeadSizes {.keySize = 32, .nonceSize = 12, .tagSize = 16};
            case AeadAlgorithm::XChaCha20Poly1305:
                return AeadSizes {.keySize = 32, .nonceSize = 24, .tagSize = 16};
        }

        return {};
    }

    [[nodiscard]] constexpr NGIN::UIntSize AeadKeySize(AeadAlgorithm algorithm) noexcept
    {
        return GetAeadSizes(algorithm).keySize;
    }

    [[nodiscard]] constexpr NGIN::UIntSize AeadNonceSize(AeadAlgorithm algorithm) noexcept
    {
        return GetAeadSizes(algorithm).nonceSize;
    }

    [[nodiscard]] constexpr NGIN::UIntSize AeadTagSize(AeadAlgorithm algorithm) noexcept
    {
        return GetAeadSizes(algorithm).tagSize;
    }

    /// @brief Encrypts and authenticates plaintext into caller-provided ciphertext and tag buffers.
    [[nodiscard]] CryptoExpected<void> SealInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadSealInput&                        input,
            ByteSpan                                    ciphertext,
            ByteSpan                                    tag) noexcept;

    /// @brief Authenticates and decrypts ciphertext into caller-provided plaintext storage.
    [[nodiscard]] CryptoExpected<void> OpenInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadOpenInput&                        input,
            ByteSpan                                    plaintext) noexcept;

    /// @brief Encrypts and authenticates plaintext into owned ciphertext and tag storage.
    [[nodiscard]] CryptoExpected<AeadSealResult> Seal(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadSealInput&                        input);

    /// @brief Authenticates and decrypts ciphertext into owned plaintext storage.
    [[nodiscard]] CryptoExpected<ByteBuffer> Open(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadOpenInput&                        input);
}// namespace NGIN::Crypto::Symmetric
