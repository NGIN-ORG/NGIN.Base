/// @file AlgorithmSet.hpp
/// @brief Declarative crypto requirements used during backend selection.
#pragma once

#include <NGIN/Crypto/Backend/BackendCapabilities.hpp>

namespace NGIN::Crypto::Backend
{
    /// @brief Startup requirement set for backend algorithm selection.
    class AlgorithmSet
    {
    public:
        /// @brief Constructs an empty requirement set.
        constexpr AlgorithmSet() noexcept = default;

        /// @brief Adds secure random generation to the requirement set.
        constexpr AlgorithmSet& RequireRandom() noexcept
        {
            m_requiresRandom = true;
            return *this;
        }

        /// @brief Requires a hash algorithm.
        constexpr AlgorithmSet& Require(HashAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires a message-authentication algorithm.
        constexpr AlgorithmSet& Require(MacAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires a key-derivation algorithm.
        constexpr AlgorithmSet& Require(KdfAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires an authenticated-encryption algorithm.
        constexpr AlgorithmSet& Require(AeadAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires a key-agreement algorithm.
        constexpr AlgorithmSet& Require(KeyAgreementAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires an asymmetric-encryption algorithm.
        constexpr AlgorithmSet& Require(AsymmetricEncryptionAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Requires a signature algorithm.
        constexpr AlgorithmSet& Require(SignatureAlgorithm algorithm) noexcept
        {
            m_required.Enable(algorithm);
            return *this;
        }

        /// @brief Returns whether secure random generation is required.
        [[nodiscard]] constexpr bool RequiresRandom() const noexcept
        {
            return m_requiresRandom;
        }

        /// @brief Returns whether a hash algorithm is required.
        [[nodiscard]] constexpr bool Requires(HashAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether a message-authentication algorithm is required.
        [[nodiscard]] constexpr bool Requires(MacAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether a key-derivation algorithm is required.
        [[nodiscard]] constexpr bool Requires(KdfAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether an authenticated-encryption algorithm is required.
        [[nodiscard]] constexpr bool Requires(AeadAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether a key-agreement algorithm is required.
        [[nodiscard]] constexpr bool Requires(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether an asymmetric-encryption algorithm is required.
        [[nodiscard]] constexpr bool Requires(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether a signature algorithm is required.
        [[nodiscard]] constexpr bool Requires(SignatureAlgorithm algorithm) const noexcept
        {
            return m_required.Supports(algorithm);
        }

        /// @brief Returns whether a backend capability set satisfies every requirement.
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
