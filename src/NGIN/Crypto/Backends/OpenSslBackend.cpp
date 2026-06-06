#include "OpenSslBackend.hpp"

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

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

        [[nodiscard]] constexpr CryptoError AuthenticationFailed() noexcept
        {
            return CryptoError {CryptoErrorCode::AuthenticationFailed};
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

        [[nodiscard]] const EVP_CIPHER* SelectCipher(AeadAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case AeadAlgorithm::Aes128Gcm:
                    return EVP_aes_128_gcm();
                case AeadAlgorithm::Aes256Gcm:
                    return EVP_aes_256_gcm();
                case AeadAlgorithm::ChaCha20Poly1305:
                case AeadAlgorithm::XChaCha20Poly1305:
                    return nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] bool FitsOpenSslInt(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<int>::max());
        }

        [[nodiscard]] const unsigned char* DataOrNull(ConstByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<const unsigned char*>(bytes.data());
        }

        [[nodiscard]] unsigned char* DataOrNull(ByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<unsigned char*>(bytes.data());
        }

        [[nodiscard]] CryptoExpected<void> GenerateRawKeyPairOpenSsl(
                int      keyType,
                ByteSpan publicKey,
                ByteSpan privateKey) noexcept
        {
            EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(keyType, nullptr);
            if (context == nullptr)
            {
                return InternalError();
            }

            EVP_PKEY* key = nullptr;
            if (EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_keygen(context, &key) <= 0 || key == nullptr)
            {
                EVP_PKEY_CTX_free(context);
                return InternalError();
            }

            auto publicKeySize  = publicKey.size();
            auto privateKeySize = privateKey.size();
            if (EVP_PKEY_get_raw_public_key(key, DataOrNull(publicKey), &publicKeySize) != 1 ||
                EVP_PKEY_get_raw_private_key(key, DataOrNull(privateKey), &privateKeySize) != 1)
            {
                EVP_PKEY_free(key);
                EVP_PKEY_CTX_free(context);
                return InternalError();
            }

            EVP_PKEY_free(key);
            EVP_PKEY_CTX_free(context);
            return publicKeySize == publicKey.size() && privateKeySize == privateKey.size()
                           ? CryptoExpected<void> {}
                           : InternalError();
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
        capabilities.Enable(AeadAlgorithm::Aes128Gcm).Enable(AeadAlgorithm::Aes256Gcm);
        capabilities.Enable(SignatureAlgorithm::Ed25519).Enable(KeyAgreementAlgorithm::X25519);

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

    CryptoExpected<void> AeadSealOpenSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept
    {
        const EVP_CIPHER* cipher = SelectCipher(algorithm);
        if (cipher == nullptr)
        {
            return UnsupportedAlgorithm();
        }
        if (!FitsOpenSslInt(plaintext.size()) || !FitsOpenSslInt(associatedData.size()))
        {
            return InvalidArgument();
        }

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (context == nullptr)
        {
            return InternalError();
        }

        const auto keyBytes = key.Bytes();
        int        produced = 0;
        int        total    = 0;

        if (EVP_EncryptInit_ex(context, cipher, nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
            EVP_EncryptInit_ex(
                    context,
                    nullptr,
                    nullptr,
                    reinterpret_cast<const unsigned char*>(keyBytes.data()),
                    reinterpret_cast<const unsigned char*>(nonce.data())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }

        if (!associatedData.empty() &&
            EVP_EncryptUpdate(
                    context,
                    nullptr,
                    &produced,
                    reinterpret_cast<const unsigned char*>(associatedData.data()),
                    static_cast<int>(associatedData.size())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }

        if (!plaintext.empty() &&
            EVP_EncryptUpdate(
                    context,
                    reinterpret_cast<unsigned char*>(ciphertext.data()),
                    &produced,
                    reinterpret_cast<const unsigned char*>(plaintext.data()),
                    static_cast<int>(plaintext.size())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }
        total += produced;

        if (EVP_EncryptFinal_ex(context, reinterpret_cast<unsigned char*>(ciphertext.data()) + total, &produced) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }
        total += produced;

        if (static_cast<NGIN::UIntSize>(total) != ciphertext.size() ||
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }

        EVP_CIPHER_CTX_free(context);
        return {};
    }

    CryptoExpected<void> AeadOpenOpenSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept
    {
        const EVP_CIPHER* cipher = SelectCipher(algorithm);
        if (cipher == nullptr)
        {
            return UnsupportedAlgorithm();
        }
        if (!FitsOpenSslInt(ciphertext.size()) || !FitsOpenSslInt(associatedData.size()))
        {
            return InvalidArgument();
        }

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (context == nullptr)
        {
            return InternalError();
        }

        const auto keyBytes = key.Bytes();
        int        produced = 0;
        int        total    = 0;

        if (EVP_DecryptInit_ex(context, cipher, nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
            EVP_DecryptInit_ex(
                    context,
                    nullptr,
                    nullptr,
                    reinterpret_cast<const unsigned char*>(keyBytes.data()),
                    reinterpret_cast<const unsigned char*>(nonce.data())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }

        if (!associatedData.empty() &&
            EVP_DecryptUpdate(
                    context,
                    nullptr,
                    &produced,
                    reinterpret_cast<const unsigned char*>(associatedData.data()),
                    static_cast<int>(associatedData.size())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            return InternalError();
        }

        if (!ciphertext.empty() &&
            EVP_DecryptUpdate(
                    context,
                    reinterpret_cast<unsigned char*>(plaintext.data()),
                    &produced,
                    reinterpret_cast<const unsigned char*>(ciphertext.data()),
                    static_cast<int>(ciphertext.size())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            NGIN::Crypto::Memory::SecureZero(plaintext);
            return InternalError();
        }
        total += produced;

        if (EVP_CIPHER_CTX_ctrl(
                    context,
                    EVP_CTRL_GCM_SET_TAG,
                    static_cast<int>(tag.size()),
                    const_cast<NGIN::Byte*>(tag.data())) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            NGIN::Crypto::Memory::SecureZero(plaintext);
            return InternalError();
        }

        if (EVP_DecryptFinal_ex(context, reinterpret_cast<unsigned char*>(plaintext.data()) + total, &produced) != 1)
        {
            EVP_CIPHER_CTX_free(context);
            NGIN::Crypto::Memory::SecureZero(plaintext);
            return AuthenticationFailed();
        }
        total += produced;

        EVP_CIPHER_CTX_free(context);
        return static_cast<NGIN::UIntSize>(total) == plaintext.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> GenerateEd25519KeyPairOpenSsl(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept
    {
        return GenerateRawKeyPairOpenSsl(EVP_PKEY_ED25519, publicKey, privateKey);
    }

    CryptoExpected<void> SignOpenSsl(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) noexcept
    {
        if (algorithm != SignatureAlgorithm::Ed25519)
        {
            return UnsupportedAlgorithm();
        }

        const auto privateKeyBytes = privateKey.Bytes();
        EVP_PKEY*  key             = EVP_PKEY_new_raw_private_key(
                EVP_PKEY_ED25519,
                nullptr,
                DataOrNull(privateKeyBytes),
                privateKeyBytes.size());
        if (key == nullptr)
        {
            return InvalidKey();
        }

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
        {
            EVP_PKEY_free(key);
            return InternalError();
        }

        auto signatureSize = signature.size();
        if (EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) != 1 ||
            EVP_DigestSign(context, DataOrNull(signature), &signatureSize, DataOrNull(message), message.size()) != 1)
        {
            EVP_MD_CTX_free(context);
            EVP_PKEY_free(key);
            return InternalError();
        }

        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        return signatureSize == signature.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> VerifySignatureOpenSsl(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) noexcept
    {
        if (algorithm != SignatureAlgorithm::Ed25519)
        {
            return UnsupportedAlgorithm();
        }

        EVP_PKEY* key = EVP_PKEY_new_raw_public_key(
                EVP_PKEY_ED25519,
                nullptr,
                DataOrNull(publicKey),
                publicKey.size());
        if (key == nullptr)
        {
            return InvalidKey();
        }

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
        {
            EVP_PKEY_free(key);
            return InternalError();
        }

        if (EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) != 1)
        {
            EVP_MD_CTX_free(context);
            EVP_PKEY_free(key);
            return InternalError();
        }

        const int result = EVP_DigestVerify(context, DataOrNull(signature), signature.size(), DataOrNull(message), message.size());
        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);

        if (result == 1)
        {
            return {};
        }
        if (result == 0)
        {
            return AuthenticationFailed();
        }
        return InternalError();
    }

    CryptoExpected<void> GenerateX25519KeyPairOpenSsl(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept
    {
        return GenerateRawKeyPairOpenSsl(EVP_PKEY_X25519, publicKey, privateKey);
    }

    CryptoExpected<void> DeriveX25519SharedSecretOpenSsl(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) noexcept
    {
        const auto privateKeyBytes = privateKey.Bytes();
        EVP_PKEY*  privatePkey     = EVP_PKEY_new_raw_private_key(
                EVP_PKEY_X25519,
                nullptr,
                DataOrNull(privateKeyBytes),
                privateKeyBytes.size());
        if (privatePkey == nullptr)
        {
            return InvalidKey();
        }

        EVP_PKEY* peerPkey = EVP_PKEY_new_raw_public_key(
                EVP_PKEY_X25519,
                nullptr,
                DataOrNull(peerPublicKey),
                peerPublicKey.size());
        if (peerPkey == nullptr)
        {
            EVP_PKEY_free(privatePkey);
            return InvalidKey();
        }

        EVP_PKEY_CTX* context = EVP_PKEY_CTX_new(privatePkey, nullptr);
        if (context == nullptr)
        {
            EVP_PKEY_free(peerPkey);
            EVP_PKEY_free(privatePkey);
            return InternalError();
        }

        auto outputSize = output.size();
        if (EVP_PKEY_derive_init(context) <= 0 ||
            EVP_PKEY_derive_set_peer(context, peerPkey) <= 0 ||
            EVP_PKEY_derive(context, DataOrNull(output), &outputSize) <= 0)
        {
            EVP_PKEY_CTX_free(context);
            EVP_PKEY_free(peerPkey);
            EVP_PKEY_free(privatePkey);
            return InternalError();
        }

        EVP_PKEY_CTX_free(context);
        EVP_PKEY_free(peerPkey);
        EVP_PKEY_free(privatePkey);
        return outputSize == output.size() ? CryptoExpected<void> {} : InternalError();
    }
}// namespace NGIN::Crypto::Backend::detail
