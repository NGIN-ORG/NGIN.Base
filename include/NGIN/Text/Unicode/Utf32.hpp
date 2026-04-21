/// @file Utf32.hpp
/// @brief UTF-32 encode and validation primitives.
#pragma once

#include <NGIN/Text/Unicode/Detail.hpp>

#include <string_view>

namespace NGIN::Text::Unicode
{
    /// @brief Encodes one code point as UTF-32.
    ///
    /// @param codePoint Unicode scalar value to encode. Invalid values are sanitized to U+FFFD.
    /// @param out Output buffer with space for one UTF-32 code unit.
    /// @return Number of UTF-32 code units written to `out`.
    [[nodiscard]] inline constexpr UIntSize EncodeUtf32(CodePoint codePoint, char32_t* out) noexcept
    {
        out[0] = static_cast<char32_t>(detail::SanitizeForEncoding(codePoint));
        return 1;
    }

    /// @brief Returns whether the entire code-unit range is valid UTF-32 scalar data.
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
