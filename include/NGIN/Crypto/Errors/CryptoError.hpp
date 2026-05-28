#pragma once

#include <NGIN/Crypto/Errors/ErrorCode.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Small value object describing a recoverable crypto failure.
    class CryptoError
    {
    public:
        constexpr CryptoError() noexcept = default;

        constexpr explicit CryptoError(CryptoErrorCode code, NGIN::Int32 platformCode = 0) noexcept
            : m_code {code}, m_platformCode {platformCode}
        {
        }

        [[nodiscard]] constexpr CryptoErrorCode Code() const noexcept
        {
            return m_code;
        }

        [[nodiscard]] constexpr NGIN::Int32 PlatformCode() const noexcept
        {
            return m_platformCode;
        }

        [[nodiscard]] constexpr bool HasError() const noexcept
        {
            return m_code != CryptoErrorCode::None;
        }

        [[nodiscard]] const char* Message() const noexcept;

    private:
        CryptoErrorCode m_code {CryptoErrorCode::None};
        NGIN::Int32     m_platformCode {0};
    };
}// namespace NGIN::Crypto
