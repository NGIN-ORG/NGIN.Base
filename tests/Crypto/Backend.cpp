#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
    void RequireCngPlatformContext(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::Platform);
        REQUIRE(context.Info().Name() == "cng");
        REQUIRE(context.Info().Source() == "Windows CNG BCrypt");
        REQUIRE(context.Info().BuildOption() == "NGIN_BASE_CRYPTO_WITH_CNG");
        REQUIRE(context.SupportsRandom());
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha512));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha512));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512));
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes128Gcm));
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha256));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519));
    }

    void RequireApplePlatformContext(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::Platform);
        REQUIRE(context.Info().Name() == "apple");
        REQUIRE(context.Info().Source() == "Apple CommonCrypto/Security");
        REQUIRE(context.Info().BuildOption() == "NGIN_BASE_CRYPTO_WITH_APPLE");
        REQUIRE(context.SupportsRandom());
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha512));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha512));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha256));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes128Gcm));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519));
    }

    void RequireLibsodiumPackageContext(const NGIN::Crypto::Backend::CryptoContext& context)
    {
        REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::ExternalPackage);
        REQUIRE(context.Info().Name() == "libsodium");
        REQUIRE(context.Info().Source() == "libsodium");
        REQUIRE(context.Info().BuildOption() == "NGIN_BASE_CRYPTO_WITH_LIBSODIUM");
        REQUIRE(context.Info().PackageName() == "libsodium");
        REQUIRE(context.SupportsRandom());
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::XChaCha20Poly1305));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Argon2id));
        REQUIRE(context.Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519));
        REQUIRE(context.Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
        REQUIRE_FALSE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));
    }

    void RequireOpenSslCompatiblePackageContext(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            name,
            std::string_view                            source,
            std::string_view                            buildOption,
            std::string_view                            packageName)
    {
        REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::ExternalPackage);
        REQUIRE(context.Info().Name() == name);
        REQUIRE(context.Info().Source() == source);
        REQUIRE(context.Info().BuildOption() == buildOption);
        REQUIRE(context.Info().PackageName() == packageName);
        REQUIRE(context.SupportsRandom());
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::HashAlgorithm::Sha512));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
        REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha512));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha256));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha512));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256));
        REQUIRE(context.Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512));
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes128Gcm));
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));
        REQUIRE(context.Supports(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305));
        REQUIRE(context.Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519));
        REQUIRE(context.Supports(NGIN::Crypto::KeyAgreementAlgorithm::X25519));
    }
}// namespace

TEST_CASE("BackendCapabilities tracks neutral algorithm support", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;

    REQUIRE_FALSE(capabilities.SupportsRandom());
    REQUIRE_FALSE(capabilities.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
    REQUIRE_FALSE(capabilities.Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));

    capabilities.EnableRandom()
            .Enable(NGIN::Crypto::HashAlgorithm::Sha256)
            .Enable(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

    REQUIRE(capabilities.SupportsRandom());
    REQUIRE(capabilities.Supports(NGIN::Crypto::HashAlgorithm::Sha256));
    REQUIRE_FALSE(capabilities.Supports(NGIN::Crypto::HashAlgorithm::Sha512));
    REQUIRE(capabilities.Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));
    REQUIRE_FALSE(capabilities.Supports(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305));
}

TEST_CASE("AlgorithmSet tracks startup requirements", "[Crypto][Backend]")
{
    auto requirements = NGIN::Crypto::Backend::AlgorithmSet {}
                                .RequireRandom()
                                .Require(NGIN::Crypto::HashAlgorithm::Sha256)
                                .Require(NGIN::Crypto::MacAlgorithm::HmacSha256)
                                .Require(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

    NGIN::Crypto::Backend::BackendCapabilities partial;
    partial.EnableRandom().Enable(NGIN::Crypto::HashAlgorithm::Sha256);

    REQUIRE(requirements.RequiresRandom());
    REQUIRE(requirements.Requires(NGIN::Crypto::HashAlgorithm::Sha256));
    REQUIRE(requirements.Requires(NGIN::Crypto::MacAlgorithm::HmacSha256));
    REQUIRE(requirements.Requires(NGIN::Crypto::AeadAlgorithm::Aes256Gcm));
    REQUIRE_FALSE(requirements.IsSatisfiedBy(partial));

    partial.Enable(NGIN::Crypto::MacAlgorithm::HmacSha256).Enable(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

    REQUIRE(requirements.IsSatisfiedBy(partial));
}

TEST_CASE("CryptoContext exposes backend info and capabilities", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::MacAlgorithm::HmacSha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {
                    NGIN::Crypto::Backend::BackendKind::Test,
                    "fake-test",
                    "1",
                    "test-provider",
                    "NGIN_TEST_BACKEND",
                    "test-package",
                    true,
                    false,
            },
            capabilities,
    };

    REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::Test);
    REQUIRE(context.Info().Name() == "fake-test");
    REQUIRE(context.Info().Version() == "1");
    REQUIRE(context.Info().Source() == "test-provider");
    REQUIRE(context.Info().BuildOption() == "NGIN_TEST_BACKEND");
    REQUIRE(context.Info().PackageName() == "test-package");
    REQUIRE(context.Info().IsFipsCapable());
    REQUIRE_FALSE(context.Info().IsFipsValidated());
    REQUIRE(context.Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
    REQUIRE_FALSE(context.SupportsRandom());
}

TEST_CASE("CryptoContext reports unsupported algorithms as values", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = context.EnsureSupports(NGIN::Crypto::HashAlgorithm::Sha256);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("CryptoContext describes algorithm support for diagnostics", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::HashAlgorithm::Sha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "diagnostic-test"},
            capabilities,
    };

    auto supported = context.DescribeSupport(NGIN::Crypto::HashAlgorithm::Sha256);
    REQUIRE(supported.supported);
    REQUIRE(supported.reason == "supported");

    auto unsupported = context.DescribeSupport(NGIN::Crypto::HashAlgorithm::Sha512);
    REQUIRE_FALSE(unsupported.supported);
    REQUIRE_FALSE(unsupported.reason.empty());
}

TEST_CASE("CreateContext returns configured default backend context", "[Crypto][Backend]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();

    REQUIRE(context.HasValue());
    REQUIRE(context.Value().SupportsRandom());

    if (context.Value().Info().Name() == "openssl")
    {
        RequireOpenSslCompatiblePackageContext(
                context.Value(),
                "openssl",
                "OpenSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_OPENSSL",
                "openssl");
    }
    else if (context.Value().Info().Name() == "boringssl")
    {
        RequireOpenSslCompatiblePackageContext(
                context.Value(),
                "boringssl",
                "BoringSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                "BoringSSL");
    }
    else if (context.Value().Info().Name() == "libsodium")
    {
        RequireLibsodiumPackageContext(context.Value());
    }
    else
    {
        REQUIRE(context.Value().Info().Kind() == NGIN::Crypto::Backend::BackendKind::Platform);
        if (context.Value().Info().Name() == "cng")
        {
            RequireCngPlatformContext(context.Value());
        }
        else if (context.Value().Info().Name() == "apple")
        {
            RequireApplePlatformContext(context.Value());
        }
        else
        {
            REQUIRE(context.Value().Info().Name() == "platform-random");
            REQUIRE(context.Value().Info().Source() == "OS secure random");
            REQUIRE(context.Value().Info().BuildOption() == "always");
            REQUIRE_FALSE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256));
            auto sha256 = context.Value().DescribeSupport(NGIN::Crypto::HashAlgorithm::Sha256);
            REQUIRE_FALSE(sha256.supported);
            REQUIRE(sha256.reason == "platform-random provides OS secure random only");
        }
    }
}

TEST_CASE("CreatePlatformContext selects only platform capabilities", "[Crypto][Backend]")
{
    auto context = NGIN::Crypto::Backend::CreatePlatformContext();

    REQUIRE(context.HasValue());
    REQUIRE(context.Value().Info().Kind() == NGIN::Crypto::Backend::BackendKind::Platform);
    if (context.Value().Info().Name() == "cng")
    {
        RequireCngPlatformContext(context.Value());
    }
    else if (context.Value().Info().Name() == "apple")
    {
        RequireApplePlatformContext(context.Value());
    }
    else
    {
        REQUIRE(context.Value().Info().Name() == "platform-random");
        REQUIRE(context.Value().SupportsRandom());
        REQUIRE_FALSE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256));
    }
}

TEST_CASE("CreatePackageContext selects named package provider when available", "[Crypto][Backend]")
{
    auto openssl = NGIN::Crypto::Backend::CreatePackageContext("openssl");

    if (openssl.HasValue())
    {
        RequireOpenSslCompatiblePackageContext(
                openssl.Value(),
                "openssl",
                "OpenSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_OPENSSL",
                "openssl");
    }
    else
    {
        REQUIRE(openssl.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
    }

    auto libsodium = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (libsodium.HasValue())
    {
        RequireLibsodiumPackageContext(libsodium.Value());
    }
    else
    {
        REQUIRE(libsodium.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
    }

    auto boringssl = NGIN::Crypto::Backend::CreatePackageContext("boringssl");
    if (boringssl.HasValue())
    {
        RequireOpenSslCompatiblePackageContext(
                boringssl.Value(),
                "boringssl",
                "BoringSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                "BoringSSL");
    }
    else
    {
        REQUIRE(boringssl.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
    }
}

TEST_CASE("CreatePackageContext rejects unknown package provider names", "[Crypto][Backend]")
{
    auto context = NGIN::Crypto::Backend::CreatePackageContext("missing-provider");

    REQUIRE_FALSE(context.HasValue());
    REQUIRE(context.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedBackend);
}

TEST_CASE("CreatePackageContext reports unavailable or configured BoringSSL package provider", "[Crypto][Backend]")
{
    auto boringssl = NGIN::Crypto::Backend::CreatePackageContext("boringssl");

    if (boringssl.HasValue())
    {
        RequireOpenSslCompatiblePackageContext(
                boringssl.Value(),
                "boringssl",
                "BoringSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                "BoringSSL");
    }
    else
    {
        REQUIRE(boringssl.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
    }
}

TEST_CASE("Backend policy can require an algorithm set at startup", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy = NGIN::Crypto::Backend::BackendPolicy::RequireAlgorithmSet;
    options.requiredAlgorithms.Require(NGIN::Crypto::HashAlgorithm::Sha256)
            .Require(NGIN::Crypto::MacAlgorithm::HmacSha256);

    auto context = NGIN::Crypto::Backend::CreateContext(options);

    if (context.HasValue())
    {
        REQUIRE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE(context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
    }
    else
    {
        REQUIRE(context.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("Backend policy reports unsupported platform algorithm requirements", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy = NGIN::Crypto::Backend::BackendPolicy::PlatformOnly;
    options.requiredAlgorithms.Require(NGIN::Crypto::HashAlgorithm::Sha256);

    auto context = NGIN::Crypto::Backend::CreateContext(options);

    REQUIRE_FALSE(context.HasValue());
    REQUIRE(context.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("CreateContextWithDiagnostics records rejected backend candidates", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy = NGIN::Crypto::Backend::BackendPolicy::RequireAlgorithmSet;
    options.requiredAlgorithms.Require(NGIN::Crypto::HashAlgorithm::Sha3_256);

    auto selection = NGIN::Crypto::Backend::CreateContextWithDiagnostics(options);

    REQUIRE_FALSE(selection.context.HasValue());
    REQUIRE(selection.diagnostics.Count() >= 1);

    bool sawUnsupportedAlgorithm = false;
    for (NGIN::UIntSize i = 0; i < selection.diagnostics.Count(); ++i)
    {
        REQUIRE_FALSE(selection.diagnostics[i].reason.empty());
        sawUnsupportedAlgorithm =
                sawUnsupportedAlgorithm ||
                selection.diagnostics[i].code == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm;
    }
    REQUIRE(sawUnsupportedAlgorithm);
}

TEST_CASE("CreateContextWithDiagnostics records unknown package providers", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy      = NGIN::Crypto::Backend::BackendPolicy::PackagesOnly;
    options.packageName = "missing-provider";

    auto selection = NGIN::Crypto::Backend::CreateContextWithDiagnostics(options);

    REQUIRE_FALSE(selection.context.HasValue());
    REQUIRE(selection.diagnostics.Count() == 1);
    REQUIRE(selection.diagnostics[0].backend.Name() == "unknown-package");
    REQUIRE(selection.diagnostics[0].code == NGIN::Crypto::CryptoErrorCode::UnsupportedBackend);
    REQUIRE_FALSE(selection.diagnostics[0].reason.empty());
}

TEST_CASE("CreateContextWithDiagnostics records unavailable or configured BoringSSL package providers", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy      = NGIN::Crypto::Backend::BackendPolicy::PackagesOnly;
    options.packageName = "boringssl";

    auto selection = NGIN::Crypto::Backend::CreateContextWithDiagnostics(options);

    if (selection.context.HasValue())
    {
        RequireOpenSslCompatiblePackageContext(
                selection.context.Value(),
                "boringssl",
                "BoringSSL libcrypto",
                "NGIN_BASE_CRYPTO_WITH_BORINGSSL",
                "BoringSSL");
    }
    else
    {
        REQUIRE(selection.context.Error().Code() == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
        REQUIRE(selection.diagnostics.Count() == 1);
        REQUIRE(selection.diagnostics[0].backend.Name() == "boringssl");
        REQUIRE(selection.diagnostics[0].backend.PackageName() == "BoringSSL");
        REQUIRE(selection.diagnostics[0].backend.BuildOption() == "NGIN_BASE_CRYPTO_WITH_BORINGSSL");
        REQUIRE(selection.diagnostics[0].code == NGIN::Crypto::CryptoErrorCode::BackendUnavailable);
        REQUIRE_FALSE(selection.diagnostics[0].reason.empty());
    }
}

TEST_CASE("Backend policy rejects FIPS requirement when no capable backend is compiled", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendOptions options;
    options.policy = NGIN::Crypto::Backend::BackendPolicy::RequireFipsCapable;

    auto context = NGIN::Crypto::Backend::CreateContext(options);

    REQUIRE_FALSE(context.HasValue());
    REQUIRE(context.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);
}

TEST_CASE("CryptoContext fills random bytes when capability is present", "[Crypto][Backend]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto bytes = NGIN::Crypto::MakeByteBuffer(16);
    auto fill  = context.Value().FillRandom(NGIN::Crypto::ByteSpan {bytes.data(), bytes.Size()});

    REQUIRE(fill.HasValue());
}

TEST_CASE("CryptoContext rejects random fill without random capability", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto bytes = NGIN::Crypto::MakeByteBuffer(16);
    auto fill  = context.FillRandom(NGIN::Crypto::ByteSpan {bytes.data(), bytes.Size()});

    REQUIRE_FALSE(fill.HasValue());
    REQUIRE(fill.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedBackend);
}
