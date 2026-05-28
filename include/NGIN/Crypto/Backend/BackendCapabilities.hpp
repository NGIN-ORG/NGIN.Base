#pragma once

#include <NGIN/Crypto/Algorithm.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto::Backend
{
    /// @brief Compact backend capability bitset.
    class BackendCapabilities
    {
    public:
        constexpr BackendCapabilities() noexcept = default;

        [[nodiscard]] constexpr bool SupportsRandom() const noexcept
        {
            return m_supportsRandom;
        }

        [[nodiscard]] constexpr bool Supports(HashAlgorithm algorithm) const noexcept
        {
            return HasBit(m_hashAlgorithms, algorithm);
        }

        [[nodiscard]] constexpr bool Supports(MacAlgorithm algorithm) const noexcept
        {
            return HasBit(m_macAlgorithms, algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KdfAlgorithm algorithm) const noexcept
        {
            return HasBit(m_kdfAlgorithms, algorithm);
        }

        [[nodiscard]] constexpr bool Supports(AeadAlgorithm algorithm) const noexcept
        {
            return HasBit(m_aeadAlgorithms, algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return HasBit(m_keyAgreementAlgorithms, algorithm);
        }

        [[nodiscard]] constexpr bool Supports(SignatureAlgorithm algorithm) const noexcept
        {
            return HasBit(m_signatureAlgorithms, algorithm);
        }

        constexpr BackendCapabilities& EnableRandom() noexcept
        {
            m_supportsRandom = true;
            return *this;
        }

        constexpr BackendCapabilities& Enable(HashAlgorithm algorithm) noexcept
        {
            SetBit(m_hashAlgorithms, algorithm);
            return *this;
        }

        constexpr BackendCapabilities& Enable(MacAlgorithm algorithm) noexcept
        {
            SetBit(m_macAlgorithms, algorithm);
            return *this;
        }

        constexpr BackendCapabilities& Enable(KdfAlgorithm algorithm) noexcept
        {
            SetBit(m_kdfAlgorithms, algorithm);
            return *this;
        }

        constexpr BackendCapabilities& Enable(AeadAlgorithm algorithm) noexcept
        {
            SetBit(m_aeadAlgorithms, algorithm);
            return *this;
        }

        constexpr BackendCapabilities& Enable(KeyAgreementAlgorithm algorithm) noexcept
        {
            SetBit(m_keyAgreementAlgorithms, algorithm);
            return *this;
        }

        constexpr BackendCapabilities& Enable(SignatureAlgorithm algorithm) noexcept
        {
            SetBit(m_signatureAlgorithms, algorithm);
            return *this;
        }

    private:
        template<class Algorithm>
        [[nodiscard]] static constexpr NGIN::UInt64 Mask(Algorithm algorithm) noexcept
        {
            return NGIN::UInt64 {1} << static_cast<NGIN::UInt8>(algorithm);
        }

        template<class Algorithm>
        [[nodiscard]] static constexpr bool HasBit(NGIN::UInt64 bits, Algorithm algorithm) noexcept
        {
            return (bits & Mask(algorithm)) != 0;
        }

        template<class Algorithm>
        static constexpr void SetBit(NGIN::UInt64& bits, Algorithm algorithm) noexcept
        {
            bits |= Mask(algorithm);
        }

        NGIN::UInt64 m_hashAlgorithms {0};
        NGIN::UInt64 m_macAlgorithms {0};
        NGIN::UInt64 m_kdfAlgorithms {0};
        NGIN::UInt64 m_aeadAlgorithms {0};
        NGIN::UInt64 m_keyAgreementAlgorithms {0};
        NGIN::UInt64 m_signatureAlgorithms {0};
        bool         m_supportsRandom {false};
    };
}// namespace NGIN::Crypto::Backend
