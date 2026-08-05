/// @file Paseto.hpp
/// @brief PASETO v4.public and v4.local parsing, validation, and sealing.
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
    /// @brief Supported PASETO protocol versions.
    enum class PasetoVersion : NGIN::UInt8
    {
        V4,
    };

    /// @brief PASETO token purpose.
    enum class PasetoPurpose : NGIN::UInt8
    {
        Local,
        Public,
    };

    /// @brief Resource limits applied while parsing PASETO tokens and JSON claims.
    struct PasetoParseOptions
    {
        NGIN::UIntSize maxPayloadBytes {65536};
        NGIN::UIntSize maxFooterBytes {8192};
        NGIN::UIntSize maxImplicitBytes {8192};
    };

    /// @brief Parsed and verified v4.public token fields.
    struct PasetoV4PublicToken
    {
        std::string payloadJson;
        std::string footer;
        ByteBuffer  signature;
    };

    /// @brief Authenticated plaintext fields recovered from a v4.local token.
    struct PasetoV4LocalToken
    {
        std::string payloadJson;
        std::string footer;
        ByteBuffer  nonce;
    };

    /// @brief Footer, implicit assertion, and required-claim policy used during validation.
    struct PasetoValidationPolicy
    {
        ConstByteSpan                           expectedFooter {};
        ConstByteSpan                           implicitAssertion {};
        std::initializer_list<std::string_view> requiredClaims;
        PasetoParseOptions                      parseOptions {};
    };

    /// @brief Footer, implicit assertion, and resource limits used while sealing a local token.
    struct PasetoSealOptions
    {
        ConstByteSpan      footer {};
        ConstByteSpan      implicitAssertion {};
        PasetoParseOptions limits {};
    };

    /// @brief Parses the structure of a v4.public token without verifying its signature.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<PasetoV4PublicToken> ParsePasetoV4Public(
            std::string_view   token,
            PasetoParseOptions options = {});

    /// @brief Returns whether a parsed public token contains a named top-level claim.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<bool> HasPasetoClaim(
            const PasetoV4PublicToken& token, std::string_view name);
    /// @brief Returns whether an opened local token contains a named top-level claim.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<bool> HasPasetoClaim(
            const PasetoV4LocalToken& token, std::string_view name);
    /// @brief Reads a named string claim from a parsed public token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<std::string> GetPasetoStringClaim(
            const PasetoV4PublicToken& token,
            std::string_view           name);
    /// @brief Reads a named string claim from an opened local token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<std::string> GetPasetoStringClaim(
            const PasetoV4LocalToken& token,
            std::string_view          name);
    /// @brief Reads a named signed-integer claim from a parsed public token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(
            const PasetoV4PublicToken& token,
            std::string_view           name);
    /// @brief Reads a named signed-integer claim from an opened local token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(
            const PasetoV4LocalToken& token,
            std::string_view          name);
    /// @brief Reads a named boolean claim from a parsed public token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<bool> GetPasetoBoolClaim(
            const PasetoV4PublicToken& token, std::string_view name);
    /// @brief Reads a named boolean claim from an opened local token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<bool> GetPasetoBoolClaim(
            const PasetoV4LocalToken& token, std::string_view name);

    /// @brief Verifies a v4.public token and enforces footer, assertion, and claim policy.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<PasetoV4PublicToken> ValidatePasetoV4Public(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            ConstByteSpan                               publicKey,
            const PasetoValidationPolicy&               policy = {});

    /// @brief Authenticates and decrypts a v4.local token, then enforces validation policy.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<PasetoV4LocalToken> OpenPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoValidationPolicy&               policy = {});

    /// @brief Authenticates and encrypts JSON payload as a v4.local token.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<std::string> SealPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            payloadJson,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoSealOptions&                    options = {});
}// namespace NGIN::Crypto::Tokens
