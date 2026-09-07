#include <NGIN/Crypto/Tokens/Jwt.hpp>

#include <NGIN/Crypto/Asymmetric/Rsa.hpp>
#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Mac/MacOperations.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace NGIN::Crypto::Tokens
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError PolicyRejected() noexcept
        {
            return CryptoError {CryptoErrorCode::PolicyRejected};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] std::string CopyToString(ConstByteSpan bytes)
        {
            std::string output;
            output.resize(bytes.size());
            for (NGIN::UIntSize i = 0; i < bytes.size(); ++i)
            {
                output[i] = static_cast<char>(std::to_integer<NGIN::UInt8>(bytes[i]));
            }
            return output;
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64>
        JsonNumberToInt64(NGIN::Serialization::JSON::ValueView value) noexcept
        {
            if (const auto integer = value.TryInt64())
                return *integer;

            const auto number = value.TryDouble();
            if (!number || !std::isfinite(*number))
            {
                return std::unexpected(ParseError());
            }

            const auto whole = std::trunc(*number);
            if (*number != whole || whole < static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::min()) ||
                whole > static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::max()))
            {
                return std::unexpected(ParseError());
            }

            return static_cast<NGIN::Int64>(whole);
        }

        [[nodiscard]] CryptoExpected<JwtAlgorithm> ParseAlgorithm(std::string_view algorithm) noexcept
        {
            if (algorithm == "HS256")
            {
                return JwtAlgorithm::Hs256;
            }
            if (algorithm == "PS256")
            {
                return JwtAlgorithm::Ps256;
            }
            if (algorithm == "ES256")
            {
                return JwtAlgorithm::Es256;
            }
            if (algorithm == "EdDSA")
            {
                return JwtAlgorithm::EdDsa;
            }
            if (algorithm == "none")
            {
                return std::unexpected(PolicyRejected());
            }

            return std::unexpected(UnsupportedAlgorithm());
        }

        [[nodiscard]] CryptoExpected<JwtAlgorithm> ParseHeaderAlgorithm(std::string_view headerJson)
        {
            auto document = NGIN::Serialization::JSON::Parser::Parse(
                    headerJson);
            if (!document.has_value() || !document.value().Root().IsObject())
            {
                return std::unexpected(ParseError());
            }

            const auto object = *document.value().Root().TryObject();
            const auto alg    = object.Find("alg");
            if (!alg || !alg->IsString())
            {
                return std::unexpected(ParseError());
            }

            return ParseAlgorithm(*alg->TryString());
        }

        [[nodiscard]] CryptoExpected<JwtClaims> ParseClaims(std::string_view payloadJson)
        {
            auto document = NGIN::Serialization::JSON::Parser::Parse(
                    payloadJson);
            if (!document.has_value() || !document.value().Root().IsObject())
            {
                return std::unexpected(ParseError());
            }

            const auto object = *document.value().Root().TryObject();
            JwtClaims  claims;

            if (const auto value = object.Find("iss"))
            {
                if (!value->IsString())
                {
                    return std::unexpected(ParseError());
                }
                claims.issuer    = std::string {*value->TryString()};
                claims.hasIssuer = true;
            }

            if (const auto value = object.Find("sub"))
            {
                if (!value->IsString())
                {
                    return std::unexpected(ParseError());
                }
                claims.subject    = std::string {*value->TryString()};
                claims.hasSubject = true;
            }

            if (const auto value = object.Find("aud"))
            {
                if (value->IsString())
                {
                    claims.audiences.PushBack(std::string {*value->TryString()});
                }
                else if (value->IsArray())
                {
                    const auto audiences = value->TryArray();
                    for (const NGIN::Serialization::JSON::ValueView item: *audiences)
                    {
                        if (!item.IsString())
                        {
                            return std::unexpected(ParseError());
                        }
                        claims.audiences.PushBack(std::string {*item.TryString()});
                    }
                }
                else
                {
                    return std::unexpected(ParseError());
                }
            }

            if (const auto value = object.Find("exp"))
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.has_value())
                {
                    return std::unexpected(std::move(number).error());
                }
                claims.expirationTime    = number.value();
                claims.hasExpirationTime = true;
            }

            if (const auto value = object.Find("nbf"))
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.has_value())
                {
                    return std::unexpected(std::move(number).error());
                }
                claims.notBefore    = number.value();
                claims.hasNotBefore = true;
            }

            if (const auto value = object.Find("iat"))
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.has_value())
                {
                    return std::unexpected(std::move(number).error());
                }
                claims.issuedAt    = number.value();
                claims.hasIssuedAt = true;
            }

            return claims;
        }

        [[nodiscard]] bool HasClaim(const JwtClaims& claims, std::string_view name) noexcept
        {
            if (name == "iss")
            {
                return claims.hasIssuer;
            }
            if (name == "sub")
            {
                return claims.hasSubject;
            }
            if (name == "aud")
            {
                return claims.audiences.Size() != 0;
            }
            if (name == "exp")
            {
                return claims.hasExpirationTime;
            }
            if (name == "nbf")
            {
                return claims.hasNotBefore;
            }
            if (name == "iat")
            {
                return claims.hasIssuedAt;
            }
            return false;
        }

        [[nodiscard]] CryptoExpected<NGIN::Serialization::JSON::Document>
        ParsePayloadDocument(const JwtCompactToken& token)
        {
            auto document = NGIN::Serialization::JSON::Parser::Parse(
                    token.payloadJson);
            if (!document.has_value() || !document.value().Root().IsObject())
            {
                return std::unexpected(ParseError());
            }

            return std::move(document.value());
        }

        [[nodiscard]] CryptoExpected<void> ValidateClaims(const JwtClaims& claims, const JwtValidationPolicy& policy) noexcept
        {
            for (std::string_view required: policy.requiredClaims)
            {
                if (!HasClaim(claims, required))
                {
                    return std::unexpected(PolicyRejected());
                }
            }

            if (!policy.expectedIssuer.empty())
            {
                if (!claims.hasIssuer || claims.issuer != policy.expectedIssuer)
                {
                    return std::unexpected(PolicyRejected());
                }
            }

            if (!policy.expectedAudience.empty())
            {
                bool found = false;
                for (const auto& audience: claims.audiences)
                {
                    if (audience == policy.expectedAudience)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return std::unexpected(PolicyRejected());
                }
            }

            if (policy.requireExpiration && !claims.hasExpirationTime)
            {
                return std::unexpected(PolicyRejected());
            }

            const auto now  = policy.currentUnixTimeSeconds;
            const auto skew = policy.allowedClockSkewSeconds;
            if (now != 0 && policy.validateExpiration && claims.hasExpirationTime && now > claims.expirationTime + skew)
            {
                return std::unexpected(PolicyRejected());
            }

            if (now != 0 && policy.validateNotBefore && claims.hasNotBefore && now + skew < claims.notBefore)
            {
                return std::unexpected(PolicyRejected());
            }

            return {};
        }

        [[nodiscard]] bool AlgorithmAllowed(JwtAlgorithm algorithm, const JwtValidationPolicy& policy) noexcept
        {
            switch (algorithm)
            {
                case JwtAlgorithm::Hs256:
                    return policy.allowHs256;
                case JwtAlgorithm::Ps256:
                    return policy.allowPs256;
                case JwtAlgorithm::Es256:
                    return policy.allowEs256;
                case JwtAlgorithm::EdDsa:
                    return policy.allowEdDsa;
            }

            return false;
        }

        [[nodiscard]] ConstByteSpan StringBytes(std::string_view text) noexcept
        {
            return ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()};
        }
    }// namespace

    CryptoExpected<JwtCompactToken> ParseJwtCompact(std::string_view token, JwtParseOptions options)
    {
        const auto firstDot = token.find('.');
        if (firstDot == std::string_view::npos)
        {
            return std::unexpected(ParseError());
        }

        const auto secondDot = token.find('.', firstDot + 1);
        if (secondDot == std::string_view::npos || token.find('.', secondDot + 1) != std::string_view::npos)
        {
            return std::unexpected(ParseError());
        }

        const auto encodedHeader    = token.substr(0, firstDot);
        const auto encodedPayload   = token.substr(firstDot + 1, secondDot - firstDot - 1);
        const auto encodedSignature = token.substr(secondDot + 1);

        if (encodedHeader.empty() || encodedPayload.empty())
        {
            return std::unexpected(ParseError());
        }

        auto header = NGIN::Crypto::Encoding::DecodeBase64Url(encodedHeader);
        if (!header.has_value())
        {
            return std::unexpected(std::move(header).error());
        }
        if (header.value().Size() > options.maxHeaderBytes)
        {
            return std::unexpected(ParseError());
        }

        auto payload = NGIN::Crypto::Encoding::DecodeBase64Url(encodedPayload);
        if (!payload.has_value())
        {
            return std::unexpected(std::move(payload).error());
        }
        if (payload.value().Size() > options.maxPayloadBytes)
        {
            return std::unexpected(ParseError());
        }

        auto signature = NGIN::Crypto::Encoding::DecodeBase64Url(encodedSignature);
        if (!signature.has_value())
        {
            return std::unexpected(std::move(signature).error());
        }
        if (signature.value().Size() > options.maxSignatureBytes)
        {
            return std::unexpected(ParseError());
        }

        auto headerJson  = CopyToString(ConstByteSpan {header.value().data(), header.value().Size()});
        auto payloadJson = CopyToString(ConstByteSpan {payload.value().data(), payload.value().Size()});

        auto algorithm = ParseHeaderAlgorithm(headerJson);
        if (!algorithm.has_value())
        {
            return std::unexpected(std::move(algorithm).error());
        }

        auto claims = ParseClaims(payloadJson);
        if (!claims.has_value())
        {
            return std::unexpected(std::move(claims).error());
        }

        return JwtCompactToken {
                .algorithm    = algorithm.value(),
                .headerJson   = std::move(headerJson),
                .payloadJson  = std::move(payloadJson),
                .signingInput = std::string {token.substr(0, secondDot)},
                .signature    = std::move(signature.value()),
                .claims       = std::move(claims.value()),
        };
    }

    CryptoExpected<bool> HasJwtClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.has_value())
        {
            return std::unexpected(std::move(document).error());
        }

        return document.value().Root().TryObject()->Find(name).has_value();
    }

    CryptoExpected<std::string> GetJwtStringClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.has_value())
        {
            return std::unexpected(std::move(document).error());
        }

        const auto value = document.value().Root().TryObject()->Find(name);
        if (!value || !value->IsString())
        {
            return std::unexpected(InvalidArgument());
        }

        return std::string {*value->TryString()};
    }

    CryptoExpected<NGIN::Int64> GetJwtInt64Claim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.has_value())
        {
            return std::unexpected(std::move(document).error());
        }

        const auto value = document.value().Root().TryObject()->Find(name);
        if (!value || !value->IsNumber())
        {
            return std::unexpected(InvalidArgument());
        }

        return JsonNumberToInt64(*value);
    }

    CryptoExpected<bool> GetJwtBoolClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.has_value())
        {
            return std::unexpected(std::move(document).error());
        }

        const auto value = document.value().Root().TryObject()->Find(name);
        if (!value || !value->IsBool())
        {
            return std::unexpected(InvalidArgument());
        }

        return *value->TryBool();
    }

    CryptoExpected<JwtCompactToken> ValidateJwt(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            const JwtValidationKey&                     key,
            const JwtValidationPolicy&                  policy)
    {
        auto parsed = ParseJwtCompact(token, policy.parseOptions);
        if (!parsed.has_value())
        {
            return std::unexpected(std::move(parsed).error());
        }

        if (parsed.value().algorithm != key.algorithm || !AlgorithmAllowed(parsed.value().algorithm, policy))
        {
            return std::unexpected(PolicyRejected());
        }

        auto claims = ValidateClaims(parsed.value().claims, policy);
        if (!claims.has_value())
        {
            return std::unexpected(std::move(claims).error());
        }

        switch (parsed.value().algorithm)
        {
            case JwtAlgorithm::Hs256: {
                auto result = NGIN::Crypto::Mac::VerifyMac(
                        context,
                        MacAlgorithm::HmacSha256,
                        key.hmacKey,
                        StringBytes(parsed.value().signingInput),
                        ConstByteSpan {parsed.value().signature.data(), parsed.value().signature.Size()});
                if (!result.has_value())
                {
                    return std::unexpected(std::move(result).error());
                }
                break;
            }
            case JwtAlgorithm::Ps256: {
                auto result = NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(
                        context,
                        NGIN::Crypto::Asymmetric::RsaPssSha256VerifyInput {
                                .publicKeyDer = key.publicKey,
                                .message      = StringBytes(parsed.value().signingInput),
                                .signature    = ConstByteSpan {
                                        parsed.value().signature.data(),
                                        parsed.value().signature.Size(),
                                },
                        });
                if (!result.has_value())
                {
                    return std::unexpected(std::move(result).error());
                }
                break;
            }
            case JwtAlgorithm::Es256: {
                auto result = NGIN::Crypto::Signatures::Verify(
                        context,
                        SignatureAlgorithm::EcdsaP256Sha256,
                        NGIN::Crypto::Signatures::VerifyInput {
                                .publicKey = key.publicKey,
                                .message   = StringBytes(parsed.value().signingInput),
                                .signature = ConstByteSpan {
                                        parsed.value().signature.data(),
                                        parsed.value().signature.Size(),
                                },
                        });
                if (!result.has_value())
                {
                    return std::unexpected(std::move(result).error());
                }
                break;
            }
            case JwtAlgorithm::EdDsa: {
                auto result = NGIN::Crypto::Signatures::Verify(
                        context,
                        SignatureAlgorithm::Ed25519,
                        NGIN::Crypto::Signatures::VerifyInput {
                                .publicKey = key.publicKey,
                                .message   = StringBytes(parsed.value().signingInput),
                                .signature = ConstByteSpan {
                                        parsed.value().signature.data(),
                                        parsed.value().signature.Size(),
                                },
                        });
                if (!result.has_value())
                {
                    return std::unexpected(std::move(result).error());
                }
                break;
            }
        }

        return parsed;
    }
}// namespace NGIN::Crypto::Tokens
