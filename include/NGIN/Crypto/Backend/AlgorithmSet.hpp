#pragma once

#include <NGIN/Crypto/Backend/BackendCapabilities.hpp>

namespace NGIN::Crypto::Backend
{
    /// @brief Startup requirement set for backend algorithm selection.
    class AlgorithmSet
    {
    public:
        constexpr AlgorithmSet() noexcept = default;

        constexpr AlgorithmSet& RequireRandom() noexcept
        {
            m_requiresRandom = true;
            return *this;
        }

        constexpr AlgorithmSet& Require(HashAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(MacAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(KdfAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(AeadAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(KeyAgreementAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(AsymmetricEncryptionAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        constexpr AlgorithmSet& Require(SignatureAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        [[nodiscard]] constexpr bool RequiresRandom() const noexcept
        {
            return m_requiresRandom;
        }

        [[nodiscard]] constexpr bool Requires(HashAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(MacAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(KdfAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(AeadAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Requires(SignatureAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool IsSatisfiedBy(const BackendCapabilities& capabilities) const noexcept
        {
            return (!m_requiresRandom || capabilities.SupportsRandom()) &&
                   SatisfiesHashRequirements(capabilities) &&
                   SatisfiesMacRequirements(capabilities) &&
                   SatisfiesKdfRequirements(capabilities) &&
                   SatisfiesAeadRequirements(capabilities) &&
                   SatisfiesKeyAgreementRequirements(capabilities) &&
                   SatisfiesAsymmetricEncryptionRequirements(capabilities) &&
                   SatisfiesSignatureRequirements(capabilities);
        }

    private:
        [[nodiscard]] constexpr bool SatisfiesHashRequirements(const BackendCapabilities& capabilities) const noexcept
        {
            return (!Requires(HashAlgorithm::Sha256) || capabilities.Supports(HashAlgorithm::Sha256)) &&
                   (!Requires(HashAlgorithm::Sha512) || capabilities.Supports(HashAlgorithm::Sha512)) &&
                   (!Requires(HashAlgorithm::Sha3_256) || capabilities.Supports(HashAlgorithm::Sha3_256)) &&
                   (!Requires(HashAlgorithm::Sha3_512) || capabilities.Supports(HashAlgorithm::Sha3_512)) &&
                   (!Requires(HashAlgorithm::Blake3) || capabilities.Supports(HashAlgorithm::Blake3));
        }

        [[nodiscard]] constexpr bool SatisfiesMacRequirements(const BackendCapabilities& capabilities) const noexcept
        {
            return (!Requires(MacAlgorithm::HmacSha256) || capabilities.Supports(MacAlgorithm::HmacSha256)) &&
                   (!Requires(MacAlgorithm::HmacSha512) || capabilities.Supports(MacAlgorithm::HmacSha512));
        }

        [[nodiscard]] constexpr bool SatisfiesKdfRequirements(const BackendCapabilities& capabilities) const noexcept
        {
            return (!Requires(KdfAlgorithm::HkdfSha256) || capabilities.Supports(KdfAlgorithm::HkdfSha256)) &&
                   (!Requires(KdfAlgorithm::HkdfSha512) || capabilities.Supports(KdfAlgorithm::HkdfSha512)) &&
                   (!Requires(KdfAlgorithm::Pbkdf2Sha256) || capabilities.Supports(KdfAlgorithm::Pbkdf2Sha256)) &&
                   (!Requires(KdfAlgorithm::Pbkdf2Sha512) || capabilities.Supports(KdfAlgorithm::Pbkdf2Sha512)) &&
                   (!Requires(KdfAlgorithm::Argon2id) || capabilities.Supports(KdfAlgorithm::Argon2id));
        }

        [[nodiscard]] constexpr bool SatisfiesAeadRequirements(const BackendCapabilities& capabilities) const noexcept
        {
            return (!Requires(AeadAlgorithm::Aes128Gcm) || capabilities.Supports(AeadAlgorithm::Aes128Gcm)) &&
                   (!Requires(AeadAlgorithm::Aes256Gcm) || capabilities.Supports(AeadAlgorithm::Aes256Gcm)) &&
                   (!Requires(AeadAlgorithm::ChaCha20Poly1305) ||
                    capabilities.Supports(AeadAlgorithm::ChaCha20Poly1305)) &&
                   (!Requires(AeadAlgorithm::XChaCha20Poly1305) ||
                    capabilities.Supports(AeadAlgorithm::XChaCha20Poly1305));
        }

        [[nodiscard]] constexpr bool SatisfiesKeyAgreementRequirements(
                const BackendCapabilities& capabilities) const noexcept
        {
            return !Requires(KeyAgreementAlgorithm::X25519) || capabilities.Supports(KeyAgreementAlgorithm::X25519);
        }

        [[nodiscard]] constexpr bool SatisfiesAsymmetricEncryptionRequirements(
                const BackendCapabilities& capabilities) const noexcept
        {
            return !Requires(AsymmetricEncryptionAlgorithm::RsaOaepSha256) ||
                   capabilities.Supports(AsymmetricEncryptionAlgorithm::RsaOaepSha256);
        }

        [[nodiscard]] constexpr bool SatisfiesSignatureRequirements(
                const BackendCapabilities& capabilities) const noexcept
        {
            return (!Requires(SignatureAlgorithm::Ed25519) || capabilities.Supports(SignatureAlgorithm::Ed25519)) &&
                   (!Requires(SignatureAlgorithm::EcdsaP256Sha256) ||
                    capabilities.Supports(SignatureAlgorithm::EcdsaP256Sha256)) &&
                   (!Requires(SignatureAlgorithm::RsaPssSha256) ||
                    capabilities.Supports(SignatureAlgorithm::RsaPssSha256));
        }

        BackendCapabilities m_required;
        bool                m_requiresRandom {false};
    };
}// namespace NGIN::Crypto::Backend
