#pragma once

#include <NGIN/Crypto/Asymmetric/Ecdsa.hpp>
#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/Rsa.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>
#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>
#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <utility>

namespace NGIN::Crypto::Keys
{
    namespace detail
    {
        [[nodiscard]] constexpr CryptoError KeyOperationInvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError KeyOperationUnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        class ScopedPrivateKeyDer final
        {
        public:
            explicit ScopedPrivateKeyDer(ByteBuffer der) noexcept
                : m_der {std::move(der)}
            {
            }

            ScopedPrivateKeyDer(const ScopedPrivateKeyDer&)            = delete;
            ScopedPrivateKeyDer& operator=(const ScopedPrivateKeyDer&) = delete;
            ScopedPrivateKeyDer(ScopedPrivateKeyDer&&) noexcept            = default;
            ScopedPrivateKeyDer& operator=(ScopedPrivateKeyDer&&) noexcept = default;

            ~ScopedPrivateKeyDer() noexcept
            {
                if (m_der.Size() != 0)
                {
                    NGIN::Crypto::Memory::SecureZero(ByteSpan {m_der.data(), m_der.Size()});
                }
            }

            [[nodiscard]] NGIN::Crypto::Memory::SecretView SecretView() const noexcept
            {
                return NGIN::Crypto::Memory::SecretView {ConstByteSpan {m_der.data(), m_der.Size()}};
            }

        private:
            ByteBuffer m_der;
        };
    }// namespace detail

    struct RsaOaepSubjectPublicKeyInfoEncryptInput
    {
        ConstByteSpan plaintext;
        ConstByteSpan label;
    };

    struct RsaOaepPrivateKeyInfoDecryptInput
    {
        ConstByteSpan ciphertext;
        ConstByteSpan label;
    };

    [[nodiscard]] inline CryptoExpected<ByteBuffer> SignPrivateKeyInfo(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const PrivateKeyInfo&                       privateKeyInfo,
            ConstByteSpan                               message)
    {
        if (algorithm == SignatureAlgorithm::Ed25519)
        {
            auto privateKey = ImportEd25519PrivateKey(privateKeyInfo);
            if (!privateKey.HasValue())
            {
                return privateKey.Error();
            }
            auto signature = NGIN::Crypto::Asymmetric::SignEd25519(context, privateKey.Value(), message);
            if (!signature.HasValue())
            {
                return signature.Error();
            }

            ByteBuffer output;
            output.Reserve(signature.Value().size());
            for (auto byte: signature.Value())
            {
                output.PushBack(byte);
            }
            return output;
        }

        if (algorithm == SignatureAlgorithm::EcdsaP256Sha256)
        {
            auto privateKey = ImportEcdsaP256PrivateKey(privateKeyInfo);
            if (!privateKey.HasValue())
            {
                return privateKey.Error();
            }
            auto signature = NGIN::Crypto::Asymmetric::SignEcdsaP256Sha256(context, privateKey.Value(), message);
            if (!signature.HasValue())
            {
                return signature.Error();
            }

            ByteBuffer output;
            output.Reserve(signature.Value().size());
            for (auto byte: signature.Value())
            {
                output.PushBack(byte);
            }
            return output;
        }

        if (algorithm == SignatureAlgorithm::RsaPssSha256)
        {
            if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::Rsa)
            {
                return detail::KeyOperationInvalidKey();
            }

            auto privateKeyDer = WritePrivateKeyInfo(
                    KeyAlgorithm::Rsa,
                    ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
            if (!privateKeyDer.HasValue())
            {
                return privateKeyDer.Error();
            }
            detail::ScopedPrivateKeyDer scopedPrivateKeyDer {std::move(privateKeyDer.Value())};

            return NGIN::Crypto::Asymmetric::SignRsaPssSha256(
                    context,
                    NGIN::Crypto::Asymmetric::RsaPssSha256SignInput {
                            .privateKeyDer = scopedPrivateKeyDer.SecretView(),
                            .message       = message,
                    });
        }

        return detail::KeyOperationUnsupportedAlgorithm();
    }

    [[nodiscard]] inline CryptoExpected<void> VerifySubjectPublicKeyInfo(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const SubjectPublicKeyInfo&                 publicKeyInfo,
            ConstByteSpan                               message,
            ConstByteSpan                               signature) noexcept
    {
        if (algorithm == SignatureAlgorithm::Ed25519)
        {
            auto publicKey = ImportEd25519PublicKey(publicKeyInfo);
            if (!publicKey.HasValue())
            {
                return publicKey.Error();
            }
            if (signature.size() != NGIN::Crypto::Signatures::SignatureSize(SignatureAlgorithm::Ed25519))
            {
                return CryptoError {CryptoErrorCode::InvalidTag};
            }

            NGIN::Crypto::Signatures::Ed25519Signature typedSignature {};
            for (NGIN::UIntSize i = 0; i < typedSignature.size(); ++i)
            {
                typedSignature[i] = signature[i];
            }

            return NGIN::Crypto::Asymmetric::VerifyEd25519(context, publicKey.Value(), message, typedSignature);
        }

        if (algorithm == SignatureAlgorithm::EcdsaP256Sha256)
        {
            auto publicKey = ImportEcdsaP256PublicKey(publicKeyInfo);
            if (!publicKey.HasValue())
            {
                return publicKey.Error();
            }
            if (signature.size() != NGIN::Crypto::Signatures::SignatureSize(SignatureAlgorithm::EcdsaP256Sha256))
            {
                return CryptoError {CryptoErrorCode::InvalidTag};
            }

            NGIN::Crypto::Asymmetric::EcdsaP256Sha256Signature typedSignature {};
            for (NGIN::UIntSize i = 0; i < typedSignature.size(); ++i)
            {
                typedSignature[i] = signature[i];
            }

            return NGIN::Crypto::Asymmetric::VerifyEcdsaP256Sha256(context, publicKey.Value(), message, typedSignature);
        }

        if (algorithm == SignatureAlgorithm::RsaPssSha256)
        {
            if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::Rsa)
            {
                return detail::KeyOperationInvalidKey();
            }

            auto publicKeyDer = WriteSubjectPublicKeyInfo(
                    KeyAlgorithm::Rsa,
                    ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
            if (!publicKeyDer.HasValue())
            {
                return publicKeyDer.Error();
            }

            return NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(
                    context,
                    NGIN::Crypto::Asymmetric::RsaPssSha256VerifyInput {
                            .publicKeyDer = ConstByteSpan {publicKeyDer.Value().data(), publicKeyDer.Value().Size()},
                            .message      = message,
                            .signature    = signature,
                    });
        }

        return detail::KeyOperationUnsupportedAlgorithm();
    }

    [[nodiscard]] inline CryptoExpected<ByteBuffer> EncryptSubjectPublicKeyInfoRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const SubjectPublicKeyInfo&                 publicKeyInfo,
            const RsaOaepSubjectPublicKeyInfoEncryptInput& input)
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::Rsa)
        {
            return detail::KeyOperationInvalidKey();
        }

        auto publicKeyDer = WriteSubjectPublicKeyInfo(
                KeyAlgorithm::Rsa,
                ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
        if (!publicKeyDer.HasValue())
        {
            return publicKeyDer.Error();
        }

        return NGIN::Crypto::Asymmetric::EncryptRsaOaepSha256(
                context,
                NGIN::Crypto::Asymmetric::RsaOaepSha256EncryptInput {
                        .publicKeyDer = ConstByteSpan {publicKeyDer.Value().data(), publicKeyDer.Value().Size()},
                        .plaintext    = input.plaintext,
                        .label        = input.label,
                });
    }

    [[nodiscard]] inline CryptoExpected<ByteBuffer> DecryptPrivateKeyInfoRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const PrivateKeyInfo&                       privateKeyInfo,
            const RsaOaepPrivateKeyInfoDecryptInput&    input)
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::Rsa)
        {
            return detail::KeyOperationInvalidKey();
        }

        auto privateKeyDer = WritePrivateKeyInfo(
                KeyAlgorithm::Rsa,
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
        if (!privateKeyDer.HasValue())
        {
            return privateKeyDer.Error();
        }
        detail::ScopedPrivateKeyDer scopedPrivateKeyDer {std::move(privateKeyDer.Value())};

        return NGIN::Crypto::Asymmetric::DecryptRsaOaepSha256(
                context,
                NGIN::Crypto::Asymmetric::RsaOaepSha256DecryptInput {
                        .privateKeyDer = scopedPrivateKeyDer.SecretView(),
                        .ciphertext    = input.ciphertext,
                        .label         = input.label,
                });
    }

    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Asymmetric::X25519SharedSecret> DeriveX25519SharedSecret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const PrivateKeyInfo&                       privateKeyInfo,
            const SubjectPublicKeyInfo&                 peerPublicKeyInfo) noexcept
    {
        auto privateKey = ImportX25519PrivateKey(privateKeyInfo);
        if (!privateKey.HasValue())
        {
            return privateKey.Error();
        }
        auto publicKey = ImportX25519PublicKey(peerPublicKeyInfo);
        if (!publicKey.HasValue())
        {
            return publicKey.Error();
        }

        return NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(context, privateKey.Value(), publicKey.Value());
    }
}// namespace NGIN::Crypto::Keys
