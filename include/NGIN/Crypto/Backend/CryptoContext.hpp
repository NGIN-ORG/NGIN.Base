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
        static constexpr NGIN::UIntSize MAX_DIAGNOSTICS {6};

        constexpr void Add(BackendSelectionDiagnostic diagnostic) noexcept
        {
            if (m_count < m_entries.size())
            {
                m_entries[m_count++] = diagnostic;
            }
        }

        [[nodiscard]] constexpr NGIN::UIntSize Count() const noexcept
        {
            return m_count;
        }

        [[nodiscard]] constexpr bool Empty() const noexcept
        {
            return m_count == 0;
        }

        [[nodiscard]] constexpr const BackendSelectionDiagnostic& operator[](NGIN::UIntSize index) const noexcept
        {
            return m_entries[index];
        }

    private:
        std::array<BackendSelectionDiagnostic, MAX_DIAGNOSTICS> m_entries {};
        NGIN::UIntSize                                          m_count {0};
    };

    /// @brief Explicit neutral handle for crypto backend capabilities and operations.
    class CryptoContext
    {
    public:
        constexpr CryptoContext() noexcept = default;

        constexpr CryptoContext(BackendInfo info, BackendCapabilities capabilities) noexcept
            : m_info {info}, m_capabilities {capabilities}
        {
        }

        [[nodiscard]] constexpr const BackendInfo& Info() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] constexpr const BackendCapabilities& Capabilities() const noexcept
        {
            return m_capabilities;
        }

        [[nodiscard]] constexpr bool SupportsRandom() const noexcept
        {
            return m_capabilities.SupportsRandom();
        }

        [[nodiscard]] constexpr bool Supports(HashAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(MacAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KdfAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(AeadAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr bool Supports(SignatureAlgorithm algorithm) const noexcept
        {
            return m_capabilities.Supports(algorithm);
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeRandomSupport() const noexcept
        {
            return SupportsRandom() ? SupportedAlgorithmStatus() : UnsupportedRandomStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(HashAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(MacAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(KdfAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(AeadAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] constexpr AlgorithmSupportInfo DescribeSupport(SignatureAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? SupportedAlgorithmStatus() : UnsupportedAlgorithmStatus();
        }

        [[nodiscard]] CryptoExpected<void> FillRandom(ByteSpan output) const noexcept;

        [[nodiscard]] CryptoExpected<void> HashInto(
                HashAlgorithm algorithm,
                ConstByteSpan input,
                ByteSpan      output) const noexcept;

        [[nodiscard]] CryptoExpected<void> MacInto(
                MacAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    input,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] CryptoExpected<void> HkdfInto(
                KdfAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView inputKeyMaterial,
                ConstByteSpan                    salt,
                ConstByteSpan                    info,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] CryptoExpected<void> Pbkdf2Into(
                KdfAlgorithm                     algorithm,
                NGIN::Crypto::Memory::SecretView password,
                ConstByteSpan                    salt,
                NGIN::UInt32                     iterations,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] CryptoExpected<void> Argon2idInto(
                NGIN::Crypto::Memory::SecretView password,
                ConstByteSpan                    salt,
                NGIN::UInt32                     memoryKiB,
                NGIN::UInt32                     iterations,
                NGIN::UInt32                     parallelism,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] CryptoExpected<std::string> HashPassword(
                NGIN::Crypto::Memory::SecretView password,
                NGIN::UInt32                     memoryKiB,
                NGIN::UInt32                     iterations,
                NGIN::UInt32                     parallelism) const;

        [[nodiscard]] CryptoExpected<void> VerifyPasswordHash(
                NGIN::Crypto::Memory::SecretView password,
                std::string_view                 encodedHash) const noexcept;

        [[nodiscard]] CryptoExpected<bool> PasswordHashNeedsRehash(
                std::string_view encodedHash,
                NGIN::UInt32     memoryKiB,
                NGIN::UInt32     iterations,
                NGIN::UInt32     parallelism) const noexcept;

        [[nodiscard]] CryptoExpected<void> AeadSealInto(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ConstByteSpan                    plaintext,
                ConstByteSpan                    associatedData,
                ByteSpan                         ciphertext,
                ByteSpan                         tag) const noexcept;

        [[nodiscard]] CryptoExpected<void> AeadOpenInto(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ConstByteSpan                    ciphertext,
                ConstByteSpan                    associatedData,
                ConstByteSpan                    tag,
                ByteSpan                         plaintext) const noexcept;

        [[nodiscard]] CryptoExpected<void> GenerateEd25519KeyPairInto(
                ByteSpan publicKey,
                ByteSpan privateKey) const noexcept;

        [[nodiscard]] CryptoExpected<void> SignInto(
                SignatureAlgorithm               algorithm,
                NGIN::Crypto::Memory::SecretView privateKey,
                ConstByteSpan                    message,
                ByteSpan                         signature) const noexcept;

        [[nodiscard]] CryptoExpected<void> VerifySignature(
                SignatureAlgorithm algorithm,
                ConstByteSpan      publicKey,
                ConstByteSpan      message,
                ConstByteSpan      signature) const noexcept;

        [[nodiscard]] CryptoExpected<ByteBuffer> RsaPssSha256Sign(
                NGIN::Crypto::Memory::SecretView privateKeyDer,
                ConstByteSpan                    message) const;

        [[nodiscard]] CryptoExpected<void> RsaPssSha256Verify(
                ConstByteSpan publicKeyDer,
                ConstByteSpan message,
                ConstByteSpan signature) const noexcept;

        [[nodiscard]] CryptoExpected<ByteBuffer> RsaOaepSha256Encrypt(
                ConstByteSpan publicKeyDer,
                ConstByteSpan plaintext,
                ConstByteSpan label = {}) const;

        [[nodiscard]] CryptoExpected<ByteBuffer> RsaOaepSha256Decrypt(
                NGIN::Crypto::Memory::SecretView privateKeyDer,
                ConstByteSpan                    ciphertext,
                ConstByteSpan                    label = {}) const;

        [[nodiscard]] CryptoExpected<void> GenerateX25519KeyPairInto(
                ByteSpan publicKey,
                ByteSpan privateKey) const noexcept;

        [[nodiscard]] CryptoExpected<void> DeriveX25519SharedSecretInto(
                NGIN::Crypto::Memory::SecretView privateKey,
                ConstByteSpan                    peerPublicKey,
                ByteSpan                         output) const noexcept;

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(HashAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(MacAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KdfAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(AeadAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(KeyAgreementAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoExpected<void> EnsureSupports(AsymmetricEncryptionAlgorithm algorithm) const noexcept
        {
            return Supports(algorithm) ? CryptoExpected<void> {} : CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

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
    [[nodiscard]] CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options = {}) noexcept;

    /// @brief Creates a context and returns diagnostics for rejected candidate providers.
    [[nodiscard]] BackendContextSelection CreateContextWithDiagnostics(const BackendOptions& options = {}) noexcept;

    /// @brief Creates a context from the strongest compiled provider that satisfies the default policy.
    [[nodiscard]] CryptoExpected<CryptoContext> CreateBestAvailableContext() noexcept;

    /// @brief Creates a context backed only by platform facilities.
    [[nodiscard]] CryptoExpected<CryptoContext> CreatePlatformContext() noexcept;

    /// @brief Creates a context backed by a named package provider such as "openssl".
    [[nodiscard]] CryptoExpected<CryptoContext> CreatePackageContext(std::string_view packageName) noexcept;
}// namespace NGIN::Crypto::Backend
