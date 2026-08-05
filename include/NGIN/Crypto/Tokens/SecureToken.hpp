/// @file SecureToken.hpp
/// @brief Opaque owned transport-safe token text.
#pragma once

#include <NGIN/Primitives.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace NGIN::Crypto::Tokens
{
    /// @brief Opaque transport-safe token text.
    class SecureToken
    {
    public:
        /// @brief Constructs an empty token.
        SecureToken() = default;

        /// @brief Takes ownership of encoded token text.
        explicit SecureToken(std::string value) noexcept
            : m_value {std::move(value)}
        {
        }

        /// @brief Returns a view of the encoded token text.
        [[nodiscard]] std::string_view Value() const noexcept
        {
            return m_value;
        }

        /// @brief Returns the owned token string.
        [[nodiscard]] const std::string& String() const noexcept
        {
            return m_value;
        }

        /// @brief Returns the encoded token length.
        [[nodiscard]] NGIN::UIntSize Size() const noexcept
        {
            return m_value.size();
        }

        /// @brief Returns whether the token contains no text.
        [[nodiscard]] bool Empty() const noexcept
        {
            return m_value.empty();
        }

    private:
        std::string m_value;
    };
}// namespace NGIN::Crypto::Tokens
