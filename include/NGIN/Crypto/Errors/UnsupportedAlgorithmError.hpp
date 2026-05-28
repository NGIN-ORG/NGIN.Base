#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto
{
    /// @brief Creates an unsupported-algorithm error.
    [[nodiscard]] constexpr CryptoError UnsupportedAlgorithmError() noexcept
    {
        return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
    }
}// namespace NGIN::Crypto
