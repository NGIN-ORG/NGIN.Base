/// @file Utf16.hpp
/// @brief UTF-16 decode, encode, and validation primitives.
#pragma once

#include <NGIN/Text/Unicode/Detail.hpp>

#include <string_view>

namespace NGIN::Text::Unicode
{
    [[nodiscard]] inline constexpr DecodeResult DecodeUtf16(std::u16string_view input, UIntSize offset = 0) noexcept
    {
        if (offset >= input.size())
            return DecodeResult {0, 0, EncodingError::UnexpectedEnd};

        const char16_t first = input[offset];
        if (!detail::IsHighSurrogate(first))
        {
            if (detail::IsLowSurrogate(first))
                return DecodeResult {0, 1, EncodingError::UnpairedSurrogate};
            return DecodeResult {static_cast<CodePoint>(first), 1, EncodingError::None};
        }

        if (offset + 1 >= input.size())
            return DecodeResult {0, 1, EncodingError::UnexpectedEnd};

        const char16_t second = input[offset + 1];
        if (!detail::IsLowSurrogate(second))
            return DecodeResult {0, 1, EncodingError::UnpairedSurrogate};

        const CodePoint high = static_cast<CodePoint>(first - 0xD800u);
        const CodePoint low  = static_cast<CodePoint>(second - 0xDC00u);
        return DecodeResult {static_cast<CodePoint>(0x10000u + ((high << 10u) | low)), 2, EncodingError::None};
    }

    [[nodiscard]] inline constexpr UIntSize EncodeUtf16(CodePoint codePoint, char16_t* out) noexcept
    {
        const CodePoint value = detail::SanitizeForEncoding(codePoint);
        if (value <= 0xFFFF)
        {
            out[0] = static_cast<char16_t>(value);
            return 1;
        }

        const CodePoint shifted = static_cast<CodePoint>(value - 0x10000u);
        out[0] = static_cast<char16_t>(0xD800u + (shifted >> 10u));
        out[1] = static_cast<char16_t>(0xDC00u + (shifted & 0x3FFu));
        return 2;
    }

    [[nodiscard]] inline constexpr bool IsValidUtf16(std::u16string_view input) noexcept
    {
        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf16(input, offset);
            if (decoded.error != EncodingError::None)
                return false;
            offset += decoded.unitsConsumed;
        }
        return true;
    }
}// namespace NGIN::Text::Unicode
