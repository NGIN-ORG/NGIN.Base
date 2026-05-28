#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto
{
    /// @brief Creates an invalid-key error.
    [[nodiscard]] constexpr CryptoError InvalidKeyError() noexcept
    {
        return CryptoError {CryptoErrorCode::InvalidKey};
    }
}// namespace NGIN::Crypto
