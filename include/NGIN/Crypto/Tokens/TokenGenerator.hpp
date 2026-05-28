#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Tokens/SecureToken.hpp>

namespace NGIN::Crypto::Tokens
{
    enum class TokenEncoding
    {
        Base64Url,
        Hex,
    };

    struct TokenOptions
    {
        NGIN::UIntSize byteLength {32};
        NGIN::UIntSize minimumEntropyBytes {16};
        TokenEncoding  encoding {TokenEncoding::Base64Url};
    };

    /// @brief Generates random opaque token bytes.
    [[nodiscard]] CryptoExpected<ByteBuffer> GenerateBytes(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::UIntSize                              byteLength,
            NGIN::UIntSize                              minimumEntropyBytes = 16);

    /// @brief Generates random token text using the encoding selected in `options`.
    [[nodiscard]] CryptoExpected<SecureToken> GenerateToken(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options = {});

    /// @brief Generates random token text encoded as lowercase hexadecimal.
    [[nodiscard]] CryptoExpected<SecureToken> GenerateHex(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options = {});

    /// @brief Generates random token text encoded as unpadded URL-safe Base64.
    [[nodiscard]] CryptoExpected<SecureToken> GenerateBase64Url(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options = {});
}// namespace NGIN::Crypto::Tokens
