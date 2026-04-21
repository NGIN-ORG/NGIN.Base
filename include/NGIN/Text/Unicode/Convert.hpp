/// @file Convert.hpp
/// @brief Unicode conversion and helper APIs built on decode/encode primitives.
#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/Text/Unicode/Utf16.hpp>
#include <NGIN/Text/Unicode/Utf32.hpp>
#include <NGIN/Text/Unicode/Utf8.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace NGIN::Text::Unicode
{
    namespace detail
    {
        [[nodiscard]] inline constexpr ConversionError MakeConversionError(EncodingError error, UIntSize inputOffset) noexcept
        {
            return ConversionError {error, inputOffset};
        }

        inline void AppendCodePoint(UTF8String& output, CodePoint codePoint)
        {
            char buffer[4] {};
            const UIntSize count = EncodeUtf8(codePoint, buffer);
            output.Append(buffer, count);
        }

        inline void AppendCodePoint(UTF16String& output, CodePoint codePoint)
        {
            char16_t buffer[2] {};
            const UIntSize count = EncodeUtf16(codePoint, buffer);
            output.Append(buffer, count);
        }

        inline void AppendCodePoint(UTF32String& output, CodePoint codePoint)
        {
            char32_t buffer[1] {};
            (void)EncodeUtf32(codePoint, buffer);
            output.Append(buffer[0]);
        }
    }// namespace detail

    /// @brief Converts UTF-8 input into `UTF32String`.
    ///
    /// @param input Source UTF-8 code units.
    /// @param policy Error-handling policy applied to malformed input.
    /// @return Converted UTF-32 text or the first strict-mode error.
    [[nodiscard]] inline Utilities::Expected<UTF32String, ConversionError> ToUtf32(std::string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF32String output;
        output.ReserveExact(input.size());

        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf8(input, offset);
            if (decoded.error == EncodingError::None)
            {
                detail::AppendCodePoint(output, decoded.codePoint);
                offset += decoded.unitsConsumed;
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF32String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(decoded.error, offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);

            offset += detail::ClampConsumed(decoded.unitsConsumed, input.size() - offset);
        }

        return Utilities::Expected<UTF32String, ConversionError>(std::move(output));
    }

    /// @brief Converts UTF-16 input into `UTF32String`.
    [[nodiscard]] inline Utilities::Expected<UTF32String, ConversionError> ToUtf32(std::u16string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF32String output;
        output.ReserveExact(input.size());

        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf16(input, offset);
            if (decoded.error == EncodingError::None)
            {
                detail::AppendCodePoint(output, decoded.codePoint);
                offset += decoded.unitsConsumed;
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF32String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(decoded.error, offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);

            offset += detail::ClampConsumed(decoded.unitsConsumed, input.size() - offset);
        }

        return Utilities::Expected<UTF32String, ConversionError>(std::move(output));
    }

    /// @brief Converts UTF-8 input into `UTF16String`.
    [[nodiscard]] inline Utilities::Expected<UTF16String, ConversionError> ToUtf16(std::string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF16String output;
        output.ReserveExact(input.size());

        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf8(input, offset);
            if (decoded.error == EncodingError::None)
            {
                detail::AppendCodePoint(output, decoded.codePoint);
                offset += decoded.unitsConsumed;
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF16String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(decoded.error, offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);

            offset += detail::ClampConsumed(decoded.unitsConsumed, input.size() - offset);
        }

        return Utilities::Expected<UTF16String, ConversionError>(std::move(output));
    }

    /// @brief Converts UTF-32 input into `UTF16String`.
    [[nodiscard]] inline Utilities::Expected<UTF16String, ConversionError> ToUtf16(std::u32string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF16String output;
        output.ReserveExact(input.size());

        for (UIntSize offset = 0; offset < input.size(); ++offset)
        {
            const CodePoint codePoint = static_cast<CodePoint>(input[offset]);
            if (detail::IsValidCodePoint(codePoint))
            {
                detail::AppendCodePoint(output, codePoint);
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF16String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(detail::IsSurrogate(codePoint) ? EncodingError::SurrogateCodePoint
                                                                                   : EncodingError::CodePointTooLarge,
                                                    offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);
        }

        return Utilities::Expected<UTF16String, ConversionError>(std::move(output));
    }

    /// @brief Converts UTF-16 input into `UTF8String`.
    [[nodiscard]] inline Utilities::Expected<UTF8String, ConversionError> ToUtf8(std::u16string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF8String output;
        output.ReserveExact(input.size() * 2);

        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf16(input, offset);
            if (decoded.error == EncodingError::None)
            {
                detail::AppendCodePoint(output, decoded.codePoint);
                offset += decoded.unitsConsumed;
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF8String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(decoded.error, offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);

            offset += detail::ClampConsumed(decoded.unitsConsumed, input.size() - offset);
        }

        return Utilities::Expected<UTF8String, ConversionError>(std::move(output));
    }

    /// @brief Converts UTF-32 input into `UTF8String`.
    [[nodiscard]] inline Utilities::Expected<UTF8String, ConversionError> ToUtf8(std::u32string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UTF8String output;
        output.ReserveExact(input.size() * 4);

        for (UIntSize offset = 0; offset < input.size(); ++offset)
        {
            const CodePoint codePoint = static_cast<CodePoint>(input[offset]);
            if (detail::IsValidCodePoint(codePoint))
            {
                detail::AppendCodePoint(output, codePoint);
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UTF8String, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(detail::IsSurrogate(codePoint) ? EncodingError::SurrogateCodePoint
                                                                                   : EncodingError::CodePointTooLarge,
                                                    offset)));

            if (policy == ErrorPolicy::Replace)
                detail::AppendCodePoint(output, detail::ReplacementCharacter);
        }

        return Utilities::Expected<UTF8String, ConversionError>(std::move(output));
    }

    /// @brief Counts Unicode code points in UTF-8 input.
    ///
    /// @param input Source UTF-8 bytes.
    /// @param policy Error-handling policy applied while counting.
    /// @return Number of code points or the first strict-mode error.
    [[nodiscard]] inline Utilities::Expected<UIntSize, ConversionError> CountCodePoints(std::string_view input, ErrorPolicy policy = ErrorPolicy::Strict)
    {
        UIntSize count  = 0;
        UIntSize offset = 0;
        while (offset < input.size())
        {
            const DecodeResult decoded = DecodeUtf8(input, offset);
            if (decoded.error == EncodingError::None)
            {
                ++count;
                offset += decoded.unitsConsumed;
                continue;
            }

            if (policy == ErrorPolicy::Strict)
                return Utilities::Expected<UIntSize, ConversionError>(Utilities::Unexpected<ConversionError>(
                        detail::MakeConversionError(decoded.error, offset)));

            if (policy == ErrorPolicy::Replace)
                ++count;

            offset += detail::ClampConsumed(decoded.unitsConsumed, input.size() - offset);
        }

        return Utilities::Expected<UIntSize, ConversionError>(count);
    }

    /// @brief Returns whether the entire byte range is ASCII.
    [[nodiscard]] inline constexpr bool IsAscii(std::string_view input) noexcept
    {
        for (const char ch: input)
        {
            if (static_cast<unsigned char>(ch) > 0x7Fu)
                return false;
        }
        return true;
    }

    /// @brief Detects a leading Unicode byte-order mark.
    ///
    /// @param bytes Raw input bytes to inspect.
    /// @return Detected BOM kind and byte count, or `BomKind::None`.
    [[nodiscard]] inline constexpr BomInfo DetectBom(std::span<const Byte> bytes) noexcept
    {
        auto byteAt = [&](UIntSize index) noexcept -> unsigned char
        {
            return std::to_integer<unsigned char>(bytes[index]);
        };

        if (bytes.size() >= 4)
        {
            if (byteAt(0) == 0x00u && byteAt(1) == 0x00u && byteAt(2) == 0xFEu && byteAt(3) == 0xFFu)
                return BomInfo {BomKind::Utf32BE, 4};
            if (byteAt(0) == 0xFFu && byteAt(1) == 0xFEu && byteAt(2) == 0x00u && byteAt(3) == 0x00u)
                return BomInfo {BomKind::Utf32LE, 4};
        }

        if (bytes.size() >= 3 && byteAt(0) == 0xEFu && byteAt(1) == 0xBBu && byteAt(2) == 0xBFu)
            return BomInfo {BomKind::Utf8, 3};

        if (bytes.size() >= 2)
        {
            if (byteAt(0) == 0xFEu && byteAt(1) == 0xFFu)
                return BomInfo {BomKind::Utf16BE, 2};
            if (byteAt(0) == 0xFFu && byteAt(1) == 0xFEu)
                return BomInfo {BomKind::Utf16LE, 2};
        }

        return BomInfo {};
    }

    /// @brief Removes a UTF-8 BOM prefix when present.
    ///
    /// @param input UTF-8 byte range.
    /// @return `input` without a leading UTF-8 BOM.
    [[nodiscard]] inline constexpr std::string_view StripBom(std::string_view input) noexcept
    {
        return input.starts_with("\xEF\xBB\xBF") ? input.substr(3) : input;
    }
}// namespace NGIN::Text::Unicode
