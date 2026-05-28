#pragma once

#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief Derives PBKDF2-HMAC-SHA256 key material.
    [[nodiscard]] inline CryptoExpected<void> Pbkdf2Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha256, parameters}, output);
    }

    /// @brief Derives PBKDF2-HMAC-SHA512 key material.
    [[nodiscard]] inline CryptoExpected<void> Pbkdf2Sha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha512, parameters}, output);
    }
}// namespace NGIN::Crypto::Kdf
