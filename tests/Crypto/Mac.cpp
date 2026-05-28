#include <NGIN/Crypto/Mac/HmacSha256.hpp>
#include <NGIN/Crypto/Mac/HmacSha512.hpp>
#include <NGIN/Crypto/Mac/Mac.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace
{
    [[nodiscard]] NGIN::Crypto::Memory::SecretView TestKey() noexcept
    {
        static constexpr std::array<NGIN::Byte, 4> KEY {
                NGIN::Byte {0x01},
                NGIN::Byte {0x02},
                NGIN::Byte {0x03},
                NGIN::Byte {0x04},
        };
        return NGIN::Crypto::Memory::SecretView {KEY};
    }
}// namespace

TEST_CASE("MacTagSize reports fixed tag sizes", "[Crypto][Mac]")
{
    REQUIRE(NGIN::Crypto::Mac::MacTagSize(NGIN::Crypto::MacAlgorithm::HmacSha256) == 32);
    REQUIRE(NGIN::Crypto::Mac::MacTagSize(NGIN::Crypto::MacAlgorithm::HmacSha512) == 64);
}

TEST_CASE("HMAC tag aliases expose fixed-size storage", "[Crypto][Mac]")
{
    NGIN::Crypto::Mac::HmacSha256Tag sha256 {};
    NGIN::Crypto::Mac::HmacSha512Tag sha512 {};

    REQUIRE(sha256.size() == 32);
    REQUIRE(sha512.size() == 64);
}

TEST_CASE("MacInto checks output size before backend support", "[Crypto][Mac]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };
    std::array<NGIN::Byte, 31> output {};

    auto result = NGIN::Crypto::Mac::MacInto(
            context,
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            TestKey(),
            NGIN::Crypto::ConstByteSpan {},
            output);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("ComputeMac returns unsupported algorithm when context lacks capability", "[Crypto][Mac]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Mac::ComputeMac(
            context.Value(),
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            TestKey(),
            NGIN::Crypto::ConstByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("MAC contract does not fake implementation even if capability is manually enabled", "[Crypto][Mac]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::MacAlgorithm::HmacSha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "mac-capable-test"},
            capabilities,
    };

    auto result = NGIN::Crypto::Mac::HmacSha256(context, TestKey(), NGIN::Crypto::ConstByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("VerifyMac validates expected tag size", "[Crypto][Mac]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };
    std::array<NGIN::Byte, 31> expectedTag {};

    auto result = NGIN::Crypto::Mac::VerifyMac(
            context,
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            TestKey(),
            NGIN::Crypto::ConstByteSpan {},
            expectedTag);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidTag);
}

TEST_CASE("VerifyMac returns unsupported algorithm without backend implementation", "[Crypto][Mac]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::MacAlgorithm::HmacSha256);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "mac-capable-test"},
            capabilities,
    };
    NGIN::Crypto::Mac::HmacSha256Tag expectedTag {};

    auto result = NGIN::Crypto::Mac::VerifyMac(
            context,
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            TestKey(),
            NGIN::Crypto::ConstByteSpan {},
            expectedTag);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}
