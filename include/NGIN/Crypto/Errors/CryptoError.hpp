/// @file CryptoError.hpp
/// @brief Recoverable crypto error value with optional platform status.
#pragma once

#include <NGIN/Crypto/Errors/CryptoErrorCode.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Small value object describing a recoverable crypto failure.
    class NGIN_CRYPTO_API CryptoError
    {
    public:
        /// @brief Constructs a success value with no error.
        constexpr CryptoError() noexcept = default;

        /// @brief Constructs an error from a portable code and optional platform code.
        constexpr explicit CryptoError(CryptoErrorCode code, NGIN::Int32 platformCode = 0) noexcept
            : m_code {code}, m_platformCode {platformCode}
        {
        }

        /// @brief Returns the portable crypto error code.
        [[nodiscard]] constexpr CryptoErrorCode Code() const noexcept
        {
            return m_code;
        }

        /// @brief Returns the backend or operating-system status code, or zero when absent.
        [[nodiscard]] constexpr NGIN::Int32 PlatformCode() const noexcept
        {
            return m_platformCode;
        }

        /// @brief Returns whether this value represents a failure.
        [[nodiscard]] constexpr bool HasError() const noexcept
        {
            return m_code != CryptoErrorCode::None;
        }

        /// @brief Returns a static human-readable description of the portable error code.
        [[nodiscard]] const char* Message() const noexcept;

    private:
        CryptoErrorCode m_code {CryptoErrorCode::None};
        NGIN::Int32     m_platformCode {0};
    };
}// namespace NGIN::Crypto
