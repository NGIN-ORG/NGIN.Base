/// @file Types.hpp
/// @brief Core Unicode result and BOM types.
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/Unicode/ErrorPolicy.hpp>

namespace NGIN::Text::Unicode
{
    /// @brief Unicode scalar storage type used by the conversion layer.
    using CodePoint = char32_t;

    /// @brief Result of decoding a single encoded sequence.
    struct DecodeResult
    {
        /// @brief Decoded code point when `error == EncodingError::None`.
        CodePoint     codePoint {0};
        /// @brief Number of input code units consumed while decoding or diagnosing the sequence.
        UIntSize      unitsConsumed {0};
        /// @brief Decode status for the attempted sequence.
        EncodingError error {EncodingError::None};
    };

    /// @brief Error payload returned by strict conversion helpers.
    struct ConversionError
    {
        /// @brief Failure category.
        EncodingError error {EncodingError::None};
        /// @brief Offset in the source input where the failure was observed.
        UIntSize      inputOffset {0};
    };

    /// @brief Recognized byte-order marks for supported Unicode encodings.
    enum class BomKind
    {
        /// @brief No known BOM prefix was detected.
        None,
        /// @brief UTF-8 BOM (`EF BB BF`).
        Utf8,
        /// @brief UTF-16 little-endian BOM (`FF FE`).
        Utf16LE,
        /// @brief UTF-16 big-endian BOM (`FE FF`).
        Utf16BE,
        /// @brief UTF-32 little-endian BOM (`FF FE 00 00`).
        Utf32LE,
        /// @brief UTF-32 big-endian BOM (`00 00 FE FF`).
        Utf32BE
    };

    /// @brief Describes a detected BOM prefix.
    struct BomInfo
    {
        /// @brief Detected BOM kind.
        BomKind  kind {BomKind::None};
        /// @brief Number of leading bytes occupied by the BOM.
        UIntSize bytes {0};
    };
}// namespace NGIN::Text::Unicode
