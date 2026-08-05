/// @file EntropySource.hpp
/// @brief Non-owning type-erased entropy source for crypto backends and tests.
#pragma once

#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Random
{
    /// @brief Non-owning byte entropy source view for tests and backend adapters.
    class EntropySource
    {
    public:
        /// @brief Callback signature used to fill an output span from opaque source state.
        using FillFunction = CryptoExpected<void> (*)(void* state, ByteSpan output) noexcept;

        /// @brief Constructs an unavailable entropy source.
        constexpr EntropySource() noexcept = default;

        /// @brief Constructs a source from borrowed state and a fill callback.
        /// @warning `state` must remain valid while this source is used.
        constexpr EntropySource(void* state, FillFunction fill, bool cryptographicallySecure) noexcept
            : m_state {state}, m_fill {fill}, m_cryptographicallySecure {cryptographicallySecure}
        {
        }

        /// @brief Returns whether a fill callback is installed.
        [[nodiscard]] constexpr bool IsAvailable() const noexcept
        {
            return m_fill != nullptr;
        }

        /// @brief Returns whether the source claims cryptographic security.
        [[nodiscard]] constexpr bool IsCryptographicallySecure() const noexcept
        {
            return m_cryptographicallySecure;
        }

        /// @brief Fills an output span or returns `EntropyUnavailable` when no callback is installed.
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
    [[nodiscard]] NGIN_CRYPTO_API EntropySource PlatformEntropySource() noexcept;
}// namespace NGIN::Crypto::Random
