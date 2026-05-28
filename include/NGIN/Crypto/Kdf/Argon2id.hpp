#pragma once

#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief Derives Argon2id key material.
    [[nodiscard]] inline CryptoExpected<void> Argon2idInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Argon2idParameters&                   parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {parameters}, output);
    }
}// namespace NGIN::Crypto::Kdf
