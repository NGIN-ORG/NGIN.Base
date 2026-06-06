#include <NGIN/Crypto/Backend/CryptoContext.hpp>

#include <NGIN/Crypto/Random/SecureRandom.hpp>

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
#include "OpenSslBackend.hpp"
#endif

namespace NGIN::Crypto::Backend
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }
    }// namespace

    CryptoExpected<void> CryptoContext::FillRandom(ByteSpan output) const noexcept
    {
        if (!SupportsRandom())
        {
            return CryptoError {CryptoErrorCode::UnsupportedBackend};
        }

        return NGIN::Crypto::Random::Fill(output);
    }

    CryptoExpected<void> CryptoContext::HashInto(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::HashOpenSsl(algorithm, input, output);
        }
#else
        (void) input;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::MacInto(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::MacOpenSsl(algorithm, key, input, output);
        }
#else
        (void) key;
        (void) input;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::HkdfInto(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView inputKeyMaterial,
            ConstByteSpan                    salt,
            ConstByteSpan                    info,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::HkdfOpenSsl(algorithm, inputKeyMaterial, salt, info, output);
        }
#else
        (void) inputKeyMaterial;
        (void) salt;
        (void) info;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::Pbkdf2Into(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::Pbkdf2OpenSsl(algorithm, password, salt, iterations, output);
        }
#else
        (void) password;
        (void) salt;
        (void) iterations;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::AeadSealInto(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::AeadSealOpenSsl(algorithm, key, nonce, plaintext, associatedData, ciphertext, tag);
        }
#else
        (void) key;
        (void) nonce;
        (void) plaintext;
        (void) associatedData;
        (void) ciphertext;
        (void) tag;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::AeadOpenInto(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::AeadOpenOpenSsl(algorithm, key, nonce, ciphertext, associatedData, tag, plaintext);
        }
#else
        (void) key;
        (void) nonce;
        (void) ciphertext;
        (void) associatedData;
        (void) tag;
        (void) plaintext;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::GenerateEd25519KeyPairInto(
            ByteSpan publicKey,
            ByteSpan privateKey) const noexcept
    {
        auto supported = EnsureSupports(SignatureAlgorithm::Ed25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::GenerateEd25519KeyPairOpenSsl(publicKey, privateKey);
        }
#else
        (void) publicKey;
        (void) privateKey;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::SignInto(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::SignOpenSsl(algorithm, privateKey, message, signature);
        }
#else
        (void) privateKey;
        (void) message;
        (void) signature;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::VerifySignature(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::VerifySignatureOpenSsl(algorithm, publicKey, message, signature);
        }
#else
        (void) publicKey;
        (void) message;
        (void) signature;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::GenerateX25519KeyPairInto(
            ByteSpan publicKey,
            ByteSpan privateKey) const noexcept
    {
        auto supported = EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::GenerateX25519KeyPairOpenSsl(publicKey, privateKey);
        }
#else
        (void) publicKey;
        (void) privateKey;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::DeriveX25519SharedSecretInto(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "openssl")
        {
            return detail::DeriveX25519SharedSecretOpenSsl(privateKey, peerPublicKey, output);
        }
#else
        (void) privateKey;
        (void) peerPublicKey;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options) noexcept
    {
        if (options.requireSecureRandom && !NGIN::Crypto::Random::IsAvailable())
        {
            return CryptoError {CryptoErrorCode::BackendUnavailable};
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
        return detail::CreateOpenSslContext(options);
#else
        BackendCapabilities capabilities;
        capabilities.EnableRandom();

        return CryptoContext {BackendInfo {BackendKind::Platform, "platform-random"}, capabilities};
#endif
    }
}// namespace NGIN::Crypto::Backend
