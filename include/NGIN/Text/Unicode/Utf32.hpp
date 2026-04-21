/// @file Utf32.hpp
/// @brief UTF-32 encode and validation primitives.
#pragma once

#include <NGIN/Text/Unicode/Detail.hpp>

#include <string_view>

namespace NGIN::Text::Unicode
{
    [[nodiscard]] inline constexpr UIntSize EncodeUtf32(CodePoint codePoint, char32_t* out) noexcept
    {
        out[0] = static_cast<char32_t>(detail::SanitizeForEncoding(codePoint));
        return 1;
    }

    [[nodiscard]] inline constexpr bool IsValidUtf32(std::u32string_view input) noexcept
    {
        for (const char32_t value: input)
        {
            if (!detail::IsValidCodePoint(static_cast<CodePoint>(value)))
                return false;
        }
        return true;
    }
}// namespace NGIN::Text::Unicode
