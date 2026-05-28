#include <NGIN/Crypto/Kdf/Argon2id.hpp>
#include <NGIN/Crypto/Kdf/Hkdf.hpp>
#include <NGIN/Crypto/Kdf/Pbkdf2.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace
{
    [[nodiscard]] NGIN::Crypto::Memory::SecretView TestSecret() noexcept
    {
        static constexpr std::array<NGIN::Byte, 4> SECRET {
                NGIN::Byte {0x01},
                NGIN::Byte {0x02},
                NGIN::Byte {0x03},
                NGIN::Byte {0x04},
        };
        return NGIN::Crypto::Memory::SecretView {SECRET};
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan TestSalt() noexcept
    {
        static constexpr std::array<NGIN::Byte, 4> SALT {
                NGIN::Byte {0x0a},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0c},
                NGIN::Byte {0x0d},
        };
        return SALT;
    }
}// namespace

TEST_CASE("KeyDerivationParameters tags HKDF algorithms", "[Crypto][Kdf]")
{
    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = TestSecret(),
            .salt             = TestSalt(),
            .info             = NGIN::Crypto::ConstByteSpan {},
    };

    NGIN::Crypto::Kdf::KeyDerivationParameters parameters {
            NGIN::Crypto::KdfAlgorithm::HkdfSha512,
            hkdf,
    };

    REQUIRE(parameters.Algorithm() == NGIN::Crypto::KdfAlgorithm::HkdfSha512);
    REQUIRE(parameters.Hkdf() == &hkdf);
    REQUIRE(parameters.Pbkdf2() == nullptr);
    REQUIRE(parameters.Argon2id() == nullptr);
}

TEST_CASE("DeriveKeyInto rejects empty output", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = TestSecret(),
            .salt             = TestSalt(),
            .info             = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Kdf::HkdfSha256Into(context.Value(), hkdf, NGIN::Crypto::ByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("DeriveKeyInto rejects invalid PBKDF2 parameters", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    NGIN::Crypto::Kdf::Pbkdf2Parameters pbkdf2 {
            .password   = TestSecret(),
            .salt       = TestSalt(),
            .iterations = 0,
    };
    std::array<NGIN::Byte, 16> output {};

    auto result = NGIN::Crypto::Kdf::Pbkdf2Sha256Into(context.Value(), pbkdf2, output);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("DeriveKeyInto rejects invalid Argon2id parameters", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    NGIN::Crypto::Kdf::Argon2idParameters argon2id {
            .password    = TestSecret(),
            .salt        = TestSalt(),
            .memoryKiB   = 0,
            .iterations  = 1,
            .parallelism = 1,
    };
    std::array<NGIN::Byte, 16> output {};

    auto result = NGIN::Crypto::Kdf::Argon2idInto(context.Value(), argon2id, output);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("DeriveKey returns unsupported algorithm when context lacks capability", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = TestSecret(),
            .salt             = TestSalt(),
            .info             = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Kdf::DeriveKey(
            context.Value(),
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::HkdfSha256, hkdf},
            32);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("KDF contract does not fake implementation even if capability is manually enabled", "[Crypto][Kdf]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::KdfAlgorithm::HkdfSha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "kdf-capable-test"},
            capabilities,
    };
    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = TestSecret(),
            .salt             = TestSalt(),
            .info             = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Kdf::DeriveFixedSecret<32>(
            context,
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::HkdfSha256, hkdf});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}
