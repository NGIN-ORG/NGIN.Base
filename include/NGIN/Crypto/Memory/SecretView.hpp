#pragma once

#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Non-owning view over secret bytes.
    class SecretView
    {
    public:
        constexpr SecretView() noexcept = default;

        constexpr explicit SecretView(ConstByteSpan bytes) noexcept
            : m_bytes {bytes}
        {
        }

        [[nodiscard]] constexpr ConstByteSpan Bytes() const noexcept
        {
            return m_bytes;
        }

        [[nodiscard]] constexpr NGIN::UIntSize Size() const noexcept
        {
            return m_bytes.size();
        }

    private:
        ConstByteSpan m_bytes;
    };
}// namespace NGIN::Crypto::Memory
