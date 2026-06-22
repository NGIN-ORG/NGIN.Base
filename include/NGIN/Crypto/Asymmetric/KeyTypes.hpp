#pragma once

#include <NGIN/Crypto/Algorithm.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <utility>

namespace NGIN::Crypto::Asymmetric
{
    struct Ed25519KeyTag
    {
    };

    struct X25519KeyTag
    {
    };

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

    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PublicKey
    {
    public:
        using Algorithm = AlgorithmTag;
        using ValueType = FixedBytes<Size>;

        static constexpr NGIN::UIntSize SizeValue = Size;

        constexpr PublicKey() noexcept = default;

        constexpr explicit PublicKey(ValueType bytes) noexcept
            : m_bytes {std::move(bytes)}
        {
        }

        [[nodiscard]] static constexpr PublicKey FromBytes(ValueType bytes) noexcept
        {
            return PublicKey {std::move(bytes)};
        }

        [[nodiscard]] static CryptoExpected<PublicKey> FromBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return detail::InvalidKey();
            }

            return PublicKey {detail::CopyFixedBytes<Size>(bytes)};
        }

        [[nodiscard]] constexpr ConstByteSpan Bytes() const noexcept
        {
            return ConstByteSpan {m_bytes.data(), m_bytes.size()};
        }

        [[nodiscard]] constexpr const ValueType& View() const noexcept
        {
            return m_bytes;
        }

    private:
        ValueType m_bytes {};
    };

    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PrivateKey
    {
    public:
        using Algorithm  = AlgorithmTag;
        using ValueType  = FixedBytes<Size>;
        using SecretType = NGIN::Crypto::Memory::FixedSecret<Size>;

        static constexpr NGIN::UIntSize SizeValue = Size;

        PrivateKey() noexcept = default;

        explicit PrivateKey(SecretType secret) noexcept
            : m_secret {std::move(secret)}
        {
        }

        PrivateKey(const PrivateKey&)                = delete;
        PrivateKey& operator=(const PrivateKey&)     = delete;
        PrivateKey(PrivateKey&&) noexcept            = default;
        PrivateKey& operator=(PrivateKey&&) noexcept = default;

        [[nodiscard]] static PrivateKey FromBytes(ValueType bytes) noexcept
        {
            return PrivateKey {SecretType::FromValue(std::move(bytes))};
        }

        [[nodiscard]] static CryptoExpected<PrivateKey> FromSecretBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return detail::InvalidKey();
            }

            return PrivateKey {SecretType::FromValue(detail::CopyFixedBytes<Size>(bytes))};
        }

        [[nodiscard]] ConstByteSpan Bytes() const noexcept
        {
            return m_secret.Bytes();
        }

        [[nodiscard]] ByteSpan UnsafeMutableBytes() noexcept
        {
            return m_secret.UnsafeMutableBytes();
        }

        [[nodiscard]] const SecretType& Secret() const noexcept
        {
            return m_secret;
        }

    private:
        SecretType m_secret {};
    };

    template<class TPublicKey, class TPrivateKey>
    struct KeyPair
    {
        TPublicKey  publicKey {};
        TPrivateKey privateKey {};
    };

    struct SignatureKeySizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize signatureSize {0};
    };

    struct KeyAgreementSizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize sharedSecretSize {0};
    };

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
