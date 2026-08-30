#include "CngBackend.hpp"

#include <NGIN/Crypto/Memory/ZeroMemory.hpp>
#include <NGIN/Crypto/Random/SecureRandom.hpp>

#include <bcrypt.h>
#include <windows.h>

#include <limits>

namespace NGIN::Crypto::Backend::detail
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError AuthenticationFailed() noexcept
        {
            return CryptoError {CryptoErrorCode::AuthenticationFailed};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoError BackendUnavailable(NTSTATUS status) noexcept
        {
            return CryptoError {CryptoErrorCode::BackendUnavailable, static_cast<NGIN::Int32>(status)};
        }

        [[nodiscard]] constexpr CryptoError InternalError(NTSTATUS status) noexcept
        {
            return CryptoError {CryptoErrorCode::InternalError, static_cast<NGIN::Int32>(status)};
        }

        [[nodiscard]] constexpr bool StatusOk(NTSTATUS status) noexcept
        {
            return status >= 0;
        }

        [[nodiscard]] constexpr bool FitsUlong(NGIN::UIntSize size) noexcept
        {
            return size <= static_cast<NGIN::UIntSize>(std::numeric_limits<ULONG>::max());
        }

        [[nodiscard]] PUCHAR DataOrNull(ByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<PUCHAR>(bytes.data());
        }

        [[nodiscard]] PUCHAR DataOrNull(ConstByteSpan bytes) noexcept
        {
            return bytes.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<NGIN::Byte*>(bytes.data()));
        }

        [[nodiscard]] LPCWSTR SelectHashId(HashAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case HashAlgorithm::Sha256:
                    return BCRYPT_SHA256_ALGORITHM;
                case HashAlgorithm::Sha512:
                    return BCRYPT_SHA512_ALGORITHM;
                case HashAlgorithm::Sha3_256:
                case HashAlgorithm::Sha3_512:
                case HashAlgorithm::Blake3:
                    return nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] LPCWSTR SelectHashId(MacAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case MacAlgorithm::HmacSha256:
                    return BCRYPT_SHA256_ALGORITHM;
                case MacAlgorithm::HmacSha512:
                    return BCRYPT_SHA512_ALGORITHM;
            }

            return nullptr;
        }

        [[nodiscard]] LPCWSTR SelectHashId(KdfAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
                case KdfAlgorithm::Pbkdf2Sha256:
                    return BCRYPT_SHA256_ALGORITHM;
                case KdfAlgorithm::Pbkdf2Sha512:
                    return BCRYPT_SHA512_ALGORITHM;
                case KdfAlgorithm::HkdfSha256:
                case KdfAlgorithm::HkdfSha512:
                case KdfAlgorithm::Argon2id:
                    return nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] bool IsCngAesGcm(AeadAlgorithm algorithm) noexcept
        {
            return algorithm == AeadAlgorithm::Aes128Gcm || algorithm == AeadAlgorithm::Aes256Gcm;
        }

        class AlgorithmHandle final
        {
        public:
            constexpr AlgorithmHandle() noexcept = default;

            explicit AlgorithmHandle(BCRYPT_ALG_HANDLE handle) noexcept : m_handle {handle} {}

            AlgorithmHandle(const AlgorithmHandle&)            = delete;
            AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;

            AlgorithmHandle(AlgorithmHandle&& other) noexcept : m_handle {other.m_handle}
            {
                other.m_handle = nullptr;
            }

            AlgorithmHandle& operator=(AlgorithmHandle&& other) noexcept
            {
                if (this != &other)
                {
                    Close();
                    m_handle       = other.m_handle;
                    other.m_handle = nullptr;
                }

                return *this;
            }

            ~AlgorithmHandle() noexcept
            {
                Close();
            }

            [[nodiscard]] BCRYPT_ALG_HANDLE Get() const noexcept
            {
                return m_handle;
            }

        private:
            void Close() noexcept
            {
                if (m_handle != nullptr)
                {
                    BCryptCloseAlgorithmProvider(m_handle, 0);
                    m_handle = nullptr;
                }
            }

            BCRYPT_ALG_HANDLE m_handle {nullptr};
        };

        class HashHandle final
        {
        public:
            constexpr HashHandle() noexcept = default;

            explicit HashHandle(BCRYPT_HASH_HANDLE handle) noexcept : m_handle {handle} {}

            HashHandle(const HashHandle&)            = delete;
            HashHandle& operator=(const HashHandle&) = delete;

            HashHandle(HashHandle&& other) noexcept : m_handle {other.m_handle}
            {
                other.m_handle = nullptr;
            }

            HashHandle& operator=(HashHandle&& other) noexcept
            {
                if (this != &other)
                {
                    Close();
                    m_handle       = other.m_handle;
                    other.m_handle = nullptr;
                }

                return *this;
            }

            ~HashHandle() noexcept
            {
                Close();
            }

            [[nodiscard]] BCRYPT_HASH_HANDLE Get() const noexcept
            {
                return m_handle;
            }

        private:
            void Close() noexcept
            {
                if (m_handle != nullptr)
                {
                    BCryptDestroyHash(m_handle);
                    m_handle = nullptr;
                }
            }

            BCRYPT_HASH_HANDLE m_handle {nullptr};
        };

        class KeyHandle final
        {
        public:
            constexpr KeyHandle() noexcept = default;

            explicit KeyHandle(BCRYPT_KEY_HANDLE handle) noexcept : m_handle {handle} {}

            KeyHandle(const KeyHandle&)            = delete;
            KeyHandle& operator=(const KeyHandle&) = delete;

            KeyHandle(KeyHandle&& other) noexcept : m_handle {other.m_handle}
            {
                other.m_handle = nullptr;
            }

            KeyHandle& operator=(KeyHandle&& other) noexcept
            {
                if (this != &other)
                {
                    Close();
                    m_handle       = other.m_handle;
                    other.m_handle = nullptr;
                }

                return *this;
            }

            ~KeyHandle() noexcept
            {
                Close();
            }

            [[nodiscard]] BCRYPT_KEY_HANDLE Get() const noexcept
            {
                return m_handle;
            }

        private:
            void Close() noexcept
            {
                if (m_handle != nullptr)
                {
                    BCryptDestroyKey(m_handle);
                    m_handle = nullptr;
                }
            }

            BCRYPT_KEY_HANDLE m_handle {nullptr};
        };

        [[nodiscard]] CryptoExpected<AlgorithmHandle> OpenAlgorithm(
                LPCWSTR algorithmId,
                ULONG   flags = 0) noexcept
        {
            BCRYPT_ALG_HANDLE handle = nullptr;
            auto              status = BCryptOpenAlgorithmProvider(&handle, algorithmId, nullptr, flags);
            if (!StatusOk(status))
            {
                return std::unexpected(BackendUnavailable(status));
            }

            return AlgorithmHandle {handle};
        }

        [[nodiscard]] CryptoExpected<HashHandle> CreateHash(
                BCRYPT_ALG_HANDLE algorithm,
                ConstByteSpan     secret = {}) noexcept
        {
            if (!FitsUlong(secret.size()))
            {
                return std::unexpected(InvalidKey());
            }

            BCRYPT_HASH_HANDLE hash   = nullptr;
            auto               status = BCryptCreateHash(
                    algorithm,
                    &hash,
                    nullptr,
                    0,
                    DataOrNull(secret),
                    static_cast<ULONG>(secret.size()),
                    0);
            if (!StatusOk(status))
            {
                return std::unexpected(InternalError(status));
            }

            return HashHandle {hash};
        }

        [[nodiscard]] CryptoExpected<KeyHandle> GenerateSymmetricKey(
                BCRYPT_ALG_HANDLE                algorithm,
                NGIN::Crypto::Memory::SecretView key) noexcept
        {
            if (!FitsUlong(key.Size()))
            {
                return std::unexpected(InvalidKey());
            }

            auto              keyBytes = key.Bytes();
            BCRYPT_KEY_HANDLE handle   = nullptr;
            auto              status   = BCryptGenerateSymmetricKey(
                    algorithm,
                    &handle,
                    nullptr,
                    0,
                    DataOrNull(keyBytes),
                    static_cast<ULONG>(keyBytes.size()),
                    0);
            if (!StatusOk(status))
            {
                return std::unexpected(InvalidKey());
            }

            return KeyHandle {handle};
        }

        [[nodiscard]] CryptoExpected<void> SetGcmMode(BCRYPT_ALG_HANDLE algorithm) noexcept
        {
            auto status = BCryptSetProperty(
                    algorithm,
                    BCRYPT_CHAINING_MODE,
                    reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                    sizeof(BCRYPT_CHAIN_MODE_GCM),
                    0);
            if (!StatusOk(status))
            {
                return std::unexpected(BackendUnavailable(status));
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ProbeHash(HashAlgorithm algorithm) noexcept
        {
            auto algorithmId = SelectHashId(algorithm);
            if (algorithmId == nullptr)
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            auto provider = OpenAlgorithm(algorithmId);
            if (!provider.has_value())
            {
                return std::unexpected(std::move(provider).error());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ProbeMac(MacAlgorithm algorithm) noexcept
        {
            auto algorithmId = SelectHashId(algorithm);
            if (algorithmId == nullptr)
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            auto provider = OpenAlgorithm(algorithmId, BCRYPT_ALG_HANDLE_HMAC_FLAG);
            if (!provider.has_value())
            {
                return std::unexpected(std::move(provider).error());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ProbeAesGcm() noexcept
        {
            auto provider = OpenAlgorithm(BCRYPT_AES_ALGORITHM);
            if (!provider.has_value())
            {
                return std::unexpected(std::move(provider).error());
            }

            return SetGcmMode(provider.value().Get());
        }
    }// namespace

    CryptoExpected<CryptoContext> CreateCngContext(const BackendOptions& options) noexcept
    {
        (void) options;

        BackendCapabilities capabilities;
        if (NGIN::Crypto::Random::IsAvailable())
        {
            capabilities.EnableRandom();
        }
        if (ProbeHash(HashAlgorithm::Sha256).has_value())
        {
            capabilities.Enable(HashAlgorithm::Sha256);
        }
        if (ProbeHash(HashAlgorithm::Sha512).has_value())
        {
            capabilities.Enable(HashAlgorithm::Sha512);
        }
        if (ProbeMac(MacAlgorithm::HmacSha256).has_value())
        {
            capabilities.Enable(MacAlgorithm::HmacSha256).Enable(KdfAlgorithm::Pbkdf2Sha256);
        }
        if (ProbeMac(MacAlgorithm::HmacSha512).has_value())
        {
            capabilities.Enable(MacAlgorithm::HmacSha512).Enable(KdfAlgorithm::Pbkdf2Sha512);
        }
        if (ProbeAesGcm().has_value())
        {
            capabilities.Enable(AeadAlgorithm::Aes128Gcm).Enable(AeadAlgorithm::Aes256Gcm);
        }

        return CryptoContext {
                BackendInfo {
                        BackendKind::Platform,
                        "cng",
                        {},
                        "Windows CNG BCrypt",
                        "NGIN_BASE_CRYPTO_WITH_CNG",
                },
                capabilities,
        };
    }

    CryptoExpected<void> HashCng(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) noexcept
    {
        auto algorithmId = SelectHashId(algorithm);
        if (algorithmId == nullptr)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (!FitsUlong(input.size()) || !FitsUlong(output.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        auto provider = OpenAlgorithm(algorithmId);
        if (!provider.has_value())
        {
            return std::unexpected(std::move(provider).error());
        }

        auto hash = CreateHash(provider.value().Get());
        if (!hash.has_value())
        {
            return std::unexpected(std::move(hash).error());
        }

        auto status = BCryptHashData(
                hash.value().Get(),
                DataOrNull(input),
                static_cast<ULONG>(input.size()),
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        status = BCryptFinishHash(
                hash.value().Get(),
                DataOrNull(output),
                static_cast<ULONG>(output.size()),
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        return {};
    }

    CryptoExpected<void> MacCng(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept
    {
        auto algorithmId = SelectHashId(algorithm);
        if (algorithmId == nullptr)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (!FitsUlong(input.size()) || !FitsUlong(output.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        auto provider = OpenAlgorithm(algorithmId, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        if (!provider.has_value())
        {
            return std::unexpected(std::move(provider).error());
        }

        auto hash = CreateHash(provider.value().Get(), key.Bytes());
        if (!hash.has_value())
        {
            return std::unexpected(std::move(hash).error());
        }

        auto status = BCryptHashData(
                hash.value().Get(),
                DataOrNull(input),
                static_cast<ULONG>(input.size()),
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        status = BCryptFinishHash(
                hash.value().Get(),
                DataOrNull(output),
                static_cast<ULONG>(output.size()),
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        return {};
    }

    CryptoExpected<void> Pbkdf2Cng(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept
    {
        auto algorithmId = SelectHashId(algorithm);
        if (algorithmId == nullptr)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (!FitsUlong(password.Size()) || !FitsUlong(salt.size()) || !FitsUlong(output.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        auto provider = OpenAlgorithm(algorithmId, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        if (!provider.has_value())
        {
            return std::unexpected(std::move(provider).error());
        }

        auto passwordBytes = password.Bytes();
        auto status        = BCryptDeriveKeyPBKDF2(
                provider.value().Get(),
                DataOrNull(passwordBytes),
                static_cast<ULONG>(passwordBytes.size()),
                DataOrNull(salt),
                static_cast<ULONG>(salt.size()),
                iterations,
                DataOrNull(output),
                static_cast<ULONG>(output.size()),
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        return {};
    }

    CryptoExpected<void> AeadSealCng(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept
    {
        if (!IsCngAesGcm(algorithm))
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (!FitsUlong(nonce.size()) || !FitsUlong(plaintext.size()) || !FitsUlong(associatedData.size()) ||
            !FitsUlong(ciphertext.size()) || !FitsUlong(tag.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        auto provider = OpenAlgorithm(BCRYPT_AES_ALGORITHM);
        if (!provider.has_value())
        {
            return std::unexpected(std::move(provider).error());
        }

        auto gcm = SetGcmMode(provider.value().Get());
        if (!gcm.has_value())
        {
            return std::unexpected(std::move(gcm).error());
        }

        auto keyHandle = GenerateSymmetricKey(provider.value().Get(), key);
        if (!keyHandle.has_value())
        {
            return std::unexpected(std::move(keyHandle).error());
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce    = DataOrNull(nonce);
        authInfo.cbNonce    = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = DataOrNull(associatedData);
        authInfo.cbAuthData = static_cast<ULONG>(associatedData.size());
        authInfo.pbTag      = DataOrNull(tag);
        authInfo.cbTag      = static_cast<ULONG>(tag.size());

        ULONG produced = 0;
        auto  status   = BCryptEncrypt(
                keyHandle.value().Get(),
                DataOrNull(plaintext),
                static_cast<ULONG>(plaintext.size()),
                &authInfo,
                nullptr,
                0,
                DataOrNull(ciphertext),
                static_cast<ULONG>(ciphertext.size()),
                &produced,
                0);
        if (!StatusOk(status))
        {
            return std::unexpected(InternalError(status));
        }

        return produced == ciphertext.size()
                       ? CryptoExpected<void> {}
                       : CryptoExpected<void> {std::unexpected(InternalError(status))};
    }

    CryptoExpected<void> AeadOpenCng(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept
    {
        if (!IsCngAesGcm(algorithm))
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (!FitsUlong(nonce.size()) || !FitsUlong(ciphertext.size()) || !FitsUlong(associatedData.size()) ||
            !FitsUlong(tag.size()) || !FitsUlong(plaintext.size()))
        {
            return std::unexpected(InvalidArgument());
        }

        auto provider = OpenAlgorithm(BCRYPT_AES_ALGORITHM);
        if (!provider.has_value())
        {
            return std::unexpected(std::move(provider).error());
        }

        auto gcm = SetGcmMode(provider.value().Get());
        if (!gcm.has_value())
        {
            return std::unexpected(std::move(gcm).error());
        }

        auto keyHandle = GenerateSymmetricKey(provider.value().Get(), key);
        if (!keyHandle.has_value())
        {
            return std::unexpected(std::move(keyHandle).error());
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce    = DataOrNull(nonce);
        authInfo.cbNonce    = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = DataOrNull(associatedData);
        authInfo.cbAuthData = static_cast<ULONG>(associatedData.size());
        authInfo.pbTag      = DataOrNull(tag);
        authInfo.cbTag      = static_cast<ULONG>(tag.size());

        ULONG produced = 0;
        auto  status   = BCryptDecrypt(
                keyHandle.value().Get(),
                DataOrNull(ciphertext),
                static_cast<ULONG>(ciphertext.size()),
                &authInfo,
                nullptr,
                0,
                DataOrNull(plaintext),
                static_cast<ULONG>(plaintext.size()),
                &produced,
                0);
        if (!StatusOk(status))
        {
            NGIN::Crypto::Memory::SecureZero(plaintext);
            return std::unexpected(AuthenticationFailed());
        }

        return produced == plaintext.size() ? CryptoExpected<void> {} : InternalError(status);
    }
}// namespace NGIN::Crypto::Backend::detail
