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
        SecureToken() = default;

        explicit SecureToken(std::string value) noexcept
            : m_value {std::move(value)}
        {
        }

        [[nodiscard]] std::string_view Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] const std::string& String() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] NGIN::UIntSize Size() const noexcept
        {
            return m_value.size();
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return m_value.empty();
        }

    private:
        std::string m_value;
    };
}// namespace NGIN::Crypto::Tokens
