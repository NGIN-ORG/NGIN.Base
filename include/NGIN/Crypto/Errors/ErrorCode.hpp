#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Recoverable crypto failure categories.
    enum class CryptoErrorCode : NGIN::UInt8
    {
        None,
        InvalidArgument,
        OutputBufferTooSmall,
        InvalidKey,
        InvalidNonce,
        InvalidTag,
        AuthenticationFailed,
        UnsupportedAlgorithm,
        UnsupportedBackend,
        BackendUnavailable,
        EntropyUnavailable,
        EncodingError,
        ParseError,
        PolicyRejected,
        InternalError,
    };

    /// @brief Returns a stable diagnostic string for a crypto error code.
    [[nodiscard]] const char* ToString(CryptoErrorCode code) noexcept;
}// namespace NGIN::Crypto
