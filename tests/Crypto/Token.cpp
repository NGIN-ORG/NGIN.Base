#include <NGIN/Crypto/Tokens/TokenGenerator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
    [[nodiscard]] bool IsHex(std::string_view text) noexcept
    {
        for (char character: text)
        {
            const bool digit = character >= '0' && character <= '9';
            const bool lower = character >= 'a' && character <= 'f';
            if (!digit && !lower)
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool IsBase64Url(std::string_view text) noexcept
    {
        for (char character: text)
        {
            const bool upper = character >= 'A' && character <= 'Z';
            const bool lower = character >= 'a' && character <= 'z';
            const bool digit = character >= '0' && character <= '9';
            if (!upper && !lower && !digit && character != '-' && character != '_')
            {
                return false;
            }
        }

        return true;
    }
}// namespace

TEST_CASE("GenerateBytes returns requested random byte count", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto token = NGIN::Crypto::Tokens::GenerateBytes(context.Value(), 32);

    REQUIRE(token.HasValue());
    REQUIRE(token.Value().Size() == 32);
}

TEST_CASE("GenerateHex returns fixed-length lowercase token text", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto token = NGIN::Crypto::Tokens::GenerateHex(
            context.Value(),
            NGIN::Crypto::Tokens::TokenOptions {
                    .byteLength          = 32,
                    .minimumEntropyBytes = 16,
                    .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Hex,
            });

    REQUIRE(token.HasValue());
    REQUIRE(token.Value().Size() == 64);
    REQUIRE(IsHex(token.Value().Value()));
}

TEST_CASE("GenerateBase64Url returns unpadded URL-safe token text", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto token = NGIN::Crypto::Tokens::GenerateBase64Url(
            context.Value(),
            NGIN::Crypto::Tokens::TokenOptions {
                    .byteLength          = 32,
                    .minimumEntropyBytes = 16,
                    .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Base64Url,
            });

    REQUIRE(token.HasValue());
    REQUIRE(token.Value().Size() == 43);
    REQUIRE(IsBase64Url(token.Value().Value()));
}

TEST_CASE("GenerateToken dispatches selected text encoding", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto token = NGIN::Crypto::Tokens::GenerateToken(
            context.Value(),
            NGIN::Crypto::Tokens::TokenOptions {
                    .byteLength          = 16,
                    .minimumEntropyBytes = 16,
                    .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Hex,
            });

    REQUIRE(token.HasValue());
    REQUIRE(token.Value().Size() == 32);
    REQUIRE(IsHex(token.Value().Value()));
}

TEST_CASE("Token generation rejects empty and below-policy sizes", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto empty       = NGIN::Crypto::Tokens::GenerateBytes(context.Value(), 0);
    auto belowPolicy = NGIN::Crypto::Tokens::GenerateBytes(
            context.Value(),
            8,
            16);

    REQUIRE_FALSE(empty.HasValue());
    REQUIRE(empty.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
    REQUIRE_FALSE(belowPolicy.HasValue());
    REQUIRE(belowPolicy.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);
}

TEST_CASE("Token generation reports missing random capability", "[Crypto][Token]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto token = NGIN::Crypto::Tokens::GenerateBytes(context, 32);

    REQUIRE_FALSE(token.HasValue());
    REQUIRE(token.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedBackend);
}
