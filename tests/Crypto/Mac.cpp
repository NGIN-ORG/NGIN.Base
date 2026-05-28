#include <NGIN/Crypto/Mac/HmacSha256.hpp>
#include <NGIN/Crypto/Mac/HmacSha512.hpp>
#include <NGIN/Crypto/Mac/Mac.hpp>

#include <NGIN/Crypto/Encoding/Hex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string_view>

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

    [[nodiscard]] constexpr std::array<NGIN::Byte, 20> Rfc4231TestKey() noexcept
    {
        return {
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
                NGIN::Byte {0x0b},
        };
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        for (NGIN::UIntSize i = 0; i < actual.size(); ++i)
        {
            REQUIRE(actual[i] == expected[i]);
        }
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
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Mac::ComputeMac(
            context,
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            TestKey(),
            NGIN::Crypto::ConstByteSpan {});

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("HMAC computes RFC 4231 vectors when backend supports them", "[Crypto][Mac]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    if (!context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha256) || !context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha512))
    {
        SUCCEED("HMAC vector test requires a MAC-capable backend such as optional OpenSSL.");
        return;
    }

    constexpr std::string_view MESSAGE {"Hi There"};
    const auto                 key   = Rfc4231TestKey();
    const auto                 input = std::as_bytes(std::span {MESSAGE.data(), MESSAGE.size()});

    auto hmacSha256 = NGIN::Crypto::Mac::HmacSha256(
            context.Value(),
            NGIN::Crypto::Memory::SecretView {key},
            input);
    auto hmacSha512 = NGIN::Crypto::Mac::HmacSha512(
            context.Value(),
            NGIN::Crypto::Memory::SecretView {key},
            input);

    auto expectedSha256 = NGIN::Crypto::Encoding::DecodeHex(
            "b0344c61d8db38535ca8afceaf0bf12b"
            "881dc200c9833da726e9376c2e32cff7");
    auto expectedSha512 = NGIN::Crypto::Encoding::DecodeHex(
            "87aa7cdea5ef619d4ff0b4241a1d6cb"
            "02379f4e2ce4ec2787ad0b30545e17cde"
            "daa833b7d6b8a702038b274eaea3f4e4"
            "be9d914eeb61f1702e696c203a126854");

    REQUIRE(hmacSha256.HasValue());
    REQUIRE(hmacSha512.HasValue());
    REQUIRE(expectedSha256.HasValue());
    REQUIRE(expectedSha512.HasValue());

    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {hmacSha256.Value().data(), hmacSha256.Value().size()},
            NGIN::Crypto::ConstByteSpan {expectedSha256.Value().data(), expectedSha256.Value().Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {hmacSha512.Value().data(), hmacSha512.Value().size()},
            NGIN::Crypto::ConstByteSpan {expectedSha512.Value().data(), expectedSha512.Value().Size()});

    auto verified = NGIN::Crypto::Mac::VerifyMac(
            context.Value(),
            NGIN::Crypto::MacAlgorithm::HmacSha256,
            NGIN::Crypto::Memory::SecretView {key},
            input,
            NGIN::Crypto::ConstByteSpan {hmacSha256.Value().data(), hmacSha256.Value().size()});

    REQUIRE(verified.HasValue());
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
