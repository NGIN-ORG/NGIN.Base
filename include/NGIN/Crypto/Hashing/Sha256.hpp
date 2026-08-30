/// @file Sha256.hpp
/// @brief Fixed-size SHA-256 hashing operations.
#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Hashing/Digest.hpp>

namespace NGIN::Crypto::Hashing
{
    /// @brief Hashes input as SHA-256 into a caller-provided digest.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha256Digest&                               output) noexcept;

    /// @brief Hashes input as SHA-256 into an owned fixed-size digest.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<Sha256Digest> Sha256(
            const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input);
}// namespace NGIN::Crypto::Hashing
