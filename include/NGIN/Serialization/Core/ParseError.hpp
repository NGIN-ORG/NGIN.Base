#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Serialization
{
    using NGIN::Text::String;

    /// @brief Structured parse error with location and human-readable context.
    enum class ParseErrorCode : UInt8
    {
        None,
        UnexpectedEnd,
        UnexpectedCharacter,
        InvalidToken,
        InvalidNumber,
        InvalidStringEscape,
        InvalidUnicodeEscape,
        InvalidEntity,
        DepthExceeded,
        TrailingCharacters,
        HandlerRejected,
        OutOfMemory,
        MismatchedTag,
    };

    /// @brief Byte offset and optional line/column position for parse errors.
    struct ParseLocation
    {
        UIntSize offset {0};
        UIntSize line {0};
        UIntSize column {0};

        [[nodiscard]] static constexpr ParseLocation Unknown() noexcept
        {
            return ParseLocation {};
        }
    };

    /// @brief Parsing error payload with code, location, and message.
    struct ParseError
    {
        ParseErrorCode code {ParseErrorCode::None};
        ParseLocation  location {};
        String         message {};
    };
}// namespace NGIN::Serialization
