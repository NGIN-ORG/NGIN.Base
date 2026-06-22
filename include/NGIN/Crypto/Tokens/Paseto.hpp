#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Tokens
{
    enum class PasetoVersion : NGIN::UInt8
    {
        V4,
    };

    enum class PasetoPurpose : NGIN::UInt8
    {
        Local,
        Public,
    };

    struct PasetoParseOptions
    {
        NGIN::UIntSize maxPayloadBytes {65536};
        NGIN::UIntSize maxFooterBytes {8192};
        NGIN::UIntSize maxImplicitBytes {8192};
    };

    struct PasetoV4PublicToken
    {
        std::string payloadJson;
        std::string footer;
        ByteBuffer  signature;
    };

    struct PasetoV4LocalToken
    {
        std::string payloadJson;
        std::string footer;
        ByteBuffer  nonce;
    };

    struct PasetoValidationPolicy
    {
        ConstByteSpan                           expectedFooter {};
        ConstByteSpan                           implicitAssertion {};
        std::initializer_list<std::string_view> requiredClaims {};
        PasetoParseOptions                      parseOptions {};
    };

    struct PasetoSealOptions
    {
        ConstByteSpan      footer {};
        ConstByteSpan      implicitAssertion {};
        PasetoParseOptions limits {};
    };

    [[nodiscard]] CryptoExpected<PasetoV4PublicToken> ParsePasetoV4Public(
            std::string_view   token,
            PasetoParseOptions options = {});

    [[nodiscard]] CryptoExpected<bool>        HasPasetoClaim(const PasetoV4PublicToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<bool>        HasPasetoClaim(const PasetoV4LocalToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<std::string> GetPasetoStringClaim(
            const PasetoV4PublicToken& token,
            std::string_view           name);
    [[nodiscard]] CryptoExpected<std::string> GetPasetoStringClaim(
            const PasetoV4LocalToken& token,
            std::string_view          name);
    [[nodiscard]] CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(
            const PasetoV4PublicToken& token,
            std::string_view           name);
    [[nodiscard]] CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(
            const PasetoV4LocalToken& token,
            std::string_view          name);
    [[nodiscard]] CryptoExpected<bool> GetPasetoBoolClaim(const PasetoV4PublicToken& token, std::string_view name);
    [[nodiscard]] CryptoExpected<bool> GetPasetoBoolClaim(const PasetoV4LocalToken& token, std::string_view name);

    [[nodiscard]] CryptoExpected<PasetoV4PublicToken> ValidatePasetoV4Public(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            ConstByteSpan                               publicKey,
            const PasetoValidationPolicy&               policy = {});

    [[nodiscard]] CryptoExpected<PasetoV4LocalToken> OpenPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoValidationPolicy&               policy = {});

    [[nodiscard]] CryptoExpected<std::string> SealPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            payloadJson,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoSealOptions&                    options = {});
}// namespace NGIN::Crypto::Tokens
