#include "OpenSslBackend.hpp"

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#if defined(OPENSSL_IS_BORINGSSL)
#    include <openssl/aead.h>
#endif

#include <cstring>
#include <limits>
#include <memory>

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
#if defined(OPENSSL_IS_BORINGSSL)
                    return nullptr;
#else
                    return EVP_chacha20_poly1305();
#endif
                case AeadAlgorithm::XChaCha20Poly1305:
                    return nullptr;
            }

            return nullptr;
        }

#if defined(OPENSSL_IS_BORINGSSL)
        [[nodiscard]] const EVP_AEAD* SelectBoringSslAead(AeadAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case AeadAlgorithm::ChaCha20Poly1305:
                    return EVP_aead_chacha20_poly1305();
                case AeadAlgorithm::Aes128Gcm:
                case AeadAlgorithm::Aes256Gcm:
                case AeadAlgorithm::XChaCha20Poly1305:
                    return nullptr;
            }

            return nullptr;
        }
#endif

        [[nodiscard]] bool FitsOpenSslInt(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<int>::max());
        }

        [[nodiscard]] bool FitsOpenSslLong(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<long>::max());
        }

        [[nodiscard]] const unsigned char* DataOrNull(ConstByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<const unsigned char*>(bytes.data());
        }

        [[nodiscard]] unsigned char* DataOrNull(ByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<unsigned char*>(bytes.data());
        }

#if defined(OPENSSL_IS_BORINGSSL)
        [[nodiscard]] unsigned char* MutableDataOrNull(ByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<unsigned char*>(bytes.data());
        }
#endif

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

        using BnPtr        = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
        using BnContextPtr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
        using EcKeyPtr     = std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)>;
        using EcPointPtr   = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
        using EcdsaSigPtr  = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;
        using EvpPkeyPtr   = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
        using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

        [[nodiscard]] CryptoExpected<FixedBytes<32>> Sha256DigestOpenSsl(ConstByteSpan input) noexcept
        {
            FixedBytes<32> digest {};
            auto           result = HashOpenSsl(HashAlgorithm::Sha256, input, ByteSpan {digest.data(), digest.size()});
            if (!result.HasValue())
            {
                return result.Error();
            }

            return digest;
        }

        [[nodiscard]] CryptoExpected<EcKeyPtr> CreateP256PrivateKey(ConstByteSpan privateKey) noexcept
        {
            EcKeyPtr key {EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free};
            if (key == nullptr)
            {
                return InternalError();
            }

            BnPtr privateScalar {BN_bin2bn(DataOrNull(privateKey), static_cast<int>(privateKey.size()), nullptr), BN_free};
            if (privateScalar == nullptr || EC_KEY_set_private_key(key.get(), privateScalar.get()) != 1)
            {
                return InvalidKey();
            }

            const EC_GROUP* group = EC_KEY_get0_group(key.get());
            if (group == nullptr)
            {
                return InternalError();
            }

            EcPointPtr   publicPoint {EC_POINT_new(group), EC_POINT_free};
            BnContextPtr bnContext {BN_CTX_new(), BN_CTX_free};
            if (publicPoint == nullptr || bnContext == nullptr)
            {
                return InternalError();
            }

            if (EC_POINT_mul(group, publicPoint.get(), privateScalar.get(), nullptr, nullptr, bnContext.get()) != 1 ||
                EC_KEY_set_public_key(key.get(), publicPoint.get()) != 1 ||
                EC_KEY_check_key(key.get()) != 1)
            {
                return InvalidKey();
            }

            return key;
        }

        [[nodiscard]] CryptoExpected<EcKeyPtr> CreateP256PublicKey(ConstByteSpan publicKey) noexcept
        {
            EcKeyPtr key {EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free};
            if (key == nullptr)
            {
                return InternalError();
            }

            const EC_GROUP* group = EC_KEY_get0_group(key.get());
            if (group == nullptr)
            {
                return InternalError();
            }

            EcPointPtr   publicPoint {EC_POINT_new(group), EC_POINT_free};
            BnContextPtr bnContext {BN_CTX_new(), BN_CTX_free};
            if (publicPoint == nullptr || bnContext == nullptr)
            {
                return InternalError();
            }

            if (EC_POINT_oct2point(
                        group,
                        publicPoint.get(),
                        reinterpret_cast<const unsigned char*>(publicKey.data()),
                        publicKey.size(),
                        bnContext.get()) != 1 ||
                EC_KEY_set_public_key(key.get(), publicPoint.get()) != 1 ||
                EC_KEY_check_key(key.get()) != 1)
            {
                return InvalidKey();
            }

            return key;
        }

        [[nodiscard]] CryptoExpected<EcdsaSigPtr> RawP256SignatureToOpenSsl(ConstByteSpan signature) noexcept
        {
            EcdsaSigPtr ecdsaSignature {ECDSA_SIG_new(), ECDSA_SIG_free};
            if (ecdsaSignature == nullptr)
            {
                return InternalError();
            }

            BnPtr r {BN_bin2bn(
                             reinterpret_cast<const unsigned char*>(signature.data()),
                             32,
                             nullptr),
                     BN_free};
            BnPtr s {BN_bin2bn(
                             reinterpret_cast<const unsigned char*>(signature.data() + 32),
                             32,
                             nullptr),
                     BN_free};
            if (r == nullptr || s == nullptr)
            {
                return InternalError();
            }

            if (ECDSA_SIG_set0(ecdsaSignature.get(), r.get(), s.get()) != 1)
            {
                return InvalidKey();
            }
            r.release();
            s.release();

            return ecdsaSignature;
        }

        [[nodiscard]] CryptoExpected<void> OpenSslP256SignatureToRaw(
                const ECDSA_SIG* signature,
                ByteSpan         output) noexcept
        {
            const BIGNUM* r = nullptr;
            const BIGNUM* s = nullptr;
            ECDSA_SIG_get0(signature, &r, &s);
            if (r == nullptr || s == nullptr)
            {
                return InternalError();
            }

            if (BN_bn2binpad(r, reinterpret_cast<unsigned char*>(output.data()), 32) != 32 ||
                BN_bn2binpad(s, reinterpret_cast<unsigned char*>(output.data() + 32), 32) != 32)
            {
                return InternalError();
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<EvpPkeyPtr> DecodePrivateKeyDer(
                NGIN::Crypto::Memory::SecretView privateKeyDer) noexcept
        {
            const auto keyBytes = privateKeyDer.Bytes();
            if (keyBytes.empty())
            {
                return InvalidKey();
            }
            if (!FitsOpenSslLong(keyBytes.size()))
            {
                return InvalidKey();
            }

            const auto* cursor = reinterpret_cast<const unsigned char*>(keyBytes.data());
            const auto* end    = cursor + keyBytes.size();
            EvpPkeyPtr  key {d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(keyBytes.size())), EVP_PKEY_free};
            if (key == nullptr || cursor != end)
            {
                return InvalidKey();
            }

            return key;
        }

        [[nodiscard]] CryptoExpected<EvpPkeyPtr> DecodePublicKeyDer(ConstByteSpan publicKeyDer) noexcept
        {
            if (publicKeyDer.empty())
            {
                return InvalidKey();
            }
            if (!FitsOpenSslLong(publicKeyDer.size()))
            {
                return InvalidKey();
            }

            const auto* cursor = reinterpret_cast<const unsigned char*>(publicKeyDer.data());
            const auto* end    = cursor + publicKeyDer.size();
            EvpPkeyPtr  key {d2i_PUBKEY(nullptr, &cursor, static_cast<long>(publicKeyDer.size())), EVP_PKEY_free};
            if (key == nullptr || cursor != end)
            {
                return InvalidKey();
            }

            return key;
        }

        [[nodiscard]] CryptoExpected<void> ConfigureRsaPssContext(EVP_PKEY_CTX* context) noexcept
        {
            if (EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PSS_PADDING) <= 0 ||
                EVP_PKEY_CTX_set_signature_md(context, EVP_sha256()) <= 0 ||
                EVP_PKEY_CTX_set_rsa_mgf1_md(context, EVP_sha256()) <= 0 ||
                EVP_PKEY_CTX_set_rsa_pss_saltlen(context, 32) <= 0)
            {
                return InvalidKey();
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ConfigureRsaOaepContext(
                EVP_PKEY_CTX* context,
                ConstByteSpan label) noexcept
        {
            if (EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_OAEP_PADDING) <= 0 ||
                EVP_PKEY_CTX_set_rsa_oaep_md(context, EVP_sha256()) <= 0 ||
                EVP_PKEY_CTX_set_rsa_mgf1_md(context, EVP_sha256()) <= 0)
            {
                return InvalidKey();
            }

            if (label.empty())
            {
                return {};
            }
            if (!FitsOpenSslInt(label.size()))
            {
                return InvalidArgument();
            }

            void* labelCopy = OPENSSL_malloc(label.size());
            if (labelCopy == nullptr)
            {
                return InternalError();
            }
            std::memcpy(labelCopy, label.data(), label.size());

            if (EVP_PKEY_CTX_set0_rsa_oaep_label(
                        context,
                        static_cast<unsigned char*>(labelCopy),
                        static_cast<int>(label.size())) <= 0)
            {
                OPENSSL_free(labelCopy);
                return InvalidKey();
            }

            return {};
        }

        [[nodiscard]] constexpr std::string_view OpenSslCompatibleVersion() noexcept
        {
#if defined(OPENSSL_IS_BORINGSSL)
            return "BoringSSL";
#else
            return OPENSSL_VERSION_TEXT;
#endif
        }

        [[nodiscard]] CryptoExpected<CryptoContext> CreateOpenSslCompatibleContext(BackendInfo info) noexcept
        {
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
            capabilities.Enable(AeadAlgorithm::Aes128Gcm)
                    .Enable(AeadAlgorithm::Aes256Gcm)
                    .Enable(AeadAlgorithm::ChaCha20Poly1305);
            capabilities.Enable(SignatureAlgorithm::Ed25519)
                    .Enable(SignatureAlgorithm::EcdsaP256Sha256)
                    .Enable(SignatureAlgorithm::RsaPssSha256)
                    .Enable(AsymmetricEncryptionAlgorithm::RsaOaepSha256)
                    .Enable(KeyAgreementAlgorithm::X25519);

            return CryptoContext {
                    info,
                    capabilities,
            };
        }
    }// namespace

    CryptoExpected<CryptoContext> CreateOpenSslContext(const BackendOptions& options) noexcept
    {
        (void) options;

        return CreateOpenSslCompatibleContext(
                BackendInfo {
                        BackendKind::ExternalPackage,
                        "openssl",
                        OpenSslCompatibleVersion(),
                        "OpenSSL libcrypto",
                        "NGIN_BASE_CRYPTO_WITH_OPENSSL",
                        "openssl",
                });
    }

    CryptoExpected<CryptoContext> CreateBoringSslContext(const BackendOptions& options) noexcept
    {
        (void) options;

        return CreateOpenSslCompatibleContext(
                BackendInfo {
                        BackendKind::ExternalPackage,
                        "boringssl",
                        OpenSslCompatibleVersion(),
                        "BoringSSL libcrypto",
                        "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                        "BoringSSL",
                });
    }

    CryptoExpected<void> RandomOpenSsl(ByteSpan output) noexcept
    {
        if (output.empty())
        {
            return {};
        }
        if (!FitsOpenSslInt(output.size()))
        {
            return InvalidArgument();
        }

        return RAND_bytes(reinterpret_cast<unsigned char*>(output.data()), static_cast<int>(output.size())) == 1
                       ? CryptoExpected<void> {}
                       : CryptoError {CryptoErrorCode::EntropyUnavailable};
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

#if defined(OPENSSL_IS_BORINGSSL)
    CryptoExpected<void> AeadSealBoringSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept
    {
        const EVP_AEAD* aead = SelectBoringSslAead(algorithm);
        if (aead == nullptr)
        {
            return UnsupportedAlgorithm();
        }
        if (key.Bytes().size() != EVP_AEAD_key_length(aead) || nonce.size() != EVP_AEAD_nonce_length(aead) ||
            tag.size() > EVP_AEAD_max_tag_len(aead))
        {
            return InvalidArgument();
        }

        EVP_AEAD_CTX context;
        const auto   keyBytes = key.Bytes();
        if (EVP_AEAD_CTX_init(
                    &context,
                    aead,
                    DataOrNull(keyBytes),
                    keyBytes.size(),
                    tag.size(),
                    nullptr) != 1)
        {
            return InternalError();
        }

        NGIN::UIntSize tagLength = 0;
        const auto     result    = EVP_AEAD_CTX_seal_scatter(
                &context,
                MutableDataOrNull(ciphertext),
                MutableDataOrNull(tag),
                &tagLength,
                tag.size(),
                DataOrNull(nonce),
                nonce.size(),
                DataOrNull(plaintext),
                plaintext.size(),
                nullptr,
                0,
                DataOrNull(associatedData),
                associatedData.size());
        EVP_AEAD_CTX_cleanup(&context);

        return result == 1 && tagLength == tag.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> AeadOpenBoringSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept
    {
        const EVP_AEAD* aead = SelectBoringSslAead(algorithm);
        if (aead == nullptr)
        {
            return UnsupportedAlgorithm();
        }
        if (key.Bytes().size() != EVP_AEAD_key_length(aead) || nonce.size() != EVP_AEAD_nonce_length(aead) ||
            tag.size() > EVP_AEAD_max_tag_len(aead))
        {
            return InvalidArgument();
        }

        EVP_AEAD_CTX context;
        const auto   keyBytes = key.Bytes();
        if (EVP_AEAD_CTX_init(
                    &context,
                    aead,
                    DataOrNull(keyBytes),
                    keyBytes.size(),
                    tag.size(),
                    nullptr) != 1)
        {
            return InternalError();
        }

        const auto result = EVP_AEAD_CTX_open_gather(
                &context,
                MutableDataOrNull(plaintext),
                DataOrNull(nonce),
                nonce.size(),
                DataOrNull(ciphertext),
                ciphertext.size(),
                DataOrNull(tag),
                tag.size(),
                DataOrNull(associatedData),
                associatedData.size());
        EVP_AEAD_CTX_cleanup(&context);

        if (result != 1)
        {
            NGIN::Crypto::Memory::SecureZero(plaintext);
            return AuthenticationFailed();
        }

        return {};
    }
#endif

    CryptoExpected<void> AeadSealOpenSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept
    {
#if defined(OPENSSL_IS_BORINGSSL)
        if (const EVP_AEAD* aead = SelectBoringSslAead(algorithm); aead != nullptr)
        {
            return AeadSealBoringSsl(algorithm, key, nonce, plaintext, associatedData, ciphertext, tag);
        }
#endif
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
#if defined(OPENSSL_IS_BORINGSSL)
        if (const EVP_AEAD* aead = SelectBoringSslAead(algorithm); aead != nullptr)
        {
            return AeadOpenBoringSsl(algorithm, key, nonce, ciphertext, associatedData, tag, plaintext);
        }
#endif
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
            if (algorithm != SignatureAlgorithm::EcdsaP256Sha256)
            {
                return UnsupportedAlgorithm();
            }

            auto key = CreateP256PrivateKey(privateKey.Bytes());
            if (!key.HasValue())
            {
                return key.Error();
            }

            auto digest = Sha256DigestOpenSsl(message);
            if (!digest.HasValue())
            {
                return digest.Error();
            }

            EcdsaSigPtr ecdsaSignature {
                    ECDSA_do_sign(
                            reinterpret_cast<const unsigned char*>(digest.Value().data()),
                            static_cast<int>(digest.Value().size()),
                            key.Value().get()),
                    ECDSA_SIG_free};
            if (ecdsaSignature == nullptr)
            {
                return InternalError();
            }

            return OpenSslP256SignatureToRaw(ecdsaSignature.get(), signature);
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
            if (algorithm != SignatureAlgorithm::EcdsaP256Sha256)
            {
                return UnsupportedAlgorithm();
            }

            auto key = CreateP256PublicKey(publicKey);
            if (!key.HasValue())
            {
                return key.Error();
            }

            auto ecdsaSignature = RawP256SignatureToOpenSsl(signature);
            if (!ecdsaSignature.HasValue())
            {
                return ecdsaSignature.Error();
            }

            auto digest = Sha256DigestOpenSsl(message);
            if (!digest.HasValue())
            {
                return digest.Error();
            }

            const int result = ECDSA_do_verify(
                    reinterpret_cast<const unsigned char*>(digest.Value().data()),
                    static_cast<int>(digest.Value().size()),
                    ecdsaSignature.Value().get(),
                    key.Value().get());
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

    CryptoExpected<ByteBuffer> RsaPssSha256SignOpenSsl(
            NGIN::Crypto::Memory::SecretView privateKeyDer,
            ConstByteSpan                    message)
    {
        auto key = DecodePrivateKeyDer(privateKeyDer);
        if (!key.HasValue())
        {
            return key.Error();
        }

        auto digest = Sha256DigestOpenSsl(message);
        if (!digest.HasValue())
        {
            return digest.Error();
        }

        EvpPkeyCtxPtr context {EVP_PKEY_CTX_new(key.Value().get(), nullptr), EVP_PKEY_CTX_free};
        if (context == nullptr)
        {
            return InternalError();
        }

        if (EVP_PKEY_sign_init(context.get()) <= 0)
        {
            return InternalError();
        }

        auto configured = ConfigureRsaPssContext(context.get());
        if (!configured.HasValue())
        {
            return configured.Error();
        }

        auto signatureSize = NGIN::UIntSize {0};
        if (EVP_PKEY_sign(
                    context.get(),
                    nullptr,
                    &signatureSize,
                    reinterpret_cast<const unsigned char*>(digest.Value().data()),
                    digest.Value().size()) <= 0)
        {
            return InternalError();
        }

        auto signature = MakeByteBuffer(signatureSize);
        if (EVP_PKEY_sign(
                    context.get(),
                    reinterpret_cast<unsigned char*>(signature.data()),
                    &signatureSize,
                    reinterpret_cast<const unsigned char*>(digest.Value().data()),
                    digest.Value().size()) <= 0)
        {
            return InternalError();
        }

        while (signature.Size() > signatureSize)
        {
            signature.PopBack();
        }

        return signature;
    }

    CryptoExpected<void> RsaPssSha256VerifyOpenSsl(
            ConstByteSpan publicKeyDer,
            ConstByteSpan message,
            ConstByteSpan signature) noexcept
    {
        auto key = DecodePublicKeyDer(publicKeyDer);
        if (!key.HasValue())
        {
            return key.Error();
        }

        auto digest = Sha256DigestOpenSsl(message);
        if (!digest.HasValue())
        {
            return digest.Error();
        }

        EvpPkeyCtxPtr context {EVP_PKEY_CTX_new(key.Value().get(), nullptr), EVP_PKEY_CTX_free};
        if (context == nullptr)
        {
            return InternalError();
        }

        if (EVP_PKEY_verify_init(context.get()) <= 0)
        {
            return InternalError();
        }

        auto configured = ConfigureRsaPssContext(context.get());
        if (!configured.HasValue())
        {
            return configured.Error();
        }

        const int result = EVP_PKEY_verify(
                context.get(),
                DataOrNull(signature),
                signature.size(),
                reinterpret_cast<const unsigned char*>(digest.Value().data()),
                digest.Value().size());
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

    CryptoExpected<ByteBuffer> RsaOaepSha256EncryptOpenSsl(
            ConstByteSpan publicKeyDer,
            ConstByteSpan plaintext,
            ConstByteSpan label)
    {
        auto key = DecodePublicKeyDer(publicKeyDer);
        if (!key.HasValue())
        {
            return key.Error();
        }

        EvpPkeyCtxPtr context {EVP_PKEY_CTX_new(key.Value().get(), nullptr), EVP_PKEY_CTX_free};
        if (context == nullptr)
        {
            return InternalError();
        }

        if (EVP_PKEY_encrypt_init(context.get()) <= 0)
        {
            return InternalError();
        }

        auto result = ConfigureRsaOaepContext(context.get(), label);
        if (!result.HasValue())
        {
            return result.Error();
        }

        auto ciphertextSize = NGIN::UIntSize {0};
        if (EVP_PKEY_encrypt(
                    context.get(),
                    nullptr,
                    &ciphertextSize,
                    DataOrNull(plaintext),
                    plaintext.size()) <= 0)
        {
            return InvalidArgument();
        }

        auto ciphertext = MakeByteBuffer(ciphertextSize);
        if (EVP_PKEY_encrypt(
                    context.get(),
                    reinterpret_cast<unsigned char*>(ciphertext.data()),
                    &ciphertextSize,
                    DataOrNull(plaintext),
                    plaintext.size()) <= 0)
        {
            return InvalidArgument();
        }

        while (ciphertext.Size() > ciphertextSize)
        {
            ciphertext.PopBack();
        }

        return ciphertext;
    }

    CryptoExpected<ByteBuffer> RsaOaepSha256DecryptOpenSsl(
            NGIN::Crypto::Memory::SecretView privateKeyDer,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    label)
    {
        auto key = DecodePrivateKeyDer(privateKeyDer);
        if (!key.HasValue())
        {
            return key.Error();
        }

        EvpPkeyCtxPtr context {EVP_PKEY_CTX_new(key.Value().get(), nullptr), EVP_PKEY_CTX_free};
        if (context == nullptr)
        {
            return InternalError();
        }

        if (EVP_PKEY_decrypt_init(context.get()) <= 0)
        {
            return InternalError();
        }

        auto result = ConfigureRsaOaepContext(context.get(), label);
        if (!result.HasValue())
        {
            return result.Error();
        }

        auto plaintextSize = NGIN::UIntSize {0};
        if (EVP_PKEY_decrypt(
                    context.get(),
                    nullptr,
                    &plaintextSize,
                    DataOrNull(ciphertext),
                    ciphertext.size()) <= 0)
        {
            return AuthenticationFailed();
        }

        auto plaintext = MakeByteBuffer(plaintextSize);
        if (EVP_PKEY_decrypt(
                    context.get(),
                    reinterpret_cast<unsigned char*>(plaintext.data()),
                    &plaintextSize,
                    DataOrNull(ciphertext),
                    ciphertext.size()) <= 0)
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.data(), plaintext.Size()});
            return AuthenticationFailed();
        }

        while (plaintext.Size() > plaintextSize)
        {
            plaintext.PopBack();
        }

        return plaintext;
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
