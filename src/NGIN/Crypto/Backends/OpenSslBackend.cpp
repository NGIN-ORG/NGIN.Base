#include "OpenSslBackend.hpp"

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
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

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
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

        [[nodiscard]] const EVP_MD* SelectDigest(KdfAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case KdfAlgorithm::HkdfSha256:
                case KdfAlgorithm::Pbkdf2Sha256:
                    return EVP_sha256();
                case KdfAlgorithm::HkdfSha512:
                case KdfAlgorithm::Pbkdf2Sha512:
                    return EVP_sha512();
                case KdfAlgorithm::Argon2id:
                    return nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] bool FitsOpenSslInt(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<int>::max());
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
        capabilities.Enable(KdfAlgorithm::HkdfSha256)
                .Enable(KdfAlgorithm::HkdfSha512)
                .Enable(KdfAlgorithm::Pbkdf2Sha256)
                .Enable(KdfAlgorithm::Pbkdf2Sha512);

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
        if (!FitsOpenSslInt(keyBytes.size()))
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

    CryptoExpected<void> HkdfOpenSsl(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView inputKeyMaterial,
            ConstByteSpan                    salt,
            ConstByteSpan                    info,
            ByteSpan                         output) noexcept
    {
        const EVP_MD* digest = SelectDigest(algorithm);
        if (digest == nullptr || (algorithm != KdfAlgorithm::HkdfSha256 && algorithm != KdfAlgorithm::HkdfSha512))
        {
            return UnsupportedAlgorithm();
        }
        if (!FitsOpenSslInt(inputKeyMaterial.Size()) || !FitsOpenSslInt(salt.size()) || !FitsOpenSslInt(info.size()))
        {
            return InvalidKey();
        }

        EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (context == nullptr)
        {
            return InternalError();
        }

        const auto  inputBytes = inputKeyMaterial.Bytes();
        const auto  zeroByte   = NGIN::Byte {0};
        const auto* inputData  = inputBytes.empty() ? &zeroByte : inputBytes.data();

        if (EVP_PKEY_derive_init(context) <= 0 ||
            EVP_PKEY_CTX_set_hkdf_md(context, digest) <= 0 ||
            EVP_PKEY_CTX_set1_hkdf_key(
                    context,
                    reinterpret_cast<const unsigned char*>(inputData),
                    static_cast<int>(inputBytes.size())) <= 0)
        {
            EVP_PKEY_CTX_free(context);
            return InternalError();
        }

        if (!salt.empty() &&
            EVP_PKEY_CTX_set1_hkdf_salt(
                    context,
                    reinterpret_cast<const unsigned char*>(salt.data()),
                    static_cast<int>(salt.size())) <= 0)
        {
            EVP_PKEY_CTX_free(context);
            return InternalError();
        }

        if (!info.empty() &&
            EVP_PKEY_CTX_add1_hkdf_info(
                    context,
                    reinterpret_cast<const unsigned char*>(info.data()),
                    static_cast<int>(info.size())) <= 0)
        {
            EVP_PKEY_CTX_free(context);
            return InternalError();
        }

        auto outputSize = output.size();
        if (EVP_PKEY_derive(context, reinterpret_cast<unsigned char*>(output.data()), &outputSize) <= 0)
        {
            EVP_PKEY_CTX_free(context);
            return InternalError();
        }

        EVP_PKEY_CTX_free(context);
        return outputSize == output.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> Pbkdf2OpenSsl(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept
    {
        const EVP_MD* digest = SelectDigest(algorithm);
        if (digest == nullptr || (algorithm != KdfAlgorithm::Pbkdf2Sha256 && algorithm != KdfAlgorithm::Pbkdf2Sha512))
        {
            return UnsupportedAlgorithm();
        }
        if (!FitsOpenSslInt(password.Size()))
        {
            return InvalidKey();
        }
        if (!FitsOpenSslInt(salt.size()) ||
            !FitsOpenSslInt(output.size()) ||
            iterations > static_cast<NGIN::UInt32>(std::numeric_limits<int>::max()))
        {
            return InvalidArgument();
        }

        const auto  passwordBytes = password.Bytes();
        const auto  zeroByte      = NGIN::Byte {0};
        const auto* passwordData  = passwordBytes.empty() ? &zeroByte : passwordBytes.data();
        const auto* saltData      = salt.empty() ? &zeroByte : salt.data();

        const int result = PKCS5_PBKDF2_HMAC(
                reinterpret_cast<const char*>(passwordData),
                static_cast<int>(passwordBytes.size()),
                reinterpret_cast<const unsigned char*>(saltData),
                static_cast<int>(salt.size()),
                static_cast<int>(iterations),
                digest,
                static_cast<int>(output.size()),
                reinterpret_cast<unsigned char*>(output.data()));

        return result == 1 ? CryptoExpected<void> {} : InternalError();
    }
}// namespace NGIN::Crypto::Backend::detail
