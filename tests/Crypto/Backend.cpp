#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("CryptoContext exposes backend info and capabilities", "[Crypto][Backend]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::MacAlgorithm::HmacSha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "fake-test", "1"},
            capabilities,
    };

    REQUIRE(context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::Test);
    REQUIRE(context.Info().Name() == "fake-test");
    REQUIRE(context.Info().Version() == "1");
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

TEST_CASE("CreateContext returns configured default backend context", "[Crypto][Backend]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();

    REQUIRE(context.HasValue());
    REQUIRE(context.Value().SupportsRandom());

    if (context.Value().Info().Name() == "openssl")
    {
        REQUIRE(context.Value().Info().Kind() == NGIN::Crypto::Backend::BackendKind::ExternalPackage);
        REQUIRE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256));
        REQUIRE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha512));
        REQUIRE(context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha256));
        REQUIRE(context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha512));
        REQUIRE(context.Value().Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha256));
        REQUIRE(context.Value().Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha512));
    }
    else
    {
        REQUIRE(context.Value().Info().Kind() == NGIN::Crypto::Backend::BackendKind::Platform);
        REQUIRE(context.Value().Info().Name() == "platform-random");
        REQUIRE_FALSE(context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256));
    }
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
