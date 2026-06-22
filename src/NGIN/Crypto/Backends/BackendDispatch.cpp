#include <NGIN/Crypto/Backend/CryptoContext.hpp>

#include <NGIN/Crypto/Random/SecureRandom.hpp>

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
#include "CngBackend.hpp"
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_APPLE)
#include "AppleBackend.hpp"
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
#include "OpenSslBackend.hpp"
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
#include "LibsodiumBackend.hpp"
#endif

namespace NGIN::Crypto::Backend
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedBackend() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedBackend};
        }

        [[nodiscard]] constexpr CryptoError BackendUnavailable() noexcept
        {
            return CryptoError {CryptoErrorCode::BackendUnavailable};
        }

        [[nodiscard]] constexpr CryptoError PolicyRejected() noexcept
        {
            return CryptoError {CryptoErrorCode::PolicyRejected};
        }

        [[nodiscard]] constexpr BackendInfo PlatformRandomInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::Platform,
                    "platform-random",
                    {},
                    "OS secure random",
                    "always",
            };
        }

        [[nodiscard]] constexpr BackendInfo OpenSslInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::ExternalPackage,
                    "openssl",
                    {},
                    "OpenSSL libcrypto",
                    "NGIN_BASE_CRYPTO_WITH_OPENSSL",
                    "openssl",
            };
        }

        [[nodiscard]] constexpr BackendInfo BoringSslInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::ExternalPackage,
                    "boringssl",
                    {},
                    "BoringSSL libcrypto",
                    "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                    "BoringSSL",
            };
        }

        [[nodiscard]] constexpr BackendInfo LibsodiumInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::ExternalPackage,
                    "libsodium",
                    {},
                    "libsodium",
                    "NGIN_BASE_CRYPTO_WITH_LIBSODIUM",
                    "libsodium",
            };
        }

        [[nodiscard]] constexpr BackendInfo CngInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::Platform,
                    "cng",
                    {},
                    "Windows CNG BCrypt",
                    "NGIN_BASE_CRYPTO_WITH_CNG",
            };
        }

        [[nodiscard]] constexpr BackendInfo AppleInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::Platform,
                    "apple",
                    {},
                    "Apple CommonCrypto/Security",
                    "NGIN_BASE_CRYPTO_WITH_APPLE",
            };
        }

        [[nodiscard]] constexpr BackendInfo UnknownPackageInfo() noexcept
        {
            return BackendInfo {
                    BackendKind::ExternalPackage,
                    "unknown-package",
            };
        }

        [[nodiscard]] constexpr bool IsKnownPackageName(std::string_view packageName) noexcept
        {
            return packageName == "openssl" || packageName == "boringssl" || packageName == "libsodium";
        }

        [[nodiscard]] constexpr bool IsOpenSslCompatiblePackage(const CryptoContext& context) noexcept
        {
            return context.Info().Kind() == BackendKind::ExternalPackage &&
                   (context.Info().Name() == "openssl" || context.Info().Name() == "boringssl");
        }

        [[nodiscard]] constexpr BackendInfo KnownPackageInfo(std::string_view packageName) noexcept
        {
            if (packageName == "openssl")
            {
                return OpenSslInfo();
            }
            if (packageName == "boringssl")
            {
                return BoringSslInfo();
            }
            if (packageName == "libsodium")
            {
                return LibsodiumInfo();
            }

            return UnknownPackageInfo();
        }

        [[nodiscard]] constexpr std::string_view RequirementFailureReason(
                const CryptoContext&  context,
                const BackendOptions& options) noexcept
        {
            if (options.requireSecureRandom && !context.SupportsRandom())
            {
                return "required secure random is unavailable";
            }

            if (options.policy == BackendPolicy::RequireFipsCapable && !context.Info().IsFipsCapable())
            {
                return "backend is not FIPS capable";
            }

            return "required algorithm set is not satisfied";
        }

        [[nodiscard]] constexpr std::string_view CandidateFailureReason(CryptoErrorCode code) noexcept
        {
            switch (code)
            {
                case CryptoErrorCode::UnsupportedAlgorithm:
                    return "required algorithm set is not satisfied";
                case CryptoErrorCode::UnsupportedBackend:
                    return "backend is not recognized by this build";
                case CryptoErrorCode::BackendUnavailable:
                    return "backend is unavailable in this build or runtime";
                case CryptoErrorCode::PolicyRejected:
                    return "backend selection policy rejected this candidate";
                default:
                    return "backend candidate failed during context creation";
            }
        }

        constexpr void AddDiagnostic(
                BackendSelectionDiagnostics* diagnostics,
                BackendInfo                  backend,
                CryptoError                  error,
                std::string_view             reason) noexcept
        {
            if (diagnostics == nullptr)
            {
                return;
            }

            diagnostics->Add(BackendSelectionDiagnostic {
                    .backend = backend,
                    .code    = error.Code(),
                    .reason  = reason,
            });
        }

        [[nodiscard]] CryptoExpected<CryptoContext> MakePlatformRandomContext() noexcept
        {
            if (!NGIN::Crypto::Random::IsAvailable())
            {
                return BackendUnavailable();
            }

            BackendCapabilities capabilities;
            capabilities.EnableRandom();

            return CryptoContext {
                    PlatformRandomInfo(),
                    capabilities,
            };
        }

        [[nodiscard]] constexpr bool MeetsRequirements(
                const CryptoContext&  context,
                const BackendOptions& options) noexcept
        {
            return (!options.requireSecureRandom || context.SupportsRandom()) &&
                   options.requiredAlgorithms.IsSatisfiedBy(context.Capabilities()) &&
                   (options.policy != BackendPolicy::RequireFipsCapable || context.Info().IsFipsCapable());
        }

        [[nodiscard]] constexpr CryptoError RequirementFailure(
                const CryptoContext&  context,
                const BackendOptions& options) noexcept
        {
            if (options.requireSecureRandom && !context.SupportsRandom())
            {
                return BackendUnavailable();
            }

            if (options.policy == BackendPolicy::RequireFipsCapable && !context.Info().IsFipsCapable())
            {
                return PolicyRejected();
            }

            return UnsupportedAlgorithm();
        }

        [[nodiscard]] CryptoExpected<CryptoContext> SelectIfUsable(
                CryptoExpected<CryptoContext> candidate,
                const BackendOptions&         options,
                BackendSelectionDiagnostics*  diagnostics  = nullptr,
                BackendInfo                   fallbackInfo = {}) noexcept
        {
            if (!candidate.HasValue())
            {
                AddDiagnostic(diagnostics, fallbackInfo, candidate.Error(), CandidateFailureReason(candidate.Error().Code()));
                return candidate;
            }

            if (!MeetsRequirements(candidate.Value(), options))
            {
                auto error = RequirementFailure(candidate.Value(), options);
                AddDiagnostic(
                        diagnostics,
                        candidate.Value().Info(),
                        error,
                        RequirementFailureReason(candidate.Value(), options));
                return error;
            }

            return candidate.Value();
        }

        [[nodiscard]] CryptoExpected<CryptoContext> MakeNamedPackageContext(
                std::string_view      packageName,
                const BackendOptions& options) noexcept
        {
            if (packageName == "openssl")
            {
#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
                return detail::CreateOpenSslContext(options);
#else
                (void) options;
                return BackendUnavailable();
#endif
            }

            if (packageName == "libsodium")
            {
#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
                return detail::CreateLibsodiumContext(options);
#else
                (void) options;
                return BackendUnavailable();
#endif
            }

            if (packageName == "boringssl")
            {
#if defined(NGIN_BASE_CRYPTO_HAS_BORINGSSL)
                return detail::CreateBoringSslContext(options);
#else
                (void) options;
                return BackendUnavailable();
#endif
            }

            return UnsupportedBackend();
        }

        [[nodiscard]] CryptoExpected<CryptoContext> MakeAnyPackageContext(const BackendOptions& options) noexcept
        {
            if (!options.packageName.empty())
            {
                return MakeNamedPackageContext(options.packageName, options);
            }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
            return detail::CreateOpenSslContext(options);
#elif defined(NGIN_BASE_CRYPTO_HAS_BORINGSSL)
            return detail::CreateBoringSslContext(options);
#elif defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
            return detail::CreateLibsodiumContext(options);
#else
            return BackendUnavailable();
#endif
        }

        [[nodiscard]] CryptoExpected<CryptoContext> SelectPlatform(
                const BackendOptions&        options,
                BackendSelectionDiagnostics* diagnostics = nullptr) noexcept
        {
#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
            auto cng = SelectIfUsable(detail::CreateCngContext(options), options, diagnostics, CngInfo());
            if (cng.HasValue())
            {
                return cng;
            }
#endif
#if defined(NGIN_BASE_CRYPTO_HAS_APPLE)
            auto apple = SelectIfUsable(detail::CreateAppleContext(options), options, diagnostics, AppleInfo());
            if (apple.HasValue())
            {
                return apple;
            }
#endif
            return SelectIfUsable(MakePlatformRandomContext(), options, diagnostics, PlatformRandomInfo());
        }

        [[nodiscard]] CryptoExpected<CryptoContext> SelectPackage(
                const BackendOptions&        options,
                BackendSelectionDiagnostics* diagnostics = nullptr) noexcept
        {
            auto fallback = !options.packageName.empty() ? KnownPackageInfo(options.packageName) : OpenSslInfo();
            if (!options.packageName.empty() && !IsKnownPackageName(options.packageName))
            {
                fallback = UnknownPackageInfo();
            }
            if (!options.packageName.empty())
            {
                return SelectIfUsable(MakeAnyPackageContext(options), options, diagnostics, fallback);
            }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL)
            auto openssl = SelectIfUsable(detail::CreateOpenSslContext(options), options, diagnostics, OpenSslInfo());
            if (openssl.HasValue())
            {
                return openssl;
            }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_BORINGSSL)
            auto boringssl =
                    SelectIfUsable(detail::CreateBoringSslContext(options), options, diagnostics, BoringSslInfo());
            if (boringssl.HasValue())
            {
                return boringssl;
            }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
            auto libsodium =
                    SelectIfUsable(detail::CreateLibsodiumContext(options), options, diagnostics, LibsodiumInfo());
            if (libsodium.HasValue())
            {
                return libsodium;
            }
#endif

            return SelectIfUsable(MakeAnyPackageContext(options), options, diagnostics, fallback);
        }
    }// namespace

    CryptoExpected<void> CryptoContext::FillRandom(ByteSpan output) const noexcept
    {
        if (!SupportsRandom())
        {
            return CryptoError {CryptoErrorCode::UnsupportedBackend};
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::RandomLibsodium(output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::RandomOpenSsl(output);
        }
#endif

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

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "cng")
        {
            return detail::HashCng(algorithm, input, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_APPLE)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "apple")
        {
            return detail::HashApple(algorithm, input, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::HashOpenSsl(algorithm, input, output);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_CNG) && !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
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

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "cng")
        {
            return detail::MacCng(algorithm, key, input, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_APPLE)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "apple")
        {
            return detail::MacApple(algorithm, key, input, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::MacOpenSsl(algorithm, key, input, output);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_CNG) && !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
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

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
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

    CryptoExpected<void> CryptoContext::Pbkdf2Into(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "cng")
        {
            return detail::Pbkdf2Cng(algorithm, password, salt, iterations, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_APPLE)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "apple")
        {
            return detail::Pbkdf2Apple(algorithm, password, salt, iterations, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::Pbkdf2OpenSsl(algorithm, password, salt, iterations, output);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_CNG) && !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        (void) password;
        (void) salt;
        (void) iterations;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::Argon2idInto(
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::Argon2idLibsodium(password, salt, memoryKiB, iterations, parallelism, output);
        }
#else
        (void) password;
        (void) salt;
        (void) memoryKiB;
        (void) iterations;
        (void) parallelism;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<std::string> CryptoContext::HashPassword(
            NGIN::Crypto::Memory::SecretView password,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism) const
    {
        auto supported = EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::HashPasswordLibsodium(password, memoryKiB, iterations, parallelism);
        }
#else
        (void) password;
        (void) memoryKiB;
        (void) iterations;
        (void) parallelism;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::VerifyPasswordHash(
            NGIN::Crypto::Memory::SecretView password,
            std::string_view                 encodedHash) const noexcept
    {
        auto supported = EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::VerifyPasswordHashLibsodium(password, encodedHash);
        }
#else
        (void) password;
        (void) encodedHash;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<bool> CryptoContext::PasswordHashNeedsRehash(
            std::string_view encodedHash,
            NGIN::UInt32     memoryKiB,
            NGIN::UInt32     iterations,
            NGIN::UInt32     parallelism) const noexcept
    {
        auto supported = EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::PasswordHashNeedsRehashLibsodium(encodedHash, memoryKiB, iterations, parallelism);
        }
#else
        (void) encodedHash;
        (void) memoryKiB;
        (void) iterations;
        (void) parallelism;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::AeadSealInto(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "cng")
        {
            return detail::AeadSealCng(algorithm, key, nonce, plaintext, associatedData, ciphertext, tag);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::AeadSealOpenSsl(algorithm, key, nonce, plaintext, associatedData, ciphertext, tag);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::AeadSealLibsodium(algorithm, key, nonce, plaintext, associatedData, ciphertext, tag);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_CNG) && !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) key;
        (void) nonce;
        (void) plaintext;
        (void) associatedData;
        (void) ciphertext;
        (void) tag;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::AeadOpenInto(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_CNG)
        if (Info().Kind() == BackendKind::Platform && Info().Name() == "cng")
        {
            return detail::AeadOpenCng(algorithm, key, nonce, ciphertext, associatedData, tag, plaintext);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::AeadOpenOpenSsl(algorithm, key, nonce, ciphertext, associatedData, tag, plaintext);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::AeadOpenLibsodium(algorithm, key, nonce, ciphertext, associatedData, tag, plaintext);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_CNG) && !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) key;
        (void) nonce;
        (void) ciphertext;
        (void) associatedData;
        (void) tag;
        (void) plaintext;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::GenerateEd25519KeyPairInto(
            ByteSpan publicKey,
            ByteSpan privateKey) const noexcept
    {
        auto supported = EnsureSupports(SignatureAlgorithm::Ed25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::GenerateEd25519KeyPairOpenSsl(publicKey, privateKey);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::GenerateEd25519KeyPairLibsodium(publicKey, privateKey);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) publicKey;
        (void) privateKey;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::SignInto(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::SignOpenSsl(algorithm, privateKey, message, signature);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::SignLibsodium(algorithm, privateKey, message, signature);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) privateKey;
        (void) message;
        (void) signature;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::VerifySignature(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) const noexcept
    {
        auto supported = EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::VerifySignatureOpenSsl(algorithm, publicKey, message, signature);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::VerifySignatureLibsodium(algorithm, publicKey, message, signature);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) publicKey;
        (void) message;
        (void) signature;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<ByteBuffer> CryptoContext::RsaPssSha256Sign(
            NGIN::Crypto::Memory::SecretView privateKeyDer,
            ConstByteSpan                    message) const
    {
        auto supported = EnsureSupports(SignatureAlgorithm::RsaPssSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::RsaPssSha256SignOpenSsl(privateKeyDer, message);
        }
#else
        (void) privateKeyDer;
        (void) message;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::RsaPssSha256Verify(
            ConstByteSpan publicKeyDer,
            ConstByteSpan message,
            ConstByteSpan signature) const noexcept
    {
        auto supported = EnsureSupports(SignatureAlgorithm::RsaPssSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::RsaPssSha256VerifyOpenSsl(publicKeyDer, message, signature);
        }
#else
        (void) publicKeyDer;
        (void) message;
        (void) signature;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<ByteBuffer> CryptoContext::RsaOaepSha256Encrypt(
            ConstByteSpan publicKeyDer,
            ConstByteSpan plaintext,
            ConstByteSpan label) const
    {
        auto supported = EnsureSupports(AsymmetricEncryptionAlgorithm::RsaOaepSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::RsaOaepSha256EncryptOpenSsl(publicKeyDer, plaintext, label);
        }
#else
        (void) publicKeyDer;
        (void) plaintext;
        (void) label;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<ByteBuffer> CryptoContext::RsaOaepSha256Decrypt(
            NGIN::Crypto::Memory::SecretView privateKeyDer,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    label) const
    {
        auto supported = EnsureSupports(AsymmetricEncryptionAlgorithm::RsaOaepSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::RsaOaepSha256DecryptOpenSsl(privateKeyDer, ciphertext, label);
        }
#else
        (void) privateKeyDer;
        (void) ciphertext;
        (void) label;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::GenerateX25519KeyPairInto(
            ByteSpan publicKey,
            ByteSpan privateKey) const noexcept
    {
        auto supported = EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::GenerateX25519KeyPairOpenSsl(publicKey, privateKey);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::GenerateX25519KeyPairLibsodium(publicKey, privateKey);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) publicKey;
        (void) privateKey;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> CryptoContext::DeriveX25519SharedSecretInto(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) const noexcept
    {
        auto supported = EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
        if (IsOpenSslCompatiblePackage(*this))
        {
            return detail::DeriveX25519SharedSecretOpenSsl(privateKey, peerPublicKey, output);
        }
#endif

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        if (Info().Kind() == BackendKind::ExternalPackage && Info().Name() == "libsodium")
        {
            return detail::DeriveX25519SharedSecretLibsodium(privateKey, peerPublicKey, output);
        }
#endif

#if !defined(NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT) && !defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        (void) privateKey;
        (void) peerPublicKey;
        (void) output;
#endif

        return UnsupportedAlgorithm();
    }

    CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options) noexcept
    {
        return CreateContextWithDiagnostics(options).context;
    }

    BackendContextSelection CreateContextWithDiagnostics(const BackendOptions& options) noexcept
    {
        BackendSelectionDiagnostics diagnostics;

        switch (options.policy)
        {
            case BackendPolicy::PlatformOnly:
                return BackendContextSelection {
                        .context     = SelectPlatform(options, &diagnostics),
                        .diagnostics = diagnostics,
                };
            case BackendPolicy::PackagesOnly:
                return BackendContextSelection {
                        .context     = SelectPackage(options, &diagnostics),
                        .diagnostics = diagnostics,
                };
            case BackendPolicy::PreferPlatformThenPackages: {
                auto platform = SelectPlatform(options, &diagnostics);
                if (platform.HasValue())
                {
                    return BackendContextSelection {
                            .context     = platform,
                            .diagnostics = diagnostics,
                    };
                }
                auto package = SelectPackage(options, &diagnostics);
                if (package.HasValue())
                {
                    return BackendContextSelection {
                            .context     = package,
                            .diagnostics = diagnostics,
                    };
                }
                return BackendContextSelection {
                        .context     = platform.Error(),
                        .diagnostics = diagnostics,
                };
            }
            case BackendPolicy::PreferPackagesThenPlatform:
            case BackendPolicy::RequireAlgorithmSet: {
                auto package = SelectPackage(options, &diagnostics);
                if (package.HasValue())
                {
                    return BackendContextSelection {
                            .context     = package,
                            .diagnostics = diagnostics,
                    };
                }
                auto platform = SelectPlatform(options, &diagnostics);
                if (platform.HasValue())
                {
                    return BackendContextSelection {
                            .context     = platform,
                            .diagnostics = diagnostics,
                    };
                }
                if (platform.Error().Code() == CryptoErrorCode::UnsupportedAlgorithm)
                {
                    return BackendContextSelection {
                            .context     = platform.Error(),
                            .diagnostics = diagnostics,
                    };
                }
                return BackendContextSelection {
                        .context     = package.Error(),
                        .diagnostics = diagnostics,
                };
            }
            case BackendPolicy::RequireFipsCapable: {
                auto package = SelectPackage(options, &diagnostics);
                if (package.HasValue())
                {
                    return BackendContextSelection {
                            .context     = package,
                            .diagnostics = diagnostics,
                    };
                }
                auto platform = SelectPlatform(options, &diagnostics);
                if (platform.HasValue())
                {
                    return BackendContextSelection {
                            .context     = platform,
                            .diagnostics = diagnostics,
                    };
                }
                return BackendContextSelection {
                        .context     = PolicyRejected(),
                        .diagnostics = diagnostics,
                };
            }
        }

        return BackendContextSelection {
                .context     = PolicyRejected(),
                .diagnostics = diagnostics,
        };
    }

    CryptoExpected<CryptoContext> CreateBestAvailableContext() noexcept
    {
        return CreateContext();
    }

    CryptoExpected<CryptoContext> CreatePlatformContext() noexcept
    {
        BackendOptions options;
        options.policy = BackendPolicy::PlatformOnly;
        return CreateContext(options);
    }

    CryptoExpected<CryptoContext> CreatePackageContext(std::string_view packageName) noexcept
    {
        BackendOptions options;
        options.policy      = BackendPolicy::PackagesOnly;
        options.packageName = packageName;
        return CreateContext(options);
    }
}// namespace NGIN::Crypto::Backend
