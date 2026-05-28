#pragma once

#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <span>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Encoding
{
    /// @brief Controls whether Base64 encoders emit `=` padding.
    enum class Base64Padding
    {
        Required,
        Omit,
    };

    /// @brief Returns the maximum exact encoded length for `byteCount` bytes with the requested padding policy.
    [[nodiscard]] NGIN::UIntSize Base64EncodedLength(NGIN::UIntSize byteCount, Base64Padding padding = Base64Padding::Required) noexcept;

    /// @brief Encodes bytes as standard Base64 text.
    [[nodiscard]] CryptoExpected<std::string> EncodeBase64(ConstByteSpan input, Base64Padding padding = Base64Padding::Required);

    /// @brief Encodes bytes as standard Base64 text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> EncodeBase64Into(
            ConstByteSpan input, std::span<char> output, Base64Padding padding = Base64Padding::Required) noexcept;

    /// @brief Decodes strict standard Base64 text into an owned byte buffer.
    [[nodiscard]] CryptoExpected<ByteBuffer> DecodeBase64(std::string_view text);

    /// @brief Decodes strict standard Base64 text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> DecodeBase64Into(std::string_view text, ByteSpan output) noexcept;
}// namespace NGIN::Crypto::Encoding
