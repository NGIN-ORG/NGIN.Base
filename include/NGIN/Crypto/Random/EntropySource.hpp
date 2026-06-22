#pragma once

#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Random
{
    /// @brief Non-owning byte entropy source view for tests and backend adapters.
    class EntropySource
    {
    public:
        using FillFunction = CryptoExpected<void> (*)(void* state, ByteSpan output) noexcept;

        constexpr EntropySource() noexcept = default;

        constexpr EntropySource(void* state, FillFunction fill, bool cryptographicallySecure) noexcept
            : m_state {state}, m_fill {fill}, m_cryptographicallySecure {cryptographicallySecure}
        {
        }

        [[nodiscard]] constexpr bool IsAvailable() const noexcept
        {
            return m_fill != nullptr;
        }

        [[nodiscard]] constexpr bool IsCryptographicallySecure() const noexcept
        {
            return m_cryptographicallySecure;
        }

        [[nodiscard]] CryptoExpected<void> Fill(ByteSpan output) const noexcept
        {
            if (m_fill == nullptr)
            {
                return CryptoError {CryptoErrorCode::EntropyUnavailable};
            }

            return m_fill(m_state, output);
        }

    private:
        void*        m_state {nullptr};
        FillFunction m_fill {nullptr};
        bool         m_cryptographicallySecure {false};
    };

    /// @brief Returns the platform secure random source used by Random::Fill.
    [[nodiscard]] EntropySource PlatformEntropySource() noexcept;
}// namespace NGIN::Crypto::Random
