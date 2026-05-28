#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto
{
    /// @brief Creates an authentication failure error.
    [[nodiscard]] constexpr CryptoError AuthenticationError() noexcept
    {
        return CryptoError {CryptoErrorCode::AuthenticationFailed};
    }
}// namespace NGIN::Crypto
