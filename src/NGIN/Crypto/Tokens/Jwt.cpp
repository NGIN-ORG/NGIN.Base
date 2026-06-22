#include <NGIN/Crypto/Tokens/Jwt.hpp>

#include <NGIN/Crypto/Asymmetric/Rsa.hpp>
#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Mac/Mac.hpp>
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

        [[nodiscard]] bool HasDuplicateMembers(const NGIN::Serialization::JsonValue& value) noexcept
        {
            if (value.IsObject())
            {
                const auto& members = value.AsObject().members;
                for (NGIN::UIntSize i = 0; i < members.Size(); ++i)
                {
                    for (NGIN::UIntSize j = i + 1; j < members.Size(); ++j)
                    {
                        if (members[i].name == members[j].name)
                        {
                            return true;
                        }
                    }
                    if (HasDuplicateMembers(members[i].value))
                    {
                        return true;
                    }
                }
                return false;
            }

            if (value.IsArray())
            {
                for (const auto& item: value.AsArray().values)
                {
                    if (HasDuplicateMembers(item))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64> JsonNumberToInt64(const NGIN::Serialization::JsonValue& value) noexcept
        {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return ParseError();
            }

            const auto number = value.AsNumber();
            const auto whole  = std::trunc(number);
            if (number != whole || whole < static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::min()) ||
                whole > static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::max()))
            {
                return ParseError();
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
                return PolicyRejected();
            }

            return UnsupportedAlgorithm();
        }

        [[nodiscard]] CryptoExpected<JwtAlgorithm> ParseHeaderAlgorithm(std::string_view headerJson)
        {
            auto document = NGIN::Serialization::JsonParser::Parse(headerJson);
            if (!document.HasValue() || !document.Value().Root().IsObject() || HasDuplicateMembers(document.Value().Root()))
            {
                return ParseError();
            }

            const auto& object = document.Value().Root().AsObject();
            const auto* alg    = object.Find("alg");
            if (alg == nullptr || !alg->IsString())
            {
                return ParseError();
            }

            return ParseAlgorithm(alg->AsString());
        }

        [[nodiscard]] CryptoExpected<JwtClaims> ParseClaims(std::string_view payloadJson)
        {
            auto document = NGIN::Serialization::JsonParser::Parse(payloadJson);
            if (!document.HasValue() || !document.Value().Root().IsObject() || HasDuplicateMembers(document.Value().Root()))
            {
                return ParseError();
            }

            const auto& object = document.Value().Root().AsObject();
            JwtClaims   claims;

            if (const auto* value = object.Find("iss"); value != nullptr)
            {
                if (!value->IsString())
                {
                    return ParseError();
                }
                claims.issuer    = std::string {value->AsString()};
                claims.hasIssuer = true;
            }

            if (const auto* value = object.Find("sub"); value != nullptr)
            {
                if (!value->IsString())
                {
                    return ParseError();
                }
                claims.subject    = std::string {value->AsString()};
                claims.hasSubject = true;
            }

            if (const auto* value = object.Find("aud"); value != nullptr)
            {
                if (value->IsString())
                {
                    claims.audiences.PushBack(std::string {value->AsString()});
                }
                else if (value->IsArray())
                {
                    for (const auto& item: value->AsArray().values)
                    {
                        if (!item.IsString())
                        {
                            return ParseError();
                        }
                        claims.audiences.PushBack(std::string {item.AsString()});
                    }
                }
                else
                {
                    return ParseError();
                }
            }

            if (const auto* value = object.Find("exp"); value != nullptr)
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.HasValue())
                {
                    return number.Error();
                }
                claims.expirationTime    = number.Value();
                claims.hasExpirationTime = true;
            }

            if (const auto* value = object.Find("nbf"); value != nullptr)
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.HasValue())
                {
                    return number.Error();
                }
                claims.notBefore    = number.Value();
                claims.hasNotBefore = true;
            }

            if (const auto* value = object.Find("iat"); value != nullptr)
            {
                auto number = JsonNumberToInt64(*value);
                if (!number.HasValue())
                {
                    return number.Error();
                }
                claims.issuedAt    = number.Value();
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

        [[nodiscard]] CryptoExpected<NGIN::Serialization::JsonDocument> ParsePayloadDocument(const JwtCompactToken& token)
        {
            auto document = NGIN::Serialization::JsonParser::Parse(token.payloadJson);
            if (!document.HasValue() || !document.Value().Root().IsObject() || HasDuplicateMembers(document.Value().Root()))
            {
                return ParseError();
            }

            return std::move(document.Value());
        }

        [[nodiscard]] CryptoExpected<void> ValidateClaims(const JwtClaims& claims, const JwtValidationPolicy& policy) noexcept
        {
            for (std::string_view required: policy.requiredClaims)
            {
                if (!HasClaim(claims, required))
                {
                    return PolicyRejected();
                }
            }

            if (!policy.expectedIssuer.empty())
            {
                if (!claims.hasIssuer || claims.issuer != policy.expectedIssuer)
                {
                    return PolicyRejected();
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
                    return PolicyRejected();
                }
            }

            if (policy.requireExpiration && !claims.hasExpirationTime)
            {
                return PolicyRejected();
            }

            const auto now  = policy.currentUnixTimeSeconds;
            const auto skew = policy.allowedClockSkewSeconds;
            if (now != 0 && policy.validateExpiration && claims.hasExpirationTime && now > claims.expirationTime + skew)
            {
                return PolicyRejected();
            }

            if (now != 0 && policy.validateNotBefore && claims.hasNotBefore && now + skew < claims.notBefore)
            {
                return PolicyRejected();
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
            return ParseError();
        }

        const auto secondDot = token.find('.', firstDot + 1);
        if (secondDot == std::string_view::npos || token.find('.', secondDot + 1) != std::string_view::npos)
        {
            return ParseError();
        }

        const auto encodedHeader    = token.substr(0, firstDot);
        const auto encodedPayload   = token.substr(firstDot + 1, secondDot - firstDot - 1);
        const auto encodedSignature = token.substr(secondDot + 1);

        if (encodedHeader.empty() || encodedPayload.empty())
        {
            return ParseError();
        }

        auto header = NGIN::Crypto::Encoding::DecodeBase64Url(encodedHeader);
        if (!header.HasValue())
        {
            return header.Error();
        }
        if (header.Value().Size() > options.maxHeaderBytes)
        {
            return ParseError();
        }

        auto payload = NGIN::Crypto::Encoding::DecodeBase64Url(encodedPayload);
        if (!payload.HasValue())
        {
            return payload.Error();
        }
        if (payload.Value().Size() > options.maxPayloadBytes)
        {
            return ParseError();
        }

        auto signature = NGIN::Crypto::Encoding::DecodeBase64Url(encodedSignature);
        if (!signature.HasValue())
        {
            return signature.Error();
        }
        if (signature.Value().Size() > options.maxSignatureBytes)
        {
            return ParseError();
        }

        auto headerJson  = CopyToString(ConstByteSpan {header.Value().data(), header.Value().Size()});
        auto payloadJson = CopyToString(ConstByteSpan {payload.Value().data(), payload.Value().Size()});

        auto algorithm = ParseHeaderAlgorithm(headerJson);
        if (!algorithm.HasValue())
        {
            return algorithm.Error();
        }

        auto claims = ParseClaims(payloadJson);
        if (!claims.HasValue())
        {
            return claims.Error();
        }

        return JwtCompactToken {
                .algorithm    = algorithm.Value(),
                .headerJson   = std::move(headerJson),
                .payloadJson  = std::move(payloadJson),
                .signingInput = std::string {token.substr(0, secondDot)},
                .signature    = std::move(signature.Value()),
                .claims       = std::move(claims.Value()),
        };
    }

    CryptoExpected<bool> HasJwtClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.HasValue())
        {
            return document.Error();
        }

        return document.Value().Root().AsObject().Find(name) != nullptr;
    }

    CryptoExpected<std::string> GetJwtStringClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.HasValue())
        {
            return document.Error();
        }

        const auto* value = document.Value().Root().AsObject().Find(name);
        if (value == nullptr || !value->IsString())
        {
            return InvalidArgument();
        }

        return std::string {value->AsString()};
    }

    CryptoExpected<NGIN::Int64> GetJwtInt64Claim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.HasValue())
        {
            return document.Error();
        }

        const auto* value = document.Value().Root().AsObject().Find(name);
        if (value == nullptr || !value->IsNumber())
        {
            return InvalidArgument();
        }

        return JsonNumberToInt64(*value);
    }

    CryptoExpected<bool> GetJwtBoolClaim(const JwtCompactToken& token, std::string_view name)
    {
        auto document = ParsePayloadDocument(token);
        if (!document.HasValue())
        {
            return document.Error();
        }

        const auto* value = document.Value().Root().AsObject().Find(name);
        if (value == nullptr || !value->IsBool())
        {
            return InvalidArgument();
        }

        return value->AsBool();
    }

    CryptoExpected<JwtCompactToken> ValidateJwt(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            const JwtValidationKey&                     key,
            const JwtValidationPolicy&                  policy)
    {
        auto parsed = ParseJwtCompact(token, policy.parseOptions);
        if (!parsed.HasValue())
        {
            return parsed.Error();
        }

        if (parsed.Value().algorithm != key.algorithm || !AlgorithmAllowed(parsed.Value().algorithm, policy))
        {
            return PolicyRejected();
        }

        auto claims = ValidateClaims(parsed.Value().claims, policy);
        if (!claims.HasValue())
        {
            return claims.Error();
        }

        switch (parsed.Value().algorithm)
        {
            case JwtAlgorithm::Hs256: {
                auto result = NGIN::Crypto::Mac::VerifyMac(
                        context,
                        MacAlgorithm::HmacSha256,
                        key.hmacKey,
                        StringBytes(parsed.Value().signingInput),
                        ConstByteSpan {parsed.Value().signature.data(), parsed.Value().signature.Size()});
                if (!result.HasValue())
                {
                    return result.Error();
                }
                break;
            }
            case JwtAlgorithm::Ps256: {
                auto result = NGIN::Crypto::Asymmetric::VerifyRsaPssSha256(
                        context,
                        NGIN::Crypto::Asymmetric::RsaPssSha256VerifyInput {
                                .publicKeyDer = key.publicKey,
                                .message      = StringBytes(parsed.Value().signingInput),
                                .signature    = ConstByteSpan {
                                        parsed.Value().signature.data(),
                                        parsed.Value().signature.Size(),
                                },
                        });
                if (!result.HasValue())
                {
                    return result.Error();
                }
                break;
            }
            case JwtAlgorithm::Es256: {
                auto result = NGIN::Crypto::Signatures::Verify(
                        context,
                        SignatureAlgorithm::EcdsaP256Sha256,
                        NGIN::Crypto::Signatures::VerifyInput {
                                .publicKey = key.publicKey,
                                .message   = StringBytes(parsed.Value().signingInput),
                                .signature = ConstByteSpan {
                                        parsed.Value().signature.data(),
                                        parsed.Value().signature.Size(),
                                },
                        });
                if (!result.HasValue())
                {
                    return result.Error();
                }
                break;
            }
            case JwtAlgorithm::EdDsa: {
                auto result = NGIN::Crypto::Signatures::Verify(
                        context,
                        SignatureAlgorithm::Ed25519,
                        NGIN::Crypto::Signatures::VerifyInput {
                                .publicKey = key.publicKey,
                                .message   = StringBytes(parsed.Value().signingInput),
                                .signature = ConstByteSpan {
                                        parsed.Value().signature.data(),
                                        parsed.Value().signature.Size(),
                                },
                        });
                if (!result.HasValue())
                {
                    return result.Error();
                }
                break;
            }
        }

        return parsed;
    }
}// namespace NGIN::Crypto::Tokens
