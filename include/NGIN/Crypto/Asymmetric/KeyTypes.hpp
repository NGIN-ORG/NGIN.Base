/// @file KeyTypes.hpp
/// @brief Strong public/private key types and fixed key-size metadata.
#pragma once

#include <NGIN/Crypto/Algorithm.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <utility>

namespace NGIN::Crypto::Asymmetric
{
    /// @brief Algorithm tag for Ed25519 key types.
    struct Ed25519KeyTag
    {
    };

    /// @brief Algorithm tag for X25519 key types.
    struct X25519KeyTag
    {
    };

    /// @brief Algorithm tag for ECDSA P-256 key types.
    struct EcdsaP256KeyTag
    {
    };

    namespace detail
    {
        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        template<NGIN::UIntSize Size>
        [[nodiscard]] constexpr FixedBytes<Size> CopyFixedBytes(ConstByteSpan bytes) noexcept
        {
            FixedBytes<Size> output {};
            for (NGIN::UIntSize i = 0; i < Size; ++i)
            {
                output[i] = bytes[i];
            }
            return output;
        }
    }// namespace detail

    /// @brief Fixed-size, non-secret public key associated with an algorithm tag.
    /// @tparam AlgorithmTag Tag preventing accidental mixing of algorithms.
    /// @tparam Size Encoded key size in bytes.
    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PublicKey
    {
    public:
        /// @brief Algorithm tag associated with this key.
        using Algorithm = AlgorithmTag;

        /// @brief Fixed-size byte representation.
        using ValueType = FixedBytes<Size>;

        /// @brief Encoded key size in bytes.
        static constexpr NGIN::UIntSize SizeValue = Size;

        /// @brief Constructs a zero-filled public key.
        constexpr PublicKey() noexcept = default;

        /// @brief Constructs a public key from an exact-size byte value.
        constexpr explicit PublicKey(ValueType bytes) noexcept
            : m_bytes {std::move(bytes)}
        {
        }

        /// @brief Constructs a public key from an exact-size byte value.
        [[nodiscard]] static constexpr PublicKey FromBytes(ValueType bytes) noexcept
        {
            return PublicKey {std::move(bytes)};
        }

        /// @brief Validates and copies a dynamically sized public-key encoding.
        /// @return A key, or `InvalidKey` when the span length differs from `SizeValue`.
        [[nodiscard]] static CryptoExpected<PublicKey> FromBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return detail::InvalidKey();
            }

            return PublicKey {detail::CopyFixedBytes<Size>(bytes)};
        }

        /// @brief Returns a read-only span over the encoded public-key bytes.
        [[nodiscard]] constexpr ConstByteSpan Bytes() const noexcept
        {
            return ConstByteSpan {m_bytes.data(), m_bytes.size()};
        }

        /// @brief Returns the fixed-size byte value.
        [[nodiscard]] constexpr const ValueType& View() const noexcept
        {
            return m_bytes;
        }

    private:
        ValueType m_bytes {};
    };

    /// @brief Fixed-size secret private key associated with an algorithm tag.
    /// @tparam AlgorithmTag Tag preventing accidental mixing of algorithms.
    /// @tparam Size Encoded key size in bytes.
    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PrivateKey
    {
    public:
        /// @brief Algorithm tag associated with this key.
        using Algorithm = AlgorithmTag;
        /// @brief Fixed-size byte representation accepted during construction.
        using ValueType = FixedBytes<Size>;
        /// @brief Secure storage type used for the private bytes.
        using SecretType = NGIN::Crypto::Memory::FixedSecret<Size>;

        /// @brief Encoded key size in bytes.
        static constexpr NGIN::UIntSize SizeValue = Size;

        /// @brief Constructs a zero-filled private key in secure storage.
        PrivateKey() noexcept = default;

        /// @brief Takes ownership of an exact-size secret value.
        explicit PrivateKey(SecretType secret) noexcept
            : m_secret {std::move(secret)}
        {
        }

        /// @brief Private keys are non-copyable to avoid duplicating secret material implicitly.
        PrivateKey(const PrivateKey&) = delete;
        /// @brief Private keys are non-copy-assignable to avoid duplicating secret material implicitly.
        PrivateKey& operator=(const PrivateKey&) = delete;
        /// @brief Transfers ownership of secret material.
        PrivateKey(PrivateKey&&) noexcept = default;
        /// @brief Replaces this key by transferring secret material.
        PrivateKey& operator=(PrivateKey&&) noexcept = default;

        /// @brief Moves an exact-size byte value into secure private-key storage.
        [[nodiscard]] static PrivateKey FromBytes(ValueType bytes) noexcept
        {
            return PrivateKey {SecretType::FromValue(std::move(bytes))};
        }

        /// @brief Validates and copies dynamically sized bytes into secure private-key storage.
        /// @return A key, or `InvalidKey` when the span length differs from `SizeValue`.
        [[nodiscard]] static CryptoExpected<PrivateKey> FromSecretBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return detail::InvalidKey();
            }

            return PrivateKey {SecretType::FromValue(detail::CopyFixedBytes<Size>(bytes))};
        }

        /// @brief Returns a read-only span over the private-key bytes.
        [[nodiscard]] ConstByteSpan Bytes() const noexcept
        {
            return m_secret.Bytes();
        }

        /// @brief Returns mutable access to private-key storage for backend output.
        /// @warning Callers must not retain or expose the returned span.
        [[nodiscard]] ByteSpan UnsafeMutableBytes() noexcept
        {
            return m_secret.UnsafeMutableBytes();
        }

        /// @brief Returns the secure storage object containing the private key.
        [[nodiscard]] const SecretType& Secret() const noexcept
        {
            return m_secret;
        }

    private:
        SecretType m_secret {};
    };

    /// @brief Public/private key pair whose member types encode their algorithm.
    template<class TPublicKey, class TPrivateKey>
    struct KeyPair
    {
        TPublicKey  publicKey {};
        TPrivateKey privateKey {};
    };

    /// @brief Fixed encoded sizes for a signature algorithm's key and signature material.
    struct SignatureKeySizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize signatureSize {0};
    };

    /// @brief Fixed encoded sizes for a key-agreement algorithm.
    struct KeyAgreementSizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize sharedSecretSize {0};
    };

    /// @brief Returns fixed key and signature sizes for an algorithm.
    /// @return Zero sizes when the algorithm uses variable-size encodings.
    [[nodiscard]] constexpr SignatureKeySizes GetSignatureKeySizes(SignatureAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case SignatureAlgorithm::Ed25519:
                return SignatureKeySizes {.publicKeySize = 32, .privateKeySize = 32, .signatureSize = 64};
            case SignatureAlgorithm::EcdsaP256Sha256:
                return SignatureKeySizes {.publicKeySize = 65, .privateKeySize = 32, .signatureSize = 64};
            case SignatureAlgorithm::RsaPssSha256:
                return {};
        }

        return {};
    }

    /// @brief Returns fixed key and shared-secret sizes for a key-agreement algorithm.
    [[nodiscard]] constexpr KeyAgreementSizes GetKeyAgreementSizes(KeyAgreementAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case KeyAgreementAlgorithm::X25519:
                return KeyAgreementSizes {.publicKeySize = 32, .privateKeySize = 32, .sharedSecretSize = 32};
        }

        return {};
    }
}// namespace NGIN::Crypto::Asymmetric
