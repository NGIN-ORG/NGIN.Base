#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Hashing/Digest.hpp>
#include <NGIN/Crypto/Hashing/HashAlgorithm.hpp>

namespace NGIN::Crypto::Hashing
{
    /// @brief Returns the digest size for a hash algorithm in bytes.
    [[nodiscard]] constexpr NGIN::UIntSize DigestSize(HashAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case HashAlgorithm::Sha256:
            case HashAlgorithm::Sha3_256:
            case HashAlgorithm::Blake3:
                return 32;
            case HashAlgorithm::Sha512:
            case HashAlgorithm::Sha3_512:
                return 64;
        }

        return 0;
    }

    /// @brief Hashes input into a caller-provided digest buffer.
    [[nodiscard]] CryptoExpected<void> HashInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept;

    /// @brief Hashes input into an owned byte buffer.
    [[nodiscard]] CryptoExpected<ByteBuffer> Hash(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input);

    /// @brief Hashes input as SHA-256 into a caller-provided digest.
    [[nodiscard]] CryptoExpected<void> Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha256Digest&                               output) noexcept;

    /// @brief Hashes input as SHA-512 into a caller-provided digest.
    [[nodiscard]] CryptoExpected<void> Sha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha512Digest&                               output) noexcept;

    /// @brief Hashes input as SHA-256 into an owned fixed-size digest.
    [[nodiscard]] CryptoExpected<Sha256Digest> Sha256(const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input);

    /// @brief Hashes input as SHA-512 into an owned fixed-size digest.
    [[nodiscard]] CryptoExpected<Sha512Digest> Sha512(const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input);
}// namespace NGIN::Crypto::Hashing
