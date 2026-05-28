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
