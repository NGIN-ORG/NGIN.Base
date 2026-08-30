/// @file Sha512.hpp
/// @brief Fixed-size SHA-512 hashing operations.
#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Hashing/Digest.hpp>

namespace NGIN::Crypto::Hashing
{
    /// @brief Hashes input as SHA-512 into a caller-provided digest.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> Sha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha512Digest&                               output) noexcept;

    /// @brief Hashes input as SHA-512 into an owned fixed-size digest.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<Sha512Digest> Sha512(
            const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input);
}// namespace NGIN::Crypto::Hashing
