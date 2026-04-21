/// @file Detail.hpp
/// @brief Internal helpers shared across Unicode encoding headers.
#pragma once

#include <NGIN/Text/Unicode/Types.hpp>

#include <algorithm>

namespace NGIN::Text::Unicode::detail
{
    static constexpr CodePoint ReplacementCharacter = static_cast<CodePoint>(0xFFFDu);
    static constexpr CodePoint MaxCodePoint         = static_cast<CodePoint>(0x10FFFFu);

    [[nodiscard]] constexpr bool IsHighSurrogate(char16_t value) noexcept
    {
        return value >= 0xD800 && value <= 0xDBFF;
    }

    [[nodiscard]] constexpr bool IsLowSurrogate(char16_t value) noexcept
    {
        return value >= 0xDC00 && value <= 0xDFFF;
    }

    [[nodiscard]] constexpr bool IsSurrogate(CodePoint value) noexcept
    {
        return value >= 0xD800 && value <= 0xDFFF;
    }

    [[nodiscard]] constexpr bool IsValidCodePoint(CodePoint value) noexcept
    {
        return value <= MaxCodePoint && !IsSurrogate(value);
    }

    [[nodiscard]] constexpr UIntSize ClampConsumed(UIntSize consumed, UIntSize remaining) noexcept
    {
        return consumed == 0 ? 1 : std::min(consumed, remaining);
    }

    [[nodiscard]] constexpr CodePoint SanitizeForEncoding(CodePoint value) noexcept
    {
        return IsValidCodePoint(value) ? value : ReplacementCharacter;
    }
}// namespace NGIN::Text::Unicode::detail
