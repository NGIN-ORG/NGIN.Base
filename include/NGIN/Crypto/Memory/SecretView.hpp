/// @file SecretView.hpp
/// @brief Explicit non-owning read-only view of secret bytes.
#pragma once

#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Non-owning view over secret bytes.
    class SecretView
    {
    public:
        /// @brief Constructs an empty secret view.
        constexpr SecretView() noexcept = default;

        /// @brief Constructs a view over borrowed secret bytes.
        /// @warning The byte storage must outlive this view.
        constexpr explicit SecretView(ConstByteSpan bytes) noexcept
            : m_bytes {bytes}
        {
        }

        /// @brief Returns the borrowed secret bytes.
        [[nodiscard]] constexpr ConstByteSpan Bytes() const noexcept
        {
            return m_bytes;
        }

        /// @brief Returns the number of borrowed bytes.
        [[nodiscard]] constexpr NGIN::UIntSize Size() const noexcept
        {
            return m_bytes.size();
        }

    private:
        ConstByteSpan m_bytes;
    };
}// namespace NGIN::Crypto::Memory
