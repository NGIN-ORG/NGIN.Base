#pragma once

#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief Derives HKDF-SHA256 key material.
    [[nodiscard]] inline CryptoExpected<void> HkdfSha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha256, parameters}, output);
    }

    /// @brief Derives HKDF-SHA512 key material.
    [[nodiscard]] inline CryptoExpected<void> HkdfSha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha512, parameters}, output);
    }
}// namespace NGIN::Crypto::Kdf
