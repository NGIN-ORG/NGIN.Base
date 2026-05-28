#pragma once

#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <span>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Encoding
{
    /// @brief Returns the exact number of characters needed to hex-encode `byteCount` bytes.
    [[nodiscard]] constexpr NGIN::UIntSize HexEncodedLength(NGIN::UIntSize byteCount) noexcept
    {
        return byteCount * 2;
    }

    /// @brief Returns the exact number of bytes produced by a valid hex string, or 0 for invalid odd-length input.
    [[nodiscard]] constexpr NGIN::UIntSize HexDecodedLength(std::string_view text) noexcept
    {
        return (text.size() % 2) == 0 ? text.size() / 2 : 0;
    }

    /// @brief Encodes bytes as lowercase hexadecimal text.
    [[nodiscard]] CryptoExpected<std::string> EncodeHex(ConstByteSpan input);

    /// @brief Encodes bytes as lowercase hexadecimal text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> EncodeHexInto(ConstByteSpan input, std::span<char> output) noexcept;

    /// @brief Decodes strict hexadecimal text into an owned byte buffer.
    [[nodiscard]] CryptoExpected<ByteBuffer> DecodeHex(std::string_view text);

    /// @brief Decodes strict hexadecimal text into caller-owned output.
    [[nodiscard]] CryptoExpected<void> DecodeHexInto(std::string_view text, ByteSpan output) noexcept;
}// namespace NGIN::Crypto::Encoding
