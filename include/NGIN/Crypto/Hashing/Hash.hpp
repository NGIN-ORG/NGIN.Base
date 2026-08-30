/// @file Hash.hpp
/// @brief Algorithm-selected cryptographic hashing operations.
#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
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
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> HashInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept;

    /// @brief Hashes input into an owned byte buffer.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> Hash(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input);

}// namespace NGIN::Crypto::Hashing
