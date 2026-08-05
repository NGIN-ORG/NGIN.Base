#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>
#include <NGIN/Text/String.hpp>

#include <optional>

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
        InvalidEncoding,
        DuplicateName,
        LimitExceeded,
        UnsupportedConstruct,
        InvalidDocumentStructure,
    };

    /// @brief Byte offset and optional line/column position for parse errors.
    struct ParseLocation
    {
        UIntSize offset {0};
        UIntSize line {0};
        UIntSize column {0};

        /// @brief Returns the sentinel location used when no source position is available.
        [[nodiscard]] static constexpr ParseLocation Unknown() noexcept
        {
            return ParseLocation {};
        }
    };

    /// @brief Parsing error payload with code, location, and message.
    struct ParseError
    {
        ParseErrorCode            code {ParseErrorCode::None};
        ParseLocation             location {};
        SourceSpan                span {};
        std::optional<SourceSpan> related {};
        UInt64                    consumerContext {0};
        String                    message {};
    };

    using ParseDiagnostic = ParseError;
}// namespace NGIN::Serialization
