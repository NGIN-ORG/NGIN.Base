#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Random
{
    /// @brief Creates an entropy-unavailable error.
    [[nodiscard]] constexpr CryptoError EntropyUnavailableError(NGIN::Int32 platformCode = 0) noexcept
    {
        return CryptoError {CryptoErrorCode::EntropyUnavailable, platformCode};
    }
}// namespace NGIN::Crypto::Random
