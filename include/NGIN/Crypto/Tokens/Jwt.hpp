#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Tokens
{
    enum class JwtAlgorithm : NGIN::UInt8
    {
        Hs256,
        Ps256,
        Es256,
        EdDsa,
    };

    struct JwtParseOptions
    {
        NGIN::UIntSize maxHeaderBytes {4096};
        NGIN::UIntSize maxPayloadBytes {65536};
        NGIN::UIntSize maxSignatureBytes {8192};
    };

    struct JwtClaims
    {
        std::string                           issuer;
        bool                                  hasIssuer {false};
        std::string                           subject;
        bool                                  hasSubject {false};
        NGIN::Containers::Vector<std::string> audiences;
        NGIN::Int64                           expirationTime {0};
        bool                                  hasExpirationTime {false};
        NGIN::Int64                           notBefore {0};
        bool                                  hasNotBefore {false};
        NGIN::Int64                           issuedAt {0};
        bool                                  hasIssuedAt {false};
    };

    struct JwtCompactToken
    {
        JwtAlgorithm algorithm {JwtAlgorithm::Hs256};
        std::string  headerJson;
        std::string  payloadJson;
        std::string  signingInput;
        ByteBuffer   signature;
        JwtClaims    claims;
    };

    struct JwtValidationKey
    {
        JwtAlgorithm                     algorithm {JwtAlgorithm::Hs256};
        NGIN::Crypto::Memory::SecretView hmacKey;
        ConstByteSpan                    publicKey;
    };

    struct JwtValidationPolicy
    {
        bool                                    allowHs256 {false};
        bool                                    allowPs256 {false};
        bool                                    allowEs256 {false};
        bool                                    allowEdDsa {false};
        std::string_view                        expectedIssuer;
        std::string_view                        expectedAudience;
        NGIN::Int64                             currentUnixTimeSeconds {0};
        NGIN::Int64                             allowedClockSkewSeconds {0};
        bool                                    requireExpiration {true};
        bool                                    validateExpiration {true};
        bool                                    validateNotBefore {true};
        std::initializer_list<std::string_view> requiredClaims {};
        JwtParseOptions                         parseOptions {};
    };

    [[nodiscard]] CryptoExpected<JwtCompactToken> ParseJwtCompact(
            std::string_view token,
            JwtParseOptions  options = {});

    [[nodiscard]] CryptoExpected<bool>        HasJwtClaim(const JwtCompactToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<std::string> GetJwtStringClaim(const JwtCompactToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<NGIN::Int64> GetJwtInt64Claim(const JwtCompactToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<bool>        GetJwtBoolClaim(const JwtCompactToken& token, std::string_view name);

    [[nodiscard]] CryptoExpected<JwtCompactToken> ValidateJwt(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            const JwtValidationKey&                     key,
            const JwtValidationPolicy&                  policy);
}// namespace NGIN::Crypto::Tokens
