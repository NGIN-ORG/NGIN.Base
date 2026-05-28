#include "OpenSslBackend.hpp"

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/opensslv.h>

#include <limits>

namespace NGIN::Crypto::Backend::detail
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InternalError() noexcept
        {
            return CryptoError {CryptoErrorCode::InternalError};
        }

        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] const EVP_MD* SelectDigest(HashAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case HashAlgorithm::Sha256:
                    return EVP_sha256();
                case HashAlgorithm::Sha512:
                    return EVP_sha512();
                case HashAlgorithm::Sha3_256:
                case HashAlgorithm::Sha3_512:
                case HashAlgorithm::Blake3:
                    return nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] const EVP_MD* SelectDigest(MacAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case MacAlgorithm::HmacSha256:
                    return EVP_sha256();
                case MacAlgorithm::HmacSha512:
                    return EVP_sha512();
            }

            return nullptr;
        }
    }// namespace

    CryptoExpected<CryptoContext> CreateOpenSslContext(const BackendOptions& options) noexcept
    {
        (void) options;

        BackendCapabilities capabilities;
        capabilities.EnableRandom()
                .Enable(HashAlgorithm::Sha256)
                .Enable(HashAlgorithm::Sha512)
                .Enable(MacAlgorithm::HmacSha256)
                .Enable(MacAlgorithm::HmacSha512);

        return CryptoContext {
                BackendInfo {
                        BackendKind::ExternalPackage,
                        "openssl",
                        OPENSSL_VERSION_TEXT,
                },
                capabilities,
        };
    }

    CryptoExpected<void> HashOpenSsl(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) noexcept
    {
        const EVP_MD* digest = SelectDigest(algorithm);
        if (digest == nullptr)
        {
            return UnsupportedAlgorithm();
        }

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
        {
            return InternalError();
        }

        if (EVP_DigestInit_ex(context, digest, nullptr) != 1)
        {
            EVP_MD_CTX_free(context);
            return InternalError();
        }

        if (!input.empty() && EVP_DigestUpdate(context, input.data(), input.size()) != 1)
        {
            EVP_MD_CTX_free(context);
            return InternalError();
        }

        unsigned int produced = 0;
        if (EVP_DigestFinal_ex(context, reinterpret_cast<unsigned char*>(output.data()), &produced) != 1)
        {
            EVP_MD_CTX_free(context);
            return InternalError();
        }

        EVP_MD_CTX_free(context);
        return static_cast<NGIN::UIntSize>(produced) == output.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> MacOpenSsl(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept
    {
        const EVP_MD* digest = SelectDigest(algorithm);
        if (digest == nullptr)
        {
            return UnsupportedAlgorithm();
        }

        const auto keyBytes = key.Bytes();
        if (keyBytes.size() > static_cast<NGIN::UIntSize>(std::numeric_limits<int>::max()))
        {
            return InvalidKey();
        }

        const auto* keyData   = keyBytes.empty() ? nullptr : reinterpret_cast<const unsigned char*>(keyBytes.data());
        const auto* inputData = input.empty() ? nullptr : reinterpret_cast<const unsigned char*>(input.data());

        unsigned int   produced = 0;
        unsigned char* result   = HMAC(
                digest,
                keyData,
                static_cast<int>(keyBytes.size()),
                inputData,
                input.size(),
                reinterpret_cast<unsigned char*>(output.data()),
                &produced);
        if (result == nullptr)
        {
            return InternalError();
        }

        return static_cast<NGIN::UIntSize>(produced) == output.size() ? CryptoExpected<void> {} : InternalError();
    }
}// namespace NGIN::Crypto::Backend::detail
