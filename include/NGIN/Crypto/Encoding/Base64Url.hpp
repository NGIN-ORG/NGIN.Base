#pragma once

#include <NGIN/Crypto/Encoding/Base64.hpp>

namespace NGIN::Crypto::Encoding
{
    /// @brief Encodes bytes as URL-safe Base64 text.
    [[nodiscard]] CryptoExpected<std::string> EncodeBase64Url(ConstByteSpan input, Base64Padding padding = Base64Padding::Omit);

    /// @brief Encodes bytes as URL-safe Base64 text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> EncodeBase64UrlInto(
            ConstByteSpan input, std::span<char> output, Base64Padding padding = Base64Padding::Omit) noexcept;

    /// @brief Decodes strict URL-safe Base64 text into an owned byte buffer.
    [[nodiscard]] CryptoExpected<ByteBuffer> DecodeBase64Url(std::string_view text);

    /// @brief Decodes strict URL-safe Base64 text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> DecodeBase64UrlInto(std::string_view text, ByteSpan output);
}// namespace NGIN::Crypto::Encoding
