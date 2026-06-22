#include "AppleBackend.hpp"

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>

#include <cstdint>
#include <limits>

namespace NGIN::Crypto::Backend::detail
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InternalError() noexcept
        {
            return CryptoError {CryptoErrorCode::InternalError};
        }

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr bool FitsCcLong(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<CC_LONG>::max());
        }

        [[nodiscard]] constexpr CryptoExpected<CCHmacAlgorithm> SelectHmac(MacAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case MacAlgorithm::HmacSha256:
                    return kCCHmacAlgSHA256;
                case MacAlgorithm::HmacSha512:
                    return kCCHmacAlgSHA512;
            }

            return UnsupportedAlgorithm();
        }

        [[nodiscard]] constexpr CryptoExpected<CCPseudoRandomAlgorithm> SelectPbkdf2Prf(KdfAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case KdfAlgorithm::Pbkdf2Sha256:
                    return kCCPRFHmacAlgSHA256;
                case KdfAlgorithm::Pbkdf2Sha512:
                    return kCCPRFHmacAlgSHA512;
                case KdfAlgorithm::HkdfSha256:
                case KdfAlgorithm::HkdfSha512:
                case KdfAlgorithm::Argon2id:
                    return UnsupportedAlgorithm();
            }

            return UnsupportedAlgorithm();
        }
    }// namespace

    CryptoExpected<CryptoContext> CreateAppleContext(const BackendOptions& options) noexcept
    {
        (void) options;

        BackendCapabilities capabilities;
        capabilities.EnableRandom()
                .Enable(HashAlgorithm::Sha256)
                .Enable(HashAlgorithm::Sha512)
                .Enable(MacAlgorithm::HmacSha256)
                .Enable(MacAlgorithm::HmacSha512)
                .Enable(KdfAlgorithm::Pbkdf2Sha256)
                .Enable(KdfAlgorithm::Pbkdf2Sha512);

        return CryptoContext {
                BackendInfo {
                        BackendKind::Platform,
                        "apple",
                        {},
                        "Apple CommonCrypto/Security",
                        "NGIN_BASE_CRYPTO_WITH_APPLE",
                },
                capabilities,
        };
    }

    CryptoExpected<void> HashApple(HashAlgorithm algorithm, ConstByteSpan input, ByteSpan output) noexcept
    {
        if (!FitsCcLong(input.size()))
        {
            return InvalidArgument();
        }

        const auto* inputData = input.empty() ? nullptr : reinterpret_cast<const void*>(input.data());
        switch (algorithm)
        {
            case HashAlgorithm::Sha256:
                if (output.size() != CC_SHA256_DIGEST_LENGTH)
                {
                    return InvalidArgument();
                }
                return CC_SHA256(inputData, static_cast<CC_LONG>(input.size()), reinterpret_cast<unsigned char*>(output.data())) !=
                                       nullptr
                               ? CryptoExpected<void> {}
                               : InternalError();
            case HashAlgorithm::Sha512:
                if (output.size() != CC_SHA512_DIGEST_LENGTH)
                {
                    return InvalidArgument();
                }
                return CC_SHA512(inputData, static_cast<CC_LONG>(input.size()), reinterpret_cast<unsigned char*>(output.data())) !=
                                       nullptr
                               ? CryptoExpected<void> {}
                               : InternalError();
            case HashAlgorithm::Sha3_256:
            case HashAlgorithm::Sha3_512:
            case HashAlgorithm::Blake3:
                return UnsupportedAlgorithm();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> MacApple(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept
    {
        auto hmac = SelectHmac(algorithm);
        if (!hmac.HasValue())
        {
            return hmac.Error();
        }

        const auto keyBytes = key.Bytes();
        if (keyBytes.empty())
        {
            return InvalidKey();
        }

        const auto expectedSize = algorithm == MacAlgorithm::HmacSha256 ? CC_SHA256_DIGEST_LENGTH : CC_SHA512_DIGEST_LENGTH;
        if (output.size() != expectedSize)
        {
            return InvalidArgument();
        }

        const auto* keyData   = reinterpret_cast<const void*>(keyBytes.data());
        const auto* inputData = input.empty() ? nullptr : reinterpret_cast<const void*>(input.data());
        CCHmac(hmac.Value(), keyData, keyBytes.size(), inputData, input.size(), output.data());
        return {};
    }

    CryptoExpected<void> Pbkdf2Apple(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept
    {
        auto prf = SelectPbkdf2Prf(algorithm);
        if (!prf.HasValue())
        {
            return prf.Error();
        }
        if (iterations == 0 || output.empty())
        {
            return InvalidArgument();
        }

        const auto passwordBytes = password.Bytes();
        if (passwordBytes.empty())
        {
            return InvalidKey();
        }

        const auto* saltData = salt.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(salt.data());
        const auto  status   = CCKeyDerivationPBKDF(
                kCCPBKDF2,
                reinterpret_cast<const char*>(passwordBytes.data()),
                passwordBytes.size(),
                saltData,
                salt.size(),
                prf.Value(),
                iterations,
                reinterpret_cast<std::uint8_t*>(output.data()),
                output.size());
        return status == kCCSuccess ? CryptoExpected<void> {} : InternalError();
    }
}// namespace NGIN::Crypto::Backend::detail
