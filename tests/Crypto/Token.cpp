#include "ProviderVectors/SignatureVectors.hpp"

#include <NGIN/Crypto/Tokens/TokenGenerator.hpp>

#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Tokens/Jwt.hpp>
#include <NGIN/Crypto/Tokens/Paseto.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
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

    [[nodiscard]] NGIN::Crypto::ByteBuffer Bytes(std::string_view text)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(text.size());
        for (NGIN::UIntSize i = 0; i < text.size(); ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(text[i]);
        }
        return buffer;
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer HexBytes(std::string_view hex)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(hex.size() / 2);

        auto nibble = [](char character) -> NGIN::UInt8 {
            if (character >= '0' && character <= '9')
            {
                return static_cast<NGIN::UInt8>(character - '0');
            }
            if (character >= 'a' && character <= 'f')
            {
                return static_cast<NGIN::UInt8>(10 + character - 'a');
            }
            if (character >= 'A' && character <= 'F')
            {
                return static_cast<NGIN::UInt8>(10 + character - 'A');
            }
            return 0;
        };

        for (NGIN::UIntSize i = 0; i < buffer.Size(); ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>((nibble(hex[i * 2]) << 4u) | nibble(hex[i * 2 + 1]));
        }

        return buffer;
    }

    [[nodiscard]] std::string Base64UrlText(std::string_view text)
    {
        auto encoded = NGIN::Crypto::Encoding::EncodeBase64Url(Bytes(text));
        REQUIRE(encoded.HasValue());
        return encoded.Value();
    }

    [[nodiscard]] std::string JwtWith(std::string_view header, std::string_view payload, std::string_view signature = "")
    {
        auto token = Base64UrlText(header);
        token.push_back('.');
        token += Base64UrlText(payload);
        token.push_back('.');
        token += Base64UrlText(signature);
        return token;
    }

    [[nodiscard]] std::string PasetoV4PublicWithPayload(std::string_view payload)
    {
        auto combined = NGIN::Crypto::MakeByteBuffer(payload.size() + 64);
        for (NGIN::UIntSize i = 0; i < payload.size(); ++i)
        {
            combined[i] = static_cast<NGIN::Byte>(payload[i]);
        }
        for (NGIN::UIntSize i = 0; i < 64; ++i)
        {
            combined[payload.size() + i] = static_cast<NGIN::Byte>('s');
        }

        auto encoded = NGIN::Crypto::Encoding::EncodeBase64Url(combined);
        REQUIRE(encoded.HasValue());
        return std::string {"v4.public."} + encoded.Value();
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

TEST_CASE("JWT compact parser extracts header, claims, and signature", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJpc3MiOiJuZ2luIiwiYXVkIjpbImFwaSIsImNsaSJdLCJzdWIiOiIxMjMiLCJleHAiOjIwMDAwMDAwMDAsImlhdCI6MTcwMDAwMDAwMH0."
            "c2lnbmF0dXJl"};

    auto parsed = NGIN::Crypto::Tokens::ParseJwtCompact(token);
    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().algorithm == NGIN::Crypto::Tokens::JwtAlgorithm::Hs256);
    REQUIRE(parsed.Value().claims.hasIssuer);
    REQUIRE(parsed.Value().claims.issuer == "ngin");
    REQUIRE(parsed.Value().claims.hasSubject);
    REQUIRE(parsed.Value().claims.subject == "123");
    REQUIRE(parsed.Value().claims.audiences.Size() == 2);
    REQUIRE(parsed.Value().claims.audiences[0] == "api");
    REQUIRE(parsed.Value().claims.audiences[1] == "cli");
    REQUIRE(parsed.Value().claims.hasExpirationTime);
    REQUIRE(parsed.Value().claims.expirationTime == 2000000000);
    REQUIRE(parsed.Value().signature.Size() == 9);
}

TEST_CASE("JWT claim accessors read typed custom claims", "[Crypto][Token]")
{
    auto parsed = NGIN::Crypto::Tokens::ParseJwtCompact(JwtWith(
            R"({"alg":"HS256"})",
            R"({"sub":"123","role":"admin","tenant":42,"enabled":true})",
            "signature"));

    REQUIRE(parsed.HasValue());

    auto hasRole = NGIN::Crypto::Tokens::HasJwtClaim(parsed.Value(), "role");
    REQUIRE(hasRole.HasValue());
    REQUIRE(hasRole.Value());

    auto missing = NGIN::Crypto::Tokens::HasJwtClaim(parsed.Value(), "missing");
    REQUIRE(missing.HasValue());
    REQUIRE_FALSE(missing.Value());

    auto role = NGIN::Crypto::Tokens::GetJwtStringClaim(parsed.Value(), "role");
    REQUIRE(role.HasValue());
    REQUIRE(role.Value() == "admin");

    auto tenant = NGIN::Crypto::Tokens::GetJwtInt64Claim(parsed.Value(), "tenant");
    REQUIRE(tenant.HasValue());
    REQUIRE(tenant.Value() == 42);

    auto enabled = NGIN::Crypto::Tokens::GetJwtBoolClaim(parsed.Value(), "enabled");
    REQUIRE(enabled.HasValue());
    REQUIRE(enabled.Value());

    auto wrongType = NGIN::Crypto::Tokens::GetJwtStringClaim(parsed.Value(), "tenant");
    REQUIRE_FALSE(wrongType.HasValue());
    REQUIRE(wrongType.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);

    auto absent = NGIN::Crypto::Tokens::GetJwtBoolClaim(parsed.Value(), "missing");
    REQUIRE_FALSE(absent.HasValue());
    REQUIRE(absent.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("JWT parser rejects alg none and duplicate fields", "[Crypto][Token]")
{
    auto none = NGIN::Crypto::Tokens::ParseJwtCompact(JwtWith(R"({"alg":"none"})", R"({"sub":"123"})"));
    REQUIRE_FALSE(none.HasValue());
    REQUIRE(none.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);

    auto duplicate = NGIN::Crypto::Tokens::ParseJwtCompact(
            JwtWith(R"({"alg":"HS256","alg":"HS256"})", R"({"sub":"123"})", "sig"));
    REQUIRE_FALSE(duplicate.HasValue());
    REQUIRE(duplicate.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("JWT compact parser malformed corpus rejects invalid envelopes", "[Crypto][Token]")
{
    for (std::string_view token: {
                 "",
                 "a.b",
                 "a.b.c.d",
                 "!!!!.eyJzdWIiOiIxMjMifQ.c2ln",
                 "eyJhbGciOiJIUzI1NiJ9.!!!!.c2ln",
                 "eyJhbGciOiJIUzI1NiJ9.W10.c2ln",
         })
    {
        auto parsed = NGIN::Crypto::Tokens::ParseJwtCompact(token);
        REQUIRE_FALSE(parsed.HasValue());
    }
}

TEST_CASE("JWT validation enforces claim policy before signature verification", "[Crypto][Token]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto token = JwtWith(
            R"({"alg":"HS256"})",
            R"({"iss":"issuer-a","aud":"api","exp":2000000000,"nbf":1900000000})",
            "signature");
    auto secret = Bytes("secret");

    auto result = NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            token,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Hs256,
                    .hmacKey   = NGIN::Crypto::Memory::SecretView {
                            NGIN::Crypto::ConstByteSpan {secret.data(), secret.Size()},
                    },
                    .publicKey = {},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256              = true,
                    .allowEdDsa              = false,
                    .expectedIssuer          = "issuer-b",
                    .expectedAudience        = "api",
                    .currentUnixTimeSeconds  = 1950000000,
                    .allowedClockSkewSeconds = 0,
                    .requireExpiration       = true,
                    .validateExpiration      = true,
                    .validateNotBefore       = true,
                    .requiredClaims          = {"iss", "aud", "exp"},
                    .parseOptions            = {},
            });

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);
}

TEST_CASE("JWT HS256 validation uses backend MAC support when available", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
            "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"};

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    REQUIRE(context.HasValue());

    auto secret = Bytes("your-256-bit-secret");
    auto result = NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            token,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Hs256,
                    .hmacKey   = NGIN::Crypto::Memory::SecretView {
                            NGIN::Crypto::ConstByteSpan {secret.data(), secret.Size()},
                    },
                    .publicKey = {},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256              = true,
                    .allowEdDsa              = false,
                    .expectedIssuer          = {},
                    .expectedAudience        = {},
                    .currentUnixTimeSeconds  = 0,
                    .allowedClockSkewSeconds = 0,
                    .requireExpiration       = false,
                    .validateExpiration      = true,
                    .validateNotBefore       = true,
                    .requiredClaims          = {},
                    .parseOptions            = {},
            });

    if (context.Value().Supports(NGIN::Crypto::MacAlgorithm::HmacSha256))
    {
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().claims.hasSubject);
        REQUIRE(result.Value().claims.subject == "1234567890");
    }
    else
    {
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("JWT PS256 validation uses provider-backed RSA-PSS when available", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "eyJhbGciOiJQUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJpc3MiOiJpc3N1ZXItYSIsImF1ZCI6ImFwaSIsInN1YiI6IjEyMyIsImV4cCI6MjAwMDAwMDAwMH0."
            "GSu3TK-SHBUoarVlw0kQu4jOvtif9F939Vh9_PVVLCHQFD2pOHy2VQyZpVqb_Ra0pdBWZODUeYQLobU1PTRb0H2Zu01OcAVxuXfHnNrmI"
            "UNgICCzeJdJKjDT-TAXstvQQE_aISW9PBeOV4Y1iL318c9ArrhXhZ_1fioOadKrBYVfEFNgZzFy1DDG5Fb2VuW7UU6R0jg4yoOeOZzIv__"
            "bhB8Erx3cVdda17TXyOeq0sPu3MRskjLyp1q8OgFpn_5HEBE_j-sLYSmGDZhtfKlekFU9ad9tkuml7waathKBhp25U__0Lln1uZzdcdkszMUY"
            "3pi8EcC5mSgcOGsGtdU1rA"};

    auto publicKeyDer = HexBytes(NGIN::Crypto::Tests::ProviderVectors::RSA_PSS_SHA256_REGRESSION.publicKeyDerHex);
    auto context      = NGIN::Crypto::Backend::CreateBestAvailableContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            token,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Ps256,
                    .hmacKey   = {},
                    .publicKey = NGIN::Crypto::ConstByteSpan {publicKeyDer.data(), publicKeyDer.Size()},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256              = false,
                    .allowPs256              = true,
                    .allowEdDsa              = false,
                    .expectedIssuer          = "issuer-a",
                    .expectedAudience        = "api",
                    .currentUnixTimeSeconds  = 1950000000,
                    .allowedClockSkewSeconds = 0,
                    .requireExpiration       = true,
                    .validateExpiration      = true,
                    .validateNotBefore       = true,
                    .requiredClaims          = {"iss", "aud", "exp"},
                    .parseOptions            = {},
            });

    if (context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::RsaPssSha256))
    {
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().claims.hasSubject);
        REQUIRE(result.Value().claims.subject == "123");
    }
    else
    {
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("JWT ES256 validation uses provider-backed ECDSA P-256 when available", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJpc3MiOiJpc3N1ZXItYSIsImF1ZCI6ImFwaSIsInN1YiI6IjEyMyIsImV4cCI6MjAwMDAwMDAwMH0."
            "xlVzFXXE_bwEGsVtdtTGyMdh1Rx40E8cjsTwqgC5OmJLLPIuPmMTT057zBrZXqmFLItqYJk9WVU0bxHGomVDMw"};

    auto publicKey = HexBytes(NGIN::Crypto::Tests::ProviderVectors::ECDSA_P256_SHA256_REGRESSION.publicKeyHex);
    auto context   = NGIN::Crypto::Backend::CreateBestAvailableContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            token,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Es256,
                    .hmacKey   = {},
                    .publicKey = NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256              = false,
                    .allowPs256              = false,
                    .allowEs256              = true,
                    .allowEdDsa              = false,
                    .expectedIssuer          = "issuer-a",
                    .expectedAudience        = "api",
                    .currentUnixTimeSeconds  = 1950000000,
                    .allowedClockSkewSeconds = 0,
                    .requireExpiration       = true,
                    .validateExpiration      = true,
                    .validateNotBefore       = true,
                    .requiredClaims          = {"iss", "aud", "exp"},
                    .parseOptions            = {},
            });

    if (context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256))
    {
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().claims.hasSubject);
        REQUIRE(result.Value().claims.subject == "123");
    }
    else
    {
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("PASETO v4.public parser extracts official vector payload and signature", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.public."
            "eyJkYXRhIjoidGhpcyBpcyBhIHNpZ25lZCBtZXNzYWdlIiwiZXhwIjoiMjAyMi0wMS0wMVQwMDowMDowMCswMDowMCJ9"
            "bg_XBBzds8lTZShVlwwKSgeKpLT3yukTw6JUz3W4h_ExsQV-P0V54zemZDcAxFaSeef1QlXEFtkqxT1ciiQEDA"};

    auto parsed = NGIN::Crypto::Tokens::ParsePasetoV4Public(token);
    REQUIRE(parsed.HasValue());
    REQUIRE(parsed.Value().payloadJson == R"({"data":"this is a signed message","exp":"2022-01-01T00:00:00+00:00"})");
    REQUIRE(parsed.Value().footer.empty());
    REQUIRE(parsed.Value().signature.Size() == 64);
}

TEST_CASE("PASETO v4.public claim accessors read typed payload claims", "[Crypto][Token]")
{
    auto parsed = NGIN::Crypto::Tokens::ParsePasetoV4Public(
            PasetoV4PublicWithPayload(R"({"data":"signed","tenant":42,"enabled":true})"));

    REQUIRE(parsed.HasValue());

    auto hasData = NGIN::Crypto::Tokens::HasPasetoClaim(parsed.Value(), "data");
    REQUIRE(hasData.HasValue());
    REQUIRE(hasData.Value());

    auto missing = NGIN::Crypto::Tokens::HasPasetoClaim(parsed.Value(), "missing");
    REQUIRE(missing.HasValue());
    REQUIRE_FALSE(missing.Value());

    auto data = NGIN::Crypto::Tokens::GetPasetoStringClaim(parsed.Value(), "data");
    REQUIRE(data.HasValue());
    REQUIRE(data.Value() == "signed");

    auto tenant = NGIN::Crypto::Tokens::GetPasetoInt64Claim(parsed.Value(), "tenant");
    REQUIRE(tenant.HasValue());
    REQUIRE(tenant.Value() == 42);

    auto enabled = NGIN::Crypto::Tokens::GetPasetoBoolClaim(parsed.Value(), "enabled");
    REQUIRE(enabled.HasValue());
    REQUIRE(enabled.Value());

    auto wrongType = NGIN::Crypto::Tokens::GetPasetoStringClaim(parsed.Value(), "tenant");
    REQUIRE_FALSE(wrongType.HasValue());
    REQUIRE(wrongType.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);

    auto absent = NGIN::Crypto::Tokens::GetPasetoBoolClaim(parsed.Value(), "missing");
    REQUIRE_FALSE(absent.HasValue());
    REQUIRE(absent.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}

TEST_CASE("PASETO v4.public validation uses official vector when Ed25519 is available", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.public."
            "eyJkYXRhIjoidGhpcyBpcyBhIHNpZ25lZCBtZXNzYWdlIiwiZXhwIjoiMjAyMi0wMS0wMVQwMDowMDowMCswMDowMCJ9"
            "bg_XBBzds8lTZShVlwwKSgeKpLT3yukTw6JUz3W4h_ExsQV-P0V54zemZDcAxFaSeef1QlXEFtkqxT1ciiQEDA"};
    auto publicKey = HexBytes("1eb9dbbbbc047c03fd70604e0071f0987e16b28b757225c11f00415d0e20b1a2");

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Tokens::ValidatePasetoV4Public(
            context.Value(),
            token,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter    = {},
                    .implicitAssertion = {},
                    .requiredClaims    = {"data", "exp"},
                    .parseOptions      = {},
            });

    if (context.Value().Supports(NGIN::Crypto::SignatureAlgorithm::Ed25519))
    {
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().payloadJson.find("signed message") != std::string::npos);
    }
    else
    {
        REQUIRE_FALSE(result.HasValue());
        REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("PASETO v4.public validates footer policy before backend verification", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.public."
            "eyJkYXRhIjoidGhpcyBpcyBhIHNpZ25lZCBtZXNzYWdlIiwiZXhwIjoiMjAyMi0wMS0wMVQwMDowMDowMCswMDowMCJ9"
            "v3Jt8mx_TdM2ceTGoqwrh4yDFn0XsHvvV_D0DtwQxVrJEBMl0F2caAdgnpKlt4p7xBnx1HcO-SPo8FPp214HDw."
            "eyJraWQiOiJ6VmhNaVBCUDlmUmYyc25FY1Q3Z0ZUaW9lQTlDT2NOeTlEZmdMMVc2MGhhTiJ9"};
    auto publicKey      = HexBytes("1eb9dbbbbc047c03fd70604e0071f0987e16b28b757225c11f00415d0e20b1a2");
    auto expectedFooter = Bytes(R"({"kid":"wrong"})");

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Tokens::ValidatePasetoV4Public(
            context.Value(),
            token,
            NGIN::Crypto::ConstByteSpan {publicKey.data(), publicKey.Size()},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter    = NGIN::Crypto::ConstByteSpan {expectedFooter.data(), expectedFooter.Size()},
                    .implicitAssertion = {},
                    .requiredClaims    = {"data", "exp"},
                    .parseOptions      = {},
            });

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);
}

TEST_CASE("PASETO v4.local opens official vector when libsodium is available", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.local."
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAr68PS4AXe7If_ZgesdkUMvSwscFlAl1pk5HC0e8kApeaqMfGo_7OpBnwJOAbY9V"
            "7WU6abu74MmcUE8YWAiaArVI8XJ5hOb_4v9RmDkneN0S92dx0OW4pgy7omxgf3S8c3LlQg"};
    auto key = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");

    auto context = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (!context.HasValue())
    {
        auto fallback = NGIN::Crypto::Backend::CreateContext();
        REQUIRE(fallback.HasValue());
        auto unsupported = NGIN::Crypto::Tokens::OpenPasetoV4Local(
                fallback.Value(),
                token,
                NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
                NGIN::Crypto::Tokens::PasetoValidationPolicy {
                        .expectedFooter    = {},
                        .implicitAssertion = {},
                        .requiredClaims    = {"data", "exp"},
                        .parseOptions      = {},
                });
        REQUIRE_FALSE(unsupported.HasValue());
        REQUIRE(unsupported.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
        return;
    }

    auto opened = NGIN::Crypto::Tokens::OpenPasetoV4Local(
            context.Value(),
            token,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter    = {},
                    .implicitAssertion = {},
                    .requiredClaims    = {"data", "exp"},
                    .parseOptions      = {},
            });

    REQUIRE(opened.HasValue());
    REQUIRE(opened.Value().payloadJson == R"({"data":"this is a secret message","exp":"2022-01-01T00:00:00+00:00"})");
    REQUIRE(opened.Value().footer.empty());
    REQUIRE(opened.Value().nonce.Size() == 32);
}

TEST_CASE("PASETO v4.local seal returns unsupported without libsodium", "[Crypto][Token]")
{
    auto key     = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto sealed = NGIN::Crypto::Tokens::SealPasetoV4Local(
            context.Value(),
            R"({"data":"secret"})",
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoSealOptions {});

    if (context.Value().Info().Name() == "libsodium")
    {
        REQUIRE(sealed.HasValue());
    }
    else
    {
        REQUIRE_FALSE(sealed.HasValue());
        REQUIRE(sealed.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    }
}

TEST_CASE("PASETO v4.local seal round-trips payload with footer and implicit assertion", "[Crypto][Token]")
{
    auto key      = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");
    auto footer   = Bytes(R"({"kid":"local-key"})");
    auto implicit = Bytes(R"({"tenant":"ngin"})");

    auto context = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (!context.HasValue())
    {
        return;
    }

    auto sealed = NGIN::Crypto::Tokens::SealPasetoV4Local(
            context.Value(),
            R"({"data":"secret","exp":"2026-06-22T00:00:00+00:00"})",
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoSealOptions {
                    .footer            = NGIN::Crypto::ConstByteSpan {footer.data(), footer.Size()},
                    .implicitAssertion = NGIN::Crypto::ConstByteSpan {implicit.data(), implicit.Size()},
                    .limits            = {},
            });
    REQUIRE(sealed.HasValue());
    REQUIRE(sealed.Value().starts_with("v4.local."));

    auto opened = NGIN::Crypto::Tokens::OpenPasetoV4Local(
            context.Value(),
            sealed.Value(),
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter    = NGIN::Crypto::ConstByteSpan {footer.data(), footer.Size()},
                    .implicitAssertion = NGIN::Crypto::ConstByteSpan {implicit.data(), implicit.Size()},
                    .requiredClaims    = {"data", "exp"},
                    .parseOptions      = {},
            });

    REQUIRE(opened.HasValue());
    REQUIRE(opened.Value().payloadJson == R"({"data":"secret","exp":"2026-06-22T00:00:00+00:00"})");
    REQUIRE(opened.Value().footer == R"({"kid":"local-key"})");

    auto data = NGIN::Crypto::Tokens::GetPasetoStringClaim(opened.Value(), "data");
    REQUIRE(data.HasValue());
    REQUIRE(data.Value() == "secret");
}

TEST_CASE("PASETO v4.local seal rejects malformed payload JSON", "[Crypto][Token]")
{
    auto key     = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");
    auto context = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (!context.HasValue())
    {
        return;
    }

    auto sealed = NGIN::Crypto::Tokens::SealPasetoV4Local(
            context.Value(),
            R"({"data":"a","data":"b"})",
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoSealOptions {});

    REQUIRE_FALSE(sealed.HasValue());
    REQUIRE(sealed.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("PASETO v4.local validates footer and implicit assertion with official vector", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.local."
            "32VIErrEkmY4JVILovbmfPXKW9wT1OdQepjMTC_MOtjA4kiqw7_tcaOM5GNEcnTxl60WiA8rd3wgFSNb_UdJPXjpzm0KW9ojM5"
            "f4O2mRvE2IcweP-PRdoHjd5-RHCiExR1IK6t6tybdlmnMwcDMw0YxA_gFSE_IUWl78aMtOepFYSWYfQA."
            "YXJiaXRyYXJ5LXN0cmluZy10aGF0LWlzbid0LWpzb24"};
    auto key      = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");
    auto footer   = Bytes("arbitrary-string-that-isn't-json");
    auto implicit = Bytes(R"({"test-vector":"4-E-9"})");

    auto context = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (!context.HasValue())
    {
        return;
    }

    auto opened = NGIN::Crypto::Tokens::OpenPasetoV4Local(
            context.Value(),
            token,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter    = NGIN::Crypto::ConstByteSpan {footer.data(), footer.Size()},
                    .implicitAssertion = NGIN::Crypto::ConstByteSpan {implicit.data(), implicit.Size()},
                    .requiredClaims    = {"data", "exp"},
                    .parseOptions      = {},
            });

    REQUIRE(opened.HasValue());
    REQUIRE(opened.Value().payloadJson == R"({"data":"this is a hidden message","exp":"2022-01-01T00:00:00+00:00"})");
    REQUIRE(opened.Value().footer == "arbitrary-string-that-isn't-json");
}

TEST_CASE("PASETO v4.local validates footer policy before backend open", "[Crypto][Token]")
{
    constexpr std::string_view token {
            "v4.local."
            "32VIErrEkmY4JVILovbmfPXKW9wT1OdQepjMTC_MOtjA4kiqw7_tcaOM5GNEcnTxl60WiA8rd3wgFSNb_UdJPXjpzm0KW9ojM5"
            "f4O2mRvE2IcweP-PRdoHjd5-RHCiExR1IK6t6tybdlmnMwcDMw0YxA_gFSE_IUWl78aMtOepFYSWYfQA."
            "YXJiaXRyYXJ5LXN0cmluZy10aGF0LWlzbid0LWpzb24"};
    auto key            = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");
    auto expectedFooter = Bytes(R"({"kid":"wrong"})");

    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());

    auto result = NGIN::Crypto::Tokens::OpenPasetoV4Local(
            context.Value(),
            token,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                    .expectedFooter =
                            NGIN::Crypto::ConstByteSpan {expectedFooter.data(), expectedFooter.Size()},
                    .implicitAssertion = {},
                    .requiredClaims    = {},
                    .parseOptions      = {},
            });

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::PolicyRejected);
}

TEST_CASE("PASETO v4.local rejects tampered payloads when libsodium is available", "[Crypto][Token]")
{
    std::string token {
            "v4.local."
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAr68PS4AXe7If_ZgesdkUMvSwscFlAl1pk5HC0e8kApeaqMfGo_7OpBnwJOAbY9V"
            "7WU6abu74MmcUE8YWAiaArVI8XJ5hOb_4v9RmDkneN0S92dx0OW4pgy7omxgf3S8c3LlQg"};
    auto key = HexBytes("707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f");

    auto context = NGIN::Crypto::Backend::CreatePackageContext("libsodium");
    if (!context.HasValue())
    {
        return;
    }

    token[token.size() - 1] = token[token.size() - 1] == 'A' ? 'B' : 'A';
    auto result             = NGIN::Crypto::Tokens::OpenPasetoV4Local(
            context.Value(),
            token,
            NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            NGIN::Crypto::Tokens::PasetoValidationPolicy {
                                .expectedFooter    = {},
                                .implicitAssertion = {},
                                .requiredClaims    = {},
                                .parseOptions      = {},
            });

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
}

TEST_CASE("PASETO v4.public parser rejects malformed purpose and duplicate payload claims", "[Crypto][Token]")
{
    auto wrongPurpose = NGIN::Crypto::Tokens::ParsePasetoV4Public("v4.local.invalid");
    REQUIRE_FALSE(wrongPurpose.HasValue());
    REQUIRE(wrongPurpose.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    auto payload   = Bytes(R"({"data":"a","data":"b"})");
    auto signature = Bytes(std::string(64, 's'));
    auto combined  = NGIN::Crypto::MakeByteBuffer(payload.Size() + signature.Size());
    for (NGIN::UIntSize i = 0; i < payload.Size(); ++i)
    {
        combined[i] = payload[i];
    }
    for (NGIN::UIntSize i = 0; i < signature.Size(); ++i)
    {
        combined[payload.Size() + i] = signature[i];
    }
    auto encoded = NGIN::Crypto::Encoding::EncodeBase64Url(combined);
    REQUIRE(encoded.HasValue());

    auto duplicate = NGIN::Crypto::Tokens::ParsePasetoV4Public(std::string {"v4.public."} + encoded.Value());
    REQUIRE_FALSE(duplicate.HasValue());
    REQUIRE(duplicate.Error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("PASETO v4.public parser malformed corpus rejects invalid envelopes", "[Crypto][Token]")
{
    auto shortPayload = NGIN::Crypto::Encoding::EncodeBase64Url(Bytes("{}"));
    REQUIRE(shortPayload.HasValue());

    auto nonJsonPayload = Bytes("not-json");
    auto signature      = Bytes(std::string(64, 's'));
    auto combined       = NGIN::Crypto::MakeByteBuffer(nonJsonPayload.Size() + signature.Size());
    for (NGIN::UIntSize i = 0; i < nonJsonPayload.Size(); ++i)
    {
        combined[i] = nonJsonPayload[i];
    }
    for (NGIN::UIntSize i = 0; i < signature.Size(); ++i)
    {
        combined[nonJsonPayload.Size() + i] = signature[i];
    }
    auto encodedNonJson = NGIN::Crypto::Encoding::EncodeBase64Url(combined);
    REQUIRE(encodedNonJson.HasValue());

    for (const auto& token: {
                 std::string {},
                 std::string {"v4.local."} + shortPayload.Value(),
                 std::string {"v4.public."},
                 std::string {"v4.public.!!!!"},
                 std::string {"v4.public."} + shortPayload.Value(),
                 std::string {"v4.public."} + encodedNonJson.Value(),
         })
    {
        auto parsed = NGIN::Crypto::Tokens::ParsePasetoV4Public(token);
        REQUIRE_FALSE(parsed.HasValue());
    }
}
