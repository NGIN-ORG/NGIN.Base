/// @file Utf8.hpp
/// @brief UTF-8 decode, encode, and validation primitives.
#pragma once

#include <NGIN/Text/Unicode/Detail.hpp>

#include <string_view>

namespace NGIN::Text::Unicode
{
    /// @brief Decodes one UTF-8 sequence starting at `offset`.
    ///
    /// @param input UTF-8 byte sequence to decode from.
    /// @param offset Code-unit offset into `input`.
    /// @return Decode result including the decoded code point or the encountered error.
    [[nodiscard]] inline constexpr DecodeResult DecodeUtf8(std::string_view input, UIntSize offset = 0) noexcept
    {
        if (offset >= input.size())
            return DecodeResult {0, 0, EncodingError::UnexpectedEnd};

        const auto remaining = static_cast<UIntSize>(input.size() - offset);
        const auto lead      = static_cast<unsigned char>(input[offset]);

        if (lead <= 0x7F)
            return DecodeResult {static_cast<CodePoint>(lead), 1, EncodingError::None};

        UIntSize  expectedUnits = 0;
        CodePoint minCodePoint  = 0;
        if (lead >= 0xC0 && lead <= 0xDF)
        {
            expectedUnits = 2;
            minCodePoint  = 0x80;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            expectedUnits = 3;
            minCodePoint  = 0x800;
        }
        else if (lead >= 0xF0 && lead <= 0xF7)
        {
            expectedUnits = 4;
            minCodePoint  = 0x10000;
        }
        else
        {
            return DecodeResult {0, 1, EncodingError::InvalidSequence};
        }

        if (remaining < expectedUnits)
            return DecodeResult {0, remaining, EncodingError::UnexpectedEnd};

        CodePoint codePoint = static_cast<CodePoint>(lead & ((1u << (8u - static_cast<unsigned>(expectedUnits) - 1u)) - 1u));
        for (UIntSize index = 1; index < expectedUnits; ++index)
        {
            const auto continuation = static_cast<unsigned char>(input[offset + index]);
            if ((continuation & 0xC0u) != 0x80u)
                return DecodeResult {0, index, EncodingError::InvalidSequence};

            codePoint = static_cast<CodePoint>((codePoint << 6u) | static_cast<CodePoint>(continuation & 0x3Fu));
        }

        if (codePoint < minCodePoint)
            return DecodeResult {0, expectedUnits, EncodingError::OverlongSequence};
        if (detail::IsSurrogate(codePoint))
            return DecodeResult {0, expectedUnits, EncodingError::SurrogateCodePoint};
        if (codePoint > detail::MaxCodePoint)
            return DecodeResult {0, expectedUnits, EncodingError::CodePointTooLarge};

        return DecodeResult {codePoint, expectedUnits, EncodingError::None};
    }

    /// @brief Encodes one code point as UTF-8.
    ///
    /// @param codePoint Unicode scalar value to encode. Invalid values are sanitized to U+FFFD.
    /// @param out Output buffer with space for at least 4 bytes.
    /// @return Number of bytes written to `out`.
    [[nodiscard]] inline constexpr UIntSize EncodeUtf8(CodePoint codePoint, char* out) noexcept
    {
        const CodePoint value = detail::SanitizeForEncoding(codePoint);
        if (value <= 0x7F)
        {
            out[0] = static_cast<char>(value);
            return 1;
        }
        if (value <= 0x7FF)
        {
            out[0] = static_cast<char>(0xC0u | static_cast<unsigned>(value >> 6u));
            out[1] = static_cast<char>(0x80u | static_cast<unsigned>(value & 0x3Fu));
            return 2;
        }
        if (value <= 0xFFFF)
        {
            out[0] = static_cast<char>(0xE0u | static_cast<unsigned>(value >> 12u));
            out[1] = static_cast<char>(0x80u | static_cast<unsigned>((value >> 6u) & 0x3Fu));
            out[2] = static_cast<char>(0x80u | static_cast<unsigned>(value & 0x3Fu));
            return 3;
        }

        out[0] = static_cast<char>(0xF0u | static_cast<unsigned>(value >> 18u));
        out[1] = static_cast<char>(0x80u | static_cast<unsigned>((value >> 12u) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | static_cast<unsigned>((value >> 6u) & 0x3Fu));
        out[3] = static_cast<char>(0x80u | static_cast<unsigned>(value & 0x3Fu));
        return 4;
    }

    /// @brief Returns whether the entire byte range is valid UTF-8.
    [[nodiscard]] inline constexpr bool IsValidUtf8(std::string_view input) noexcept
    {
        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf8(input, offset);
            if (decoded.error != EncodingError::None)
                return false;
            offset += decoded.unitsConsumed;
        }
        return true;
    }
}// namespace NGIN::Text::Unicode
