/// @file Types.hpp
/// @brief Core Unicode result and BOM types.
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/Unicode/ErrorPolicy.hpp>

namespace NGIN::Text::Unicode
{
    using CodePoint = char32_t;

    struct DecodeResult
    {
        CodePoint     codePoint {0};
        UIntSize      unitsConsumed {0};
        EncodingError error {EncodingError::None};
    };

    struct ConversionError
    {
        EncodingError error {EncodingError::None};
        UIntSize      inputOffset {0};
    };

    enum class BomKind
    {
        None,
        Utf8,
        Utf16LE,
        Utf16BE,
        Utf32LE,
        Utf32BE
    };

    struct BomInfo
    {
        BomKind  kind {BomKind::None};
        UIntSize bytes {0};
    };
}// namespace NGIN::Text::Unicode
