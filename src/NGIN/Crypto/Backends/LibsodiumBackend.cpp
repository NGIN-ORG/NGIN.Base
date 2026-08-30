#include "LibsodiumBackend.hpp"

#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <sodium.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>

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

        [[nodiscard]] constexpr CryptoError InvalidNonce() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidNonce};
        }

        [[nodiscard]] constexpr CryptoError InvalidTag() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidTag};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoError AuthenticationFailed() noexcept
        {
            return CryptoError {CryptoErrorCode::AuthenticationFailed};
        }

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] CryptoExpected<void> EnsureSodiumInitialized() noexcept
        {
            return sodium_init() >= 0 ? CryptoExpected<void> {} : InternalError();
        }

        [[nodiscard]] const unsigned char* DataOrNull(ConstByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<const unsigned char*>(bytes.data());
        }

        [[nodiscard]] unsigned char* DataOrNull(ByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<unsigned char*>(bytes.data());
        }

        [[nodiscard]] bool FitsUnsignedLongLong(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<unsigned long long>::max());
        }

        [[nodiscard]] CryptoExpected<std::size_t> ValidatePasswordHashParameters(
                NGIN::Crypto::Memory::SecretView password,
                NGIN::UInt32                     memoryKiB,
                NGIN::UInt32                     iterations,
                NGIN::UInt32                     parallelism) noexcept
        {
            if (parallelism != 1 ||
                password.Size() > crypto_pwhash_PASSWD_MAX ||
                !FitsUnsignedLongLong(password.Size()) ||
                iterations < crypto_pwhash_OPSLIMIT_MIN ||
                iterations > crypto_pwhash_OPSLIMIT_MAX ||
                memoryKiB == 0 ||
                memoryKiB > std::numeric_limits<std::size_t>::max() / 1024U)
            {
                return std::unexpected(InvalidArgument());
            }

            const auto memoryBytes = static_cast<std::size_t>(memoryKiB) * 1024U;
            if (memoryBytes < crypto_pwhash_MEMLIMIT_MIN || memoryBytes > crypto_pwhash_MEMLIMIT_MAX)
            {
                return std::unexpected(InvalidArgument());
            }

            return memoryBytes;
        }

        [[nodiscard]] CryptoExpected<void> CopySupportedPasswordHash(
                std::string_view                          encodedHash,
                std::array<char, crypto_pwhash_STRBYTES>& output) noexcept
        {
            constexpr std::string_view ARGON2ID_PREFIX {"$argon2id$"};

            if (encodedHash.empty() ||
                encodedHash.size() >= crypto_pwhash_STRBYTES ||
                !encodedHash.starts_with(ARGON2ID_PREFIX) ||
                std::find(encodedHash.begin(), encodedHash.end(), '\0') != encodedHash.end())
            {
                return std::unexpected(ParseError());
            }

            std::copy(encodedHash.begin(), encodedHash.end(), output.begin());
            output[encodedHash.size()] = '\0';
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ValidateXChaCha20Poly1305(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ConstByteSpan                    tag) noexcept
        {
            if (algorithm != AeadAlgorithm::XChaCha20Poly1305)
            {
                return std::unexpected(UnsupportedAlgorithm());
            }
            if (key.Size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES)
            {
                return std::unexpected(InvalidKey());
            }
            if (nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES)
            {
                return std::unexpected(InvalidNonce());
            }
            if (tag.size() != crypto_aead_xchacha20poly1305_ietf_ABYTES)
            {
                return std::unexpected(InvalidTag());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> SeedToEd25519SecretKey(
                NGIN::Crypto::Memory::SecretView                       seed,
                std::array<unsigned char, crypto_sign_SECRETKEYBYTES>& secretKey) noexcept
        {
            if (seed.Size() != crypto_sign_SEEDBYTES)
            {
                return std::unexpected(InvalidKey());
            }

            std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey {};
            if (crypto_sign_seed_keypair(publicKey.data(), secretKey.data(), DataOrNull(seed.Bytes())) != 0)
            {
                return std::unexpected(InvalidKey());
            }

            return {};
        }
    }// namespace

    CryptoExpected<CryptoContext> CreateLibsodiumContext(const BackendOptions& options) noexcept
    {
        (void) options;

        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        BackendCapabilities capabilities;
        capabilities.EnableRandom()
                .Enable(AeadAlgorithm::XChaCha20Poly1305)
                .Enable(KdfAlgorithm::Argon2id)
                .Enable(SignatureAlgorithm::Ed25519)
                .Enable(KeyAgreementAlgorithm::X25519);

        return CryptoContext {
                BackendInfo {
                        BackendKind::ExternalPackage,
                        "libsodium",
                        sodium_version_string(),
                        "libsodium",
                        "NGIN_BASE_CRYPTO_WITH_LIBSODIUM",
                        "libsodium",
                },
                capabilities,
        };
    }

    CryptoExpected<void> RandomLibsodium(ByteSpan output) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        if (!output.empty())
        {
            randombytes_buf(DataOrNull(output), output.size());
        }

        return {};
    }

    CryptoExpected<void> Argon2idLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism,
            ByteSpan                         output) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        if (parallelism != 1 ||
            salt.size() != crypto_pwhash_SALTBYTES ||
            output.size() < crypto_pwhash_BYTES_MIN ||
            output.size() > crypto_pwhash_BYTES_MAX ||
            password.Size() > crypto_pwhash_PASSWD_MAX ||
            iterations < crypto_pwhash_OPSLIMIT_MIN ||
            memoryKiB == 0 ||
            memoryKiB > std::numeric_limits<std::size_t>::max() / 1024U)
        {
            return std::unexpected(InvalidArgument());
        }

        const auto memoryBytes = static_cast<std::size_t>(memoryKiB) * 1024U;
        if (memoryBytes < crypto_pwhash_MEMLIMIT_MIN || memoryBytes > crypto_pwhash_MEMLIMIT_MAX)
        {
            return std::unexpected(InvalidArgument());
        }

        const auto passwordBytes = password.Bytes();
        const int  result        = crypto_pwhash(
                DataOrNull(output),
                static_cast<unsigned long long>(output.size()),
                reinterpret_cast<const char*>(DataOrNull(passwordBytes)),
                static_cast<unsigned long long>(passwordBytes.size()),
                DataOrNull(salt),
                static_cast<unsigned long long>(iterations),
                memoryBytes,
                crypto_pwhash_ALG_ARGON2ID13);

        return result == 0 ? CryptoExpected<void> {} : InvalidArgument();
    }

    CryptoExpected<std::string> HashPasswordLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism)
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        auto memoryBytes = ValidatePasswordHashParameters(password, memoryKiB, iterations, parallelism);
        if (!memoryBytes.has_value())
        {
            return std::unexpected(std::move(memoryBytes).error());
        }

        std::array<char, crypto_pwhash_STRBYTES> encoded {};
        const auto                               passwordBytes = password.Bytes();
        const int                                result        = crypto_pwhash_str_alg(
                encoded.data(),
                reinterpret_cast<const char*>(DataOrNull(passwordBytes)),
                static_cast<unsigned long long>(passwordBytes.size()),
                static_cast<unsigned long long>(iterations),
                memoryBytes.value(),
                crypto_pwhash_ALG_ARGON2ID13);

        if (result != 0)
        {
            return std::unexpected(InvalidArgument());
        }

        return std::string {encoded.data()};
    }

    CryptoExpected<void> VerifyPasswordHashLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            std::string_view                 encodedHash) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (password.Size() > crypto_pwhash_PASSWD_MAX || !FitsUnsignedLongLong(password.Size()))
        {
            return std::unexpected(InvalidArgument());
        }

        std::array<char, crypto_pwhash_STRBYTES> encoded {};
        auto                                     copied = CopySupportedPasswordHash(encodedHash, encoded);
        if (!copied.has_value())
        {
            return std::unexpected(std::move(copied).error());
        }

        const auto passwordBytes = password.Bytes();
        const int  result        = crypto_pwhash_str_verify(
                encoded.data(),
                reinterpret_cast<const char*>(DataOrNull(passwordBytes)),
                static_cast<unsigned long long>(passwordBytes.size()));

        return result == 0 ? CryptoExpected<void> {} : AuthenticationFailed();
    }

    CryptoExpected<bool> PasswordHashNeedsRehashLibsodium(
            std::string_view encodedHash,
            NGIN::UInt32     memoryKiB,
            NGIN::UInt32     iterations,
            NGIN::UInt32     parallelism) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        auto memoryBytes = ValidatePasswordHashParameters(
                NGIN::Crypto::Memory::SecretView {},
                memoryKiB,
                iterations,
                parallelism);
        if (!memoryBytes.has_value())
        {
            return std::unexpected(std::move(memoryBytes).error());
        }

        std::array<char, crypto_pwhash_STRBYTES> encoded {};
        auto                                     copied = CopySupportedPasswordHash(encodedHash, encoded);
        if (!copied.has_value())
        {
            return std::unexpected(std::move(copied).error());
        }

        const int result = crypto_pwhash_str_needs_rehash(
                encoded.data(),
                static_cast<unsigned long long>(iterations),
                memoryBytes.value());
        if (result < 0)
        {
            return std::unexpected(ParseError());
        }

        return result == 1;
    }

    CryptoExpected<void> AeadSealLibsodium(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        auto valid = ValidateXChaCha20Poly1305(algorithm, key, nonce, tag);
        if (!valid.has_value())
        {
            return std::unexpected(std::move(valid).error());
        }
        if (!FitsUnsignedLongLong(plaintext.size()) || !FitsUnsignedLongLong(associatedData.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        unsigned long long producedTagSize = 0;
        const auto         result          = crypto_aead_xchacha20poly1305_ietf_encrypt_detached(
                DataOrNull(ciphertext),
                DataOrNull(tag),
                &producedTagSize,
                DataOrNull(plaintext),
                static_cast<unsigned long long>(plaintext.size()),
                DataOrNull(associatedData),
                static_cast<unsigned long long>(associatedData.size()),
                nullptr,
                DataOrNull(nonce),
                DataOrNull(key.Bytes()));

        return result == 0 && producedTagSize == tag.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> AeadOpenLibsodium(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }

        auto valid = ValidateXChaCha20Poly1305(algorithm, key, nonce, tag);
        if (!valid.has_value())
        {
            return std::unexpected(std::move(valid).error());
        }
        if (!FitsUnsignedLongLong(ciphertext.size()) || !FitsUnsignedLongLong(associatedData.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        const auto result = crypto_aead_xchacha20poly1305_ietf_decrypt_detached(
                DataOrNull(plaintext),
                nullptr,
                DataOrNull(ciphertext),
                static_cast<unsigned long long>(ciphertext.size()),
                DataOrNull(tag),
                DataOrNull(associatedData),
                static_cast<unsigned long long>(associatedData.size()),
                DataOrNull(nonce),
                DataOrNull(key.Bytes()));
        if (result == 0)
        {
            return {};
        }

        NGIN::Crypto::Memory::SecureZero(plaintext);
        return std::unexpected(AuthenticationFailed());
    }

    CryptoExpected<void> Blake2bLibsodium(
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (output.size() < crypto_generichash_BYTES_MIN ||
            output.size() > crypto_generichash_BYTES_MAX ||
            key.Size() < crypto_generichash_KEYBYTES_MIN ||
            key.Size() > crypto_generichash_KEYBYTES_MAX ||
            !FitsUnsignedLongLong(input.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        const auto result = crypto_generichash(
                DataOrNull(output),
                output.size(),
                DataOrNull(input),
                static_cast<unsigned long long>(input.size()),
                DataOrNull(key.Bytes()),
                key.Size());
        return result == 0 ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> XChaCha20XorLibsodium(
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (key.Size() != crypto_stream_xchacha20_KEYBYTES ||
            nonce.size() != crypto_stream_xchacha20_NONCEBYTES ||
            input.size() != output.size() ||
            !FitsUnsignedLongLong(input.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        const auto result = crypto_stream_xchacha20_xor(
                DataOrNull(output),
                DataOrNull(input),
                static_cast<unsigned long long>(input.size()),
                DataOrNull(nonce),
                DataOrNull(key.Bytes()));
        return result == 0 ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> GenerateEd25519KeyPairLibsodium(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (publicKey.size() != crypto_sign_PUBLICKEYBYTES || privateKey.size() != crypto_sign_SEEDBYTES)
        {
            return std::unexpected(InvalidArgument());
        }

        randombytes_buf(DataOrNull(privateKey), privateKey.size());

        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey {};
        const auto                                            result = crypto_sign_seed_keypair(DataOrNull(publicKey), secretKey.data(), DataOrNull(privateKey));
        NGIN::Crypto::Memory::SecureZero(ByteSpan {reinterpret_cast<NGIN::Byte*>(secretKey.data()), secretKey.size()});

        return result == 0 ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> SignLibsodium(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (algorithm != SignatureAlgorithm::Ed25519)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (signature.size() != crypto_sign_BYTES)
        {
            return std::unexpected(InvalidArgument());
        }

        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey {};
        auto                                                  seedResult = SeedToEd25519SecretKey(privateKey, secretKey);
        if (!seedResult.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {reinterpret_cast<NGIN::Byte*>(secretKey.data()), secretKey.size()});
            return std::unexpected(std::move(seedResult).error());
        }

        unsigned long long produced = 0;
        const auto         result   = crypto_sign_detached(
                DataOrNull(signature),
                &produced,
                DataOrNull(message),
                static_cast<unsigned long long>(message.size()),
                secretKey.data());
        NGIN::Crypto::Memory::SecureZero(ByteSpan {reinterpret_cast<NGIN::Byte*>(secretKey.data()), secretKey.size()});

        return result == 0 && produced == signature.size() ? CryptoExpected<void> {} : InternalError();
    }

    CryptoExpected<void> VerifySignatureLibsodium(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (algorithm != SignatureAlgorithm::Ed25519)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (publicKey.size() != crypto_sign_PUBLICKEYBYTES)
        {
            return std::unexpected(InvalidKey());
        }
        if (signature.size() != crypto_sign_BYTES)
        {
            return std::unexpected(InvalidTag());
        }

        const int result = crypto_sign_verify_detached(
                DataOrNull(signature),
                DataOrNull(message),
                static_cast<unsigned long long>(message.size()),
                DataOrNull(publicKey));
        return result == 0 ? CryptoExpected<void> {} : AuthenticationFailed();
    }

    CryptoExpected<void> GenerateX25519KeyPairLibsodium(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (publicKey.size() != crypto_scalarmult_BYTES || privateKey.size() != crypto_scalarmult_SCALARBYTES)
        {
            return std::unexpected(InvalidArgument());
        }

        randombytes_buf(DataOrNull(privateKey), privateKey.size());
        return crypto_scalarmult_base(DataOrNull(publicKey), DataOrNull(privateKey)) == 0 ? CryptoExpected<void> {}
                                                                                          : InternalError();
    }

    CryptoExpected<void> DeriveX25519SharedSecretLibsodium(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) noexcept
    {
        auto initialized = EnsureSodiumInitialized();
        if (!initialized.has_value())
        {
            return std::unexpected(std::move(initialized).error());
        }
        if (privateKey.Size() != crypto_scalarmult_SCALARBYTES || peerPublicKey.size() != crypto_scalarmult_BYTES)
        {
            return std::unexpected(InvalidKey());
        }
        if (output.size() != crypto_scalarmult_BYTES)
        {
            return std::unexpected(InvalidArgument());
        }

        return crypto_scalarmult(DataOrNull(output), DataOrNull(privateKey.Bytes()), DataOrNull(peerPublicKey)) == 0
                       ? CryptoExpected<void> {}
                       : InvalidKey();
    }
}// namespace NGIN::Crypto::Backend::detail
