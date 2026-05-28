#pragma once

#include <NGIN/Crypto/Backend/BackendCapabilities.hpp>
#include <NGIN/Crypto/Backend/BackendInfo.hpp>
#include <NGIN/Crypto/Backend/BackendOptions.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Backend
{
    /// @brief Explicit neutral handle for crypto backend capabilities and operations.
    class CryptoContext
    {
    public:
        constexpr CryptoContext() noexcept = default;

        constexpr CryptoContext(BackendInfo info, BackendCapabilities capabilities) noexcept
            : m_info {info}, m_capabilities {capabilities}
        {
        }

        [[nodiscard]] constexpr const BackendInfo& Info() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] constexpr const BackendCapabilities& Capabilities() const noexcept
        {
            return m_capabilities;
        }

        [[nodiscard]] constexpr bool SupportsRandom() const noexcept
        {
            return m_capabilities.SupportsRandom();
        }

        [[nodiscard]] constexpr bool Supports(HashAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(MacAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KdfAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(AeadAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(SignatureAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] CryptoExpected<void> FillRandom(ByteSpan output) const noexcept;

        [[nodiscard]] CryptoExpected<void> HashInto(
                HashAlgorithm algorithm,
                ConstByteSpan input,
                ByteSpan      output) const noexcept;

        [[nodiscard]] CryptoExpected<void> MacInto(
                MacAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    input,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(HashAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(MacAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KdfAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(AeadAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(SignatureAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

    private:
        BackendInfo         m_info;
        BackendCapabilities m_capabilities;
    };

    /// @brief Creates a neutral context backed by the platform facilities available in NGIN.Base core.
    [[nodiscard]] CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options = {}) noexcept;
}// namespace NGIN::Crypto::Backend
