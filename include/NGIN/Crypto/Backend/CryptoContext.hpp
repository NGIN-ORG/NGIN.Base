/// @file CryptoContext.hpp
/// @brief Provider-neutral crypto capability discovery and operation dispatch.
#pragma once

#include <NGIN/Crypto/Backend/BackendCapabilities.hpp>
#include <NGIN/Crypto/Backend/BackendInfo.hpp>
#include <NGIN/Crypto/Backend/BackendOptions.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <array>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Backend
{
    /// @brief Human-readable support status for diagnostics and tooling.
    struct AlgorithmSupportInfo
    {
        bool             supported {false};
        std::string_view reason;
    };

    /// @brief One candidate-provider result captured during context creation.
    struct BackendSelectionDiagnostic
    {
        BackendInfo      backend;
        CryptoErrorCode  code {CryptoErrorCode::BackendUnavailable};
        std::string_view reason;
    };

    /// @brief Fixed-size, allocation-free diagnostics for backend selection attempts.
    class BackendSelectionDiagnostics
    {
    public:
        /// @brief Maximum number of rejected backend candidates retained.
        static constexpr NGIN::UIntSize MAX_DIAGNOSTICS {6};

        /// @brief Appends a diagnostic when fixed storage remains; excess entries are discarded.
        constexpr void Add(BackendSelectionDiagnostic diagnostic) noexcept
        {
            if (m_count < m_entries.size())
            {
                m_entries[m_count++] = diagnostic;
            }
        }

        /// @brief Returns the number of retained diagnostics.
        [[nodiscard]] constexpr NGIN::UIntSize Count() const noexcept
        {
            return m_count;
        }

        /// @brief Returns whether no diagnostics were retained.
        [[nodiscard]] constexpr bool Empty() const noexcept
        {
            return m_count == 0;
        }

        /// @brief Returns a diagnostic without bounds checking.
        [[nodiscard]] constexpr const BackendSelectionDiagnostic& operator[](NGIN::UIntSize index) const noexcept
        {
            return m_entries[index];
        }

    private:
        std::array<BackendSelectionDiagnostic, MAX_DIAGNOSTICS> m_entries {};
        NGIN::UIntSize                                          m_count {0};
    };

    /// @brief Explicit neutral handle for crypto backend capabilities and operations.
    class NGIN_CRYPTO_API CryptoContext
    {
    public:
        /// @brief Constructs an empty context with no backend capabilities.
        constexpr CryptoContext() noexcept = default;

        /// @brief Constructs a context from backend metadata and probed capabilities.
        constexpr CryptoContext(BackendInfo info, BackendCapabilities capabilities) noexcept
            : m_info {info}, m_capabilities {capabilities}
        {
        }

        /// @brief Returns metadata for the selected backend.
        [[nodiscard]] constexpr const BackendInfo& Info() const noexcept
        {
            return m_info;
        }

        /// @brief Returns the selected backend's capability set.
        [[nodiscard]] constexpr const BackendCapabilities& Capabilities() const noexcept
        {
            return m_capabilities;
        }

        /// @brief Returns whether the backend provides secure random bytes.
        [[nodiscard]] constexpr bool SupportsRandom() const noexcept
        {
            return m_capabilities.SupportsRandom();
        }

        /// @brief Returns whether the backend supports a hash algorithm.
        [[nodiscard]] constexpr bool Supports(HashAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports a message-authentication algorithm.
        [[nodiscard]] constexpr bool Supports(MacAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports a key-derivation algorithm.
        [[nodiscard]] constexpr bool Supports(KdfAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports an authenticated-encryption algorithm.
        [[nodiscard]] constexpr bool Supports(AeadAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports a key-agreement algorithm.
        [[nodiscard]] constexpr bool Supports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports an asymmetric-encryption algorithm.
        [[nodiscard]] constexpr bool Supports(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Returns whether the backend supports a signature algorithm.
        [[nodiscard]] constexpr bool Supports(SignatureAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        /// @brief Describes secure-random support for diagnostics and tooling.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeRandomSupport() const noexcept
        {
            return SupportsRandom() ? SupportedAlgorithmStatus() : UnsupportedRandomStatus();
        }

        /// @brief Describes support for a hash algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(HashAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for a message-authentication algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(MacAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for a key-derivation algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(KdfAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for an authenticated-encryption algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(AeadAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for a key-agreement algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for an asymmetric-encryption algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Describes support for a signature algorithm.
        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(SignatureAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        /// @brief Fills an output span with cryptographically secure random bytes.
        [[nodiscard]] CryptoExpected<void> FillRandom(ByteSpan output) const noexcept;

        /// @brief Computes a digest into caller-provided output storage.
        /// @return An error when the algorithm is unavailable or output has the wrong size.
        [[nodiscard]] CryptoExpected<void> HashInto(
                HashAlgorithm algorithm,
                ConstByteSpan input,
                ByteSpan      output) const noexcept;

        /// @brief Computes a message-authentication code into caller-provided output storage.
        [[nodiscard]] CryptoExpected<void> MacInto(
                MacAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    input,
                ByteSpan                         output) const noexcept;

        /// @brief Derives key material with HKDF into caller-provided output storage.
        [[nodiscard]] CryptoExpected<void> HkdfInto(
                KdfAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView inputKeyMaterial,
                ConstByteSpan                    salt,
                ConstByteSpan                    info,
                ByteSpan                         output) const noexcept;

        /// @brief Derives key material from a password with PBKDF2.
        [[nodiscard]] CryptoExpected<void> Pbkdf2Into(
                KdfAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView password,
                ConstByteSpan                    salt,
                NGIN::UInt32                     iterations,
                ByteSpan                         output) const noexcept;

        /// @brief Derives key material from a password with Argon2id.
        [[nodiscard]] CryptoExpected<void> Argon2idInto(
                NGIN::Crypto::Memory::SecretView password,
                ConstByteSpan                    salt,
                NGIN::UInt32                     memoryKiB,
                NGIN::UInt32                     iterations,
                NGIN::UInt32                     parallelism,
                ByteSpan                         output) const noexcept;

        /// @brief Creates a self-describing Argon2id password hash string.
        [[nodiscard]] CryptoExpected<std::string> HashPassword(
                NGIN::Crypto::Memory::SecretView password,
                NGIN::UInt32                     memoryKiB,
                NGIN::UInt32                     iterations,
                NGIN::UInt32                     parallelism) const;

        /// @brief Verifies a password against a self-describing encoded hash.
        [[nodiscard]] CryptoExpected<void> VerifyPasswordHash(
                NGIN::Crypto::Memory::SecretView password,
                std::string_view                 encodedHash) const noexcept;

        /// @brief Returns whether an encoded password hash should be regenerated for the requested policy.
        [[nodiscard]] CryptoExpected<bool> PasswordHashNeedsRehash(
                std::string_view encodedHash,
                NGIN::UInt32     memoryKiB,
                NGIN::UInt32     iterations,
                NGIN::UInt32     parallelism) const noexcept;

        /// @brief Authenticates and encrypts plaintext into separate ciphertext and tag buffers.
        [[nodiscard]] CryptoExpected<void> AeadSealInto(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ConstByteSpan                    plaintext,
                ConstByteSpan                    associatedData,
                ByteSpan                         ciphertext,
                ByteSpan                         tag) const noexcept;

        /// @brief Authenticates and decrypts ciphertext into caller-provided plaintext storage.
        /// @details Authentication failure is returned without exposing unauthenticated plaintext.
        [[nodiscard]] CryptoExpected<void> AeadOpenInto(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ConstByteSpan                    ciphertext,
                ConstByteSpan                    associatedData,
                ConstByteSpan                    tag,
                ByteSpan                         plaintext) const noexcept;

        /// @brief Generates an Ed25519 key pair into fixed-size caller-provided buffers.
        [[nodiscard]] CryptoExpected<void> GenerateEd25519KeyPairInto(
                ByteSpan publicKey,
                ByteSpan privateKey) const noexcept;

        /// @brief Signs a message into caller-provided signature storage.
        [[nodiscard]] CryptoExpected<void> SignInto(
                SignatureAlgorithm               algorithm,
                NGIN::Crypto::Memory::SecretView privateKey,
                ConstByteSpan                    message,
                ByteSpan                         signature) const noexcept;

        /// @brief Verifies a signature for a message and public key.
        [[nodiscard]] CryptoExpected<void> VerifySignature(
                SignatureAlgorithm algorithm,
                ConstByteSpan      publicKey,
                ConstByteSpan      message,
                ConstByteSpan      signature) const noexcept;

        /// @brief Produces an RSA-PSS SHA-256 signature using a DER private key.
        [[nodiscard]] CryptoExpected<ByteBuffer> RsaPssSha256Sign(
                NGIN::Crypto::Memory::SecretView privateKeyDer,
                ConstByteSpan                    message) const;

        /// @brief Verifies an RSA-PSS SHA-256 signature using a DER public key.
        [[nodiscard]] CryptoExpected<void> RsaPssSha256Verify(
                ConstByteSpan publicKeyDer,
                ConstByteSpan message,
                ConstByteSpan signature) const noexcept;

        /// @brief Encrypts plaintext with RSA-OAEP SHA-256 and an optional label.
        [[nodiscard]] CryptoExpected<ByteBuffer> RsaOaepSha256Encrypt(
                ConstByteSpan publicKeyDer,
                ConstByteSpan plaintext,
                ConstByteSpan label = {}) const;

        /// @brief Decrypts RSA-OAEP SHA-256 ciphertext using a DER private key.
        [[nodiscard]] CryptoExpected<ByteBuffer> RsaOaepSha256Decrypt(
                NGIN::Crypto::Memory::SecretView privateKeyDer,
                ConstByteSpan                    ciphertext,
                ConstByteSpan                    label = {}) const;

        /// @brief Generates an X25519 key pair into fixed-size caller-provided buffers.
        [[nodiscard]] CryptoExpected<void> GenerateX25519KeyPairInto(
                ByteSpan publicKey,
                ByteSpan privateKey) const noexcept;

        /// @brief Derives an X25519 shared secret into caller-provided output storage.
        [[nodiscard]] CryptoExpected<void> DeriveX25519SharedSecretInto(
                NGIN::Crypto::Memory::SecretView privateKey,
                ConstByteSpan                    peerPublicKey,
                ByteSpan                         output) const noexcept;

        /// @brief Returns success when a hash algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(HashAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when a message-authentication algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(MacAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when a key-derivation algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KdfAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when an authenticated-encryption algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(AeadAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when a key-agreement algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when an asymmetric-encryption algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        /// @brief Returns success when a signature algorithm is supported.
        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(SignatureAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

    private:
        [[nodiscard]] constexpr AlgorithmSupportInfo SupportedAlgorithmStatus() const noexcept
        {
            return AlgorithmSupportInfo {
                    .supported = true,
                    .reason    = "supported",
            };
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo UnsupportedRandomStatus() const noexcept
        {
            return AlgorithmSupportInfo {
                    .supported = false,
                    .reason    = "backend does not provide secure random",
            };
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo UnsupportedAlgorithmStatus() const noexcept
        {
            if (m_info.Name() == "platform-random")
            {
                return AlgorithmSupportInfo {
                        .supported = false,
                        .reason    = "platform-random provides OS secure random only",
                };
            }

            if (m_info.Name() == "cng")
            {
                return AlgorithmSupportInfo {
                        .supported = false,
                        .reason    = "algorithm is not supported by the CNG backend or failed its capability probe",
                };
            }

            if (m_info.Name() == "openssl")
            {
                return AlgorithmSupportInfo {
                        .supported = false,
                        .reason    = "algorithm is not supported by the OpenSSL-compatible backend or provider configuration",
                };
            }

            if (m_info.Name() == "libsodium")
            {
                return AlgorithmSupportInfo {
                        .supported = false,
                        .reason    = "algorithm is not supported by the libsodium backend",
                };
            }

            return AlgorithmSupportInfo {
                    .supported = false,
                    .reason    = "algorithm is not supported by this backend",
            };
        }

        BackendInfo         m_info;
        BackendCapabilities m_capabilities;
    };

    /// @brief Context creation result plus rejected candidate diagnostics.
    struct BackendContextSelection
    {
        CryptoExpected<CryptoContext> context;
        BackendSelectionDiagnostics   diagnostics;
    };

    /// @brief Creates a neutral context backed by the platform facilities available in NGIN.Base core.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options = {}) noexcept;

    /// @brief Creates a context and returns diagnostics for rejected candidate providers.
    [[nodiscard]] NGIN_CRYPTO_API BackendContextSelection CreateContextWithDiagnostics(const BackendOptions& options = {}) noexcept;

    /// @brief Creates a context from the strongest compiled provider that satisfies the default policy.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CryptoContext> CreateBestAvailableContext() noexcept;

    /// @brief Creates a context backed only by platform facilities.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CryptoContext> CreatePlatformContext() noexcept;

    /// @brief Creates a context backed by a named package provider such as "openssl".
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CryptoContext> CreatePackageContext(std::string_view packageName) noexcept;
}// namespace NGIN::Crypto::Backend
