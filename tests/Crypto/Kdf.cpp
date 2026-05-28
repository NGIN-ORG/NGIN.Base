#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Kdf/Argon2id.hpp>
#include <NGIN/Crypto/Kdf/Hkdf.hpp>
#include <NGIN/Crypto/Kdf/Pbkdf2.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

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

    [[nodiscard]] NGIN::Crypto::ByteBuffer DecodeHex(std::string_view text)
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeHex(text);
        REQUIRE(decoded.HasValue());
        return decoded.Value();
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char> {text.data(), text.size()});
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        REQUIRE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
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
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = TestSecret(),
            .salt             = TestSalt(),
            .info             = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Kdf::DeriveKey(
            context,
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::HkdfSha256, hkdf},
            32);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("HKDF-SHA256 matches RFC 5869 test vector when backend supports it", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::KdfAlgorithm::HkdfSha256))
    {
        return;
    }

    const auto ikm = DecodeHex(
            "0b0b0b0b0b0b0b0b0b0b0b"
            "0b0b0b0b0b0b0b0b0b0b0b");
    const auto salt     = DecodeHex("000102030405060708090a0b0c");
    const auto info     = DecodeHex("f0f1f2f3f4f5f6f7f8f9");
    const auto expected = DecodeHex(
            "3cb25f25faacd57a90434f64d0362f2a"
            "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865");

    NGIN::Crypto::Kdf::HkdfParameters hkdf {
            .inputKeyMaterial = NGIN::Crypto::Memory::SecretView {
                    NGIN::Crypto::ConstByteSpan {ikm.data(), ikm.Size()}},
            .salt = NGIN::Crypto::ConstByteSpan {salt.data(), salt.Size()},
            .info = NGIN::Crypto::ConstByteSpan {info.data(), info.Size()},
    };

    auto output = NGIN::Crypto::Kdf::DeriveKey(
            context.Value(),
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::HkdfSha256, hkdf},
            expected.Size());

    REQUIRE(output.HasValue());
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {output.Value().data(), output.Value().Size()},
            NGIN::Crypto::ConstByteSpan {expected.data(), expected.Size()});
}

TEST_CASE("PBKDF2-HMAC-SHA256 and SHA512 match known-answer vectors when backend supports them", "[Crypto][Kdf]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256) ||
        !context.Value().Supports(NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512))
    {
        return;
    }

    const auto expectedSha256 = DecodeHex("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    const auto expectedSha512 = DecodeHex(
            "e1d9c16aa681708a45f5c7c4e215ceb6"
            "6e011a2e9f0040713f18aefdb866d53c"
            "f76cab2868a39b9f7840edce4fef5a82"
            "be67335c77a6068e04112754f27ccf4e");

    NGIN::Crypto::Kdf::Pbkdf2Parameters parameters {
            .password   = NGIN::Crypto::Memory::SecretView {Bytes("password")},
            .salt       = Bytes("salt"),
            .iterations = 2,
    };

    auto sha256Output = NGIN::Crypto::Kdf::DeriveKey(
            context.Value(),
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha256, parameters},
            expectedSha256.Size());
    auto sha512Output = NGIN::Crypto::Kdf::DeriveKey(
            context.Value(),
            NGIN::Crypto::Kdf::KeyDerivationParameters {NGIN::Crypto::KdfAlgorithm::Pbkdf2Sha512, parameters},
            expectedSha512.Size());

    REQUIRE(sha256Output.HasValue());
    REQUIRE(sha512Output.HasValue());
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {sha256Output.Value().data(), sha256Output.Value().Size()},
            NGIN::Crypto::ConstByteSpan {expectedSha256.data(), expectedSha256.Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {sha512Output.Value().data(), sha512Output.Value().Size()},
            NGIN::Crypto::ConstByteSpan {expectedSha512.data(), expectedSha512.Size()});
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
