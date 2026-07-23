#include <NGIN/Serialization/JSON/JsonEventParser.hpp>

#include <NGIN/SIMD/Scan.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

namespace NGIN::Serialization::JSON::detail
{
    namespace
    {
        struct ParsedString
        {
            std::string_view value {};
            SourceSpan       span {};
        };

        struct SeenKey
        {
            std::string_view value {};
            SourceSpan       span {};
        };

        struct ParseContext
        {
            InputCursor      cursor;
            std::string_view source;
            SourceId         sourceId {};
            ParseOptions     options {};
            ParseLimits      limits {};
            ParseScratch*    scratch {nullptr};
            void*            handlerContext {nullptr};
            EventCallback    callback {nullptr};
            UIntSize         depth {0};
            UIntSize         nodeCount {0};
            UIntSize         memberCount {0};
            UIntSize         decodedBytes {0};
        };

        [[nodiscard]] SourceLocation Locate(std::string_view source,
                                            SourceId         sourceId,
                                            UIntSize         offset) noexcept
        {
            SourceLocation location {
                    .source = sourceId,
                    .offset = offset,
                    .line   = 1,
                    .column = 1,
            };
            bool           previousWasCarriageReturn = false;
            const UIntSize end                       = (std::min) (offset, source.size());
            for (UIntSize index = 0; index < end; ++index)
            {
                const char value = source[index];
                if (value == '\r')
                {
                    ++location.line;
                    location.column           = 1;
                    previousWasCarriageReturn = true;
                }
                else if (value == '\n')
                {
                    if (!previousWasCarriageReturn)
                        ++location.line;
                    location.column           = 1;
                    previousWasCarriageReturn = false;
                }
                else
                {
                    ++location.column;
                    previousWasCarriageReturn = false;
                }
            }
            return location;
        }

        [[nodiscard]] ParseDiagnostic MakeErrorAt(const ParseContext& context,
                                                  ParseErrorCode      code,
                                                  const char*         message,
                                                  UIntSize            begin,
                                                  UIntSize            end)
        {
            const auto      location = Locate(context.source, context.sourceId, begin);
            ParseDiagnostic error;
            error.code     = code;
            error.location = ParseLocation {
                    .offset = location.offset,
                    .line   = location.line,
                    .column = location.column,
            };
            error.span = SourceSpan {
                    .source = context.sourceId,
                    .begin  = begin,
                    .end    = (std::max) (begin, end),
            };
            error.message = message;
            return error;
        }

        [[nodiscard]] ParseDiagnostic MakeError(const ParseContext& context,
                                                ParseErrorCode      code,
                                                const char*         message)
        {
            return MakeErrorAt(
                    context, code, message, context.cursor.Offset(), context.cursor.Offset());
        }

        template<class T>
        [[nodiscard]] NGIN::Utilities::Expected<T, ParseDiagnostic>
        Failure(ParseDiagnostic error)
        {
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }

        [[nodiscard]] bool IsDigit(char value) noexcept
        {
            return value >= '0' && value <= '9';
        }

        [[nodiscard]] bool IsHexDigit(char value) noexcept
        {
            return (value >= '0' && value <= '9') ||
                   (value >= 'a' && value <= 'f') ||
                   (value >= 'A' && value <= 'F');
        }

        [[nodiscard]] UInt32 HexValue(char value) noexcept
        {
            if (value >= '0' && value <= '9')
                return static_cast<UInt32>(value - '0');
            if (value >= 'a' && value <= 'f')
                return static_cast<UInt32>(value - 'a' + 10);
            return static_cast<UInt32>(value - 'A' + 10);
        }

        [[nodiscard]] bool DecodeHex4(const char* digits, UInt32& result) noexcept
        {
            result = 0;
            for (UIntSize index = 0; index < 4; ++index)
            {
                if (!IsHexDigit(digits[index]))
                    return false;
                result = static_cast<UInt32>((result << 4U) | HexValue(digits[index]));
            }
            return true;
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        Deliver(ParseContext& context, const Event& event, bool emit)
        {
            if (!emit)
                return {};

            const EventAction action = context.callback(context.handlerContext, event);
            if (action.continueParsing)
                return {};

            ParseDiagnostic diagnostic;
            diagnostic.code            = ParseErrorCode::HandlerRejected;
            diagnostic.span            = event.span;
            diagnostic.location.offset = event.span.begin;
            diagnostic.consumerContext = action.consumerContext;
            diagnostic.message         = "JSON event handler stopped parsing";
            return Failure<void>(std::move(diagnostic));
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        BeginValue(ParseContext& context, UIntSize offset)
        {
            if (context.nodeCount >= context.limits.maxNodes)
            {
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::LimitExceeded,
                                                 "JSON node limit exceeded",
                                                 offset,
                                                 offset));
            }
            ++context.nodeCount;
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        SkipComment(ParseContext& context)
        {
            const UIntSize start = context.cursor.Offset();
            if (context.cursor.Peek() != '/')
                return {};

            if (context.cursor.Peek(1) == '/')
            {
                context.cursor.Advance(2);
                while (!context.cursor.IsEof())
                {
                    const char value = context.cursor.Peek();
                    if (value == '\r' || value == '\n')
                        break;
                    context.cursor.Advance();
                }
                return {};
            }

            if (context.cursor.Peek(1) == '*')
            {
                context.cursor.Advance(2);
                while (!context.cursor.IsEof())
                {
                    if (context.cursor.Peek() == '*' && context.cursor.Peek(1) == '/')
                    {
                        context.cursor.Advance(2);
                        return {};
                    }
                    context.cursor.Advance();
                }
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::UnexpectedEnd,
                                                 "Unterminated block comment",
                                                 start,
                                                 context.cursor.Offset()));
            }

            return Failure<void>(MakeErrorAt(context,
                                             ParseErrorCode::InvalidToken,
                                             "Invalid comment token",
                                             start,
                                             context.cursor.Offset() + 1));
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        SkipTrivia(ParseContext& context)
        {
            while (true)
            {
                context.cursor.SkipWhitespace();
                if (context.cursor.Peek() != '/')
                    return {};
                if (context.options.comments == CommentPolicy::Reject)
                {
                    return Failure<void>(MakeErrorAt(context,
                                                     ParseErrorCode::InvalidToken,
                                                     "JSON comments are disabled",
                                                     context.cursor.Offset(),
                                                     context.cursor.Offset() + 1));
                }
                auto comment = SkipComment(context);
                if (!comment)
                    return comment;
            }
        }

        [[nodiscard]] NGIN::Utilities::Expected<ParsedString, ParseDiagnostic>
        ParseString(ParseContext& context)
        {
            const UIntSize tokenStart = context.cursor.Offset();
            if (context.cursor.Peek() != '"')
            {
                return Failure<ParsedString>(
                        MakeError(context, ParseErrorCode::InvalidToken, "Expected JSON string"));
            }

            context.cursor.Advance();
            const char* start      = context.cursor.CurrentPtr();
            const char* scan       = start;
            bool        hasEscapes = false;

            while (scan && scan < context.cursor.EndPtr())
            {
                const auto remaining =
                        static_cast<std::size_t>(context.cursor.EndPtr() - scan);
                const auto  offset = NGIN::SIMD::FindAnyByte(scan, remaining, '"', '\\');
                const char* next   = scan + offset;

                for (const char* current = scan; current < next; ++current)
                {
                    if (static_cast<unsigned char>(*current) < 0x20U)
                    {
                        const UIntSize errorOffset =
                                tokenStart + 1 + static_cast<UIntSize>(current - start);
                        return Failure<ParsedString>(MakeErrorAt(context,
                                                                 ParseErrorCode::InvalidToken,
                                                                 "Control character in JSON string",
                                                                 errorOffset,
                                                                 errorOffset + 1));
                    }
                }

                if (next >= context.cursor.EndPtr())
                {
                    return Failure<ParsedString>(MakeErrorAt(context,
                                                             ParseErrorCode::UnexpectedEnd,
                                                             "Unterminated JSON string",
                                                             tokenStart,
                                                             context.source.size()));
                }

                if (*next == '"')
                {
                    scan = next;
                    break;
                }

                hasEscapes = true;
                scan       = next + 1;
                if (scan >= context.cursor.EndPtr())
                {
                    return Failure<ParsedString>(MakeErrorAt(
                            context,
                            ParseErrorCode::UnexpectedEnd,
                            "Unterminated JSON escape",
                            static_cast<UIntSize>(next - context.source.data()),
                            context.source.size()));
                }
                ++scan;
            }

            if (!scan || scan >= context.cursor.EndPtr())
            {
                return Failure<ParsedString>(MakeErrorAt(context,
                                                         ParseErrorCode::UnexpectedEnd,
                                                         "Unterminated JSON string",
                                                         tokenStart,
                                                         context.source.size()));
            }

            const UIntSize rawLength = static_cast<UIntSize>(scan - start);
            context.cursor.Advance(rawLength + 1);
            const SourceSpan span {
                    .source = context.sourceId,
                    .begin  = tokenStart,
                    .end    = context.cursor.Offset(),
            };

            if (!hasEscapes)
                return ParsedString {.value = {start, rawLength}, .span = span};

            if (context.decodedBytes > context.limits.maxDecodedStringBytes ||
                rawLength > context.limits.maxDecodedStringBytes - context.decodedBytes)
            {
                return Failure<ParsedString>(MakeErrorAt(context,
                                                         ParseErrorCode::LimitExceeded,
                                                         "Decoded JSON string limit exceeded",
                                                         tokenStart,
                                                         span.end));
            }

            char* output = context.scratch->TryAllocate(rawLength);
            if (!output)
            {
                return Failure<ParsedString>(MakeErrorAt(context,
                                                         ParseErrorCode::OutOfMemory,
                                                         "JSON event string allocation failed",
                                                         tokenStart,
                                                         span.end));
            }

            const char* read  = start;
            const char* end   = start + rawLength;
            char*       write = output;
            while (read < end)
            {
                const char value = *read++;
                if (value != '\\')
                {
                    *write++ = value;
                    continue;
                }

                const UIntSize escapeOffset =
                        tokenStart + 1 + static_cast<UIntSize>((read - 1) - start);
                if (read >= end)
                {
                    return Failure<ParsedString>(MakeErrorAt(context,
                                                             ParseErrorCode::InvalidStringEscape,
                                                             "Truncated JSON escape",
                                                             escapeOffset,
                                                             escapeOffset + 1));
                }

                switch (*read++)
                {
                    case '"':
                        *write++ = '"';
                        break;
                    case '\\':
                        *write++ = '\\';
                        break;
                    case '/':
                        *write++ = '/';
                        break;
                    case 'b':
                        *write++ = '\b';
                        break;
                    case 'f':
                        *write++ = '\f';
                        break;
                    case 'n':
                        *write++ = '\n';
                        break;
                    case 'r':
                        *write++ = '\r';
                        break;
                    case 't':
                        *write++ = '\t';
                        break;
                    case 'u': {
                        if (static_cast<UIntSize>(end - read) < 4)
                        {
                            return Failure<ParsedString>(MakeErrorAt(
                                    context,
                                    ParseErrorCode::InvalidUnicodeEscape,
                                    "Truncated JSON Unicode escape",
                                    escapeOffset,
                                    span.end));
                        }

                        UInt32 codePoint = 0;
                        if (!DecodeHex4(read, codePoint))
                        {
                            return Failure<ParsedString>(MakeErrorAt(
                                    context,
                                    ParseErrorCode::InvalidUnicodeEscape,
                                    "Invalid JSON Unicode escape",
                                    escapeOffset,
                                    escapeOffset + 6));
                        }
                        read += 4;

                        if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
                        {
                            if (static_cast<UIntSize>(end - read) < 6 ||
                                read[0] != '\\' ||
                                read[1] != 'u')
                            {
                                return Failure<ParsedString>(MakeErrorAt(
                                        context,
                                        ParseErrorCode::InvalidUnicodeEscape,
                                        "Missing low surrogate",
                                        escapeOffset,
                                        escapeOffset + 6));
                            }

                            UInt32 low = 0;
                            if (!DecodeHex4(read + 2, low) ||
                                low < 0xDC00U ||
                                low > 0xDFFFU)
                            {
                                return Failure<ParsedString>(MakeErrorAt(
                                        context,
                                        ParseErrorCode::InvalidUnicodeEscape,
                                        "Invalid low surrogate",
                                        escapeOffset + 6,
                                        escapeOffset + 12));
                            }
                            read += 6;
                            codePoint =
                                    0x10000U +
                                    ((codePoint - 0xD800U) << 10U) +
                                    (low - 0xDC00U);
                        }
                        else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
                        {
                            return Failure<ParsedString>(MakeErrorAt(
                                    context,
                                    ParseErrorCode::InvalidUnicodeEscape,
                                    "Unexpected low surrogate",
                                    escapeOffset,
                                    escapeOffset + 6));
                        }

                        write += NGIN::Text::Unicode::EncodeUtf8(
                                static_cast<NGIN::Text::Unicode::CodePoint>(codePoint),
                                write);
                        break;
                    }
                    default:
                        return Failure<ParsedString>(MakeErrorAt(
                                context,
                                ParseErrorCode::InvalidStringEscape,
                                "Invalid JSON string escape",
                                escapeOffset,
                                escapeOffset + 2));
                }
            }

            const UIntSize decodedLength = static_cast<UIntSize>(write - output);
            context.decodedBytes += decodedLength;
            return ParsedString {
                    .value = {output, decodedLength},
                    .span  = span,
            };
        }

        [[nodiscard]] bool MatchLiteral(ParseContext&    context,
                                        std::string_view literal) noexcept
        {
            if (context.cursor.Remaining().size() < literal.size())
                return false;
            if (context.cursor.Remaining().substr(0, literal.size()) != literal)
                return false;
            context.cursor.Advance(literal.size());
            return true;
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseValue(ParseContext& context, bool emit);

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseArray(ParseContext& context, bool emit)
        {
            const UIntSize start   = context.cursor.Offset();
            auto           counted = BeginValue(context, start);
            if (!counted)
                return counted;
            if (context.depth >= context.limits.maxDepth)
            {
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::LimitExceeded,
                                                 "JSON nesting depth limit exceeded",
                                                 start,
                                                 start + 1));
            }

            context.cursor.Advance();
            ++context.depth;
            auto started = Deliver(
                    context,
                    Event {
                            .kind = EventKind::StartArray,
                            .span = SourceSpan {context.sourceId, start, start + 1},
                    },
                    emit);
            if (!started)
                return started;

            auto trivia = SkipTrivia(context);
            if (!trivia)
                return trivia;

            if (context.cursor.Peek() != ']')
            {
                while (true)
                {
                    auto value = ParseValue(context, emit);
                    if (!value)
                        return value;

                    auto postValue = SkipTrivia(context);
                    if (!postValue)
                        return postValue;
                    if (context.cursor.Peek() == ']')
                        break;
                    if (context.cursor.Peek() != ',')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::UnexpectedCharacter,
                                "Expected ',' or ']' in JSON array"));
                    }

                    context.cursor.Advance();
                    auto postComma = SkipTrivia(context);
                    if (!postComma)
                        return postComma;
                    if (context.cursor.Peek() == ']')
                    {
                        if (context.options.trailingCommas == TrailingCommaPolicy::Reject)
                        {
                            return Failure<void>(MakeError(
                                    context,
                                    ParseErrorCode::InvalidToken,
                                    "Trailing comma in JSON array"));
                        }
                        break;
                    }
                }
            }

            const UIntSize endStart = context.cursor.Offset();
            if (context.cursor.Peek() != ']')
            {
                return Failure<void>(MakeError(
                        context, ParseErrorCode::UnexpectedEnd, "Unterminated JSON array"));
            }
            context.cursor.Advance();
            --context.depth;
            return Deliver(
                    context,
                    Event {
                            .kind = EventKind::EndArray,
                            .span = SourceSpan {context.sourceId, endStart, context.cursor.Offset()},
                    },
                    emit);
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseObject(ParseContext& context, bool emit)
        {
            const UIntSize start   = context.cursor.Offset();
            auto           counted = BeginValue(context, start);
            if (!counted)
                return counted;
            if (context.depth >= context.limits.maxDepth)
            {
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::LimitExceeded,
                                                 "JSON nesting depth limit exceeded",
                                                 start,
                                                 start + 1));
            }

            context.cursor.Advance();
            ++context.depth;
            auto started = Deliver(
                    context,
                    Event {
                            .kind = EventKind::StartObject,
                            .span = SourceSpan {context.sourceId, start, start + 1},
                    },
                    emit);
            if (!started)
                return started;

            std::vector<SeenKey> keys;
            auto                 trivia = SkipTrivia(context);
            if (!trivia)
                return trivia;

            if (context.cursor.Peek() != '}')
            {
                while (true)
                {
                    if (context.cursor.Peek() != '"')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::InvalidToken,
                                "Expected JSON object key"));
                    }
                    auto key = ParseString(context);
                    if (!key)
                        return Failure<void>(std::move(key.Error()));

                    const SeenKey* duplicate = nullptr;
                    if (context.options.duplicateKeys != DuplicateKeyPolicy::Preserve)
                    {
                        const auto found = std::find_if(
                                keys.begin(), keys.end(), [&](const SeenKey& candidate) {
                                    return candidate.value == key.Value().value;
                                });
                        if (found != keys.end())
                            duplicate = &*found;
                    }
                    if (duplicate &&
                        context.options.duplicateKeys == DuplicateKeyPolicy::Reject)
                    {
                        auto error = MakeErrorAt(
                                context,
                                ParseErrorCode::DuplicateName,
                                "Duplicate JSON object key",
                                key.Value().span.begin,
                                key.Value().span.end);
                        error.related = duplicate->span;
                        return Failure<void>(std::move(error));
                    }

                    const bool keepMember =
                            !duplicate ||
                            context.options.duplicateKeys == DuplicateKeyPolicy::Preserve;
                    if (keepMember)
                    {
                        if (context.memberCount >= context.limits.maxMembers)
                        {
                            return Failure<void>(MakeErrorAt(
                                    context,
                                    ParseErrorCode::LimitExceeded,
                                    "JSON object member limit exceeded",
                                    key.Value().span.begin,
                                    key.Value().span.end));
                        }
                        ++context.memberCount;
                        if (context.options.duplicateKeys != DuplicateKeyPolicy::Preserve)
                        {
                            try
                            {
                                keys.push_back(SeenKey {
                                        .value = key.Value().value,
                                        .span  = key.Value().span,
                                });
                            } catch (const std::bad_alloc&)
                            {
                                return Failure<void>(MakeErrorAt(
                                        context,
                                        ParseErrorCode::OutOfMemory,
                                        "JSON event key tracking allocation failed",
                                        key.Value().span.begin,
                                        key.Value().span.end));
                            }
                        }
                    }

                    auto postKey = SkipTrivia(context);
                    if (!postKey)
                        return postKey;
                    if (context.cursor.Peek() != ':')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::UnexpectedCharacter,
                                "Expected ':' after JSON object key"));
                    }
                    context.cursor.Advance();

                    auto keyResult = Deliver(
                            context,
                            Event {
                                    .kind = EventKind::Key,
                                    .span = key.Value().span,
                                    .text = key.Value().value,
                            },
                            emit && keepMember);
                    if (!keyResult)
                        return keyResult;

                    auto value = ParseValue(context, emit && keepMember);
                    if (!value)
                        return value;

                    auto postValue = SkipTrivia(context);
                    if (!postValue)
                        return postValue;
                    if (context.cursor.Peek() == '}')
                        break;
                    if (context.cursor.Peek() != ',')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::UnexpectedCharacter,
                                "Expected ',' or '}' in JSON object"));
                    }

                    context.cursor.Advance();
                    auto postComma = SkipTrivia(context);
                    if (!postComma)
                        return postComma;
                    if (context.cursor.Peek() == '}')
                    {
                        if (context.options.trailingCommas == TrailingCommaPolicy::Reject)
                        {
                            return Failure<void>(MakeError(
                                    context,
                                    ParseErrorCode::InvalidToken,
                                    "Trailing comma in JSON object"));
                        }
                        break;
                    }
                }
            }

            const UIntSize endStart = context.cursor.Offset();
            if (context.cursor.Peek() != '}')
            {
                return Failure<void>(MakeError(
                        context, ParseErrorCode::UnexpectedEnd, "Unterminated JSON object"));
            }
            context.cursor.Advance();
            --context.depth;
            return Deliver(
                    context,
                    Event {
                            .kind = EventKind::EndObject,
                            .span = SourceSpan {context.sourceId, endStart, context.cursor.Offset()},
                    },
                    emit);
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseNumber(ParseContext& context, bool emit)
        {
            const UIntSize startOffset = context.cursor.Offset();
            auto           counted     = BeginValue(context, startOffset);
            if (!counted)
                return counted;

            const char* start   = context.cursor.CurrentPtr();
            const char* current = start;
            const char* end     = context.cursor.EndPtr();

            if (*current == '-')
                ++current;
            if (current >= end)
            {
                return Failure<void>(
                        MakeError(context, ParseErrorCode::UnexpectedEnd, "Truncated JSON number"));
            }

            if (*current == '0')
            {
                ++current;
                if (current < end && IsDigit(*current))
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidNumber,
                            "Leading zero in JSON number",
                            startOffset,
                            startOffset + static_cast<UIntSize>(current - start) + 1));
                }
            }
            else
            {
                if (!IsDigit(*current))
                {
                    return Failure<void>(
                            MakeError(context, ParseErrorCode::InvalidNumber, "Invalid JSON number"));
                }
                while (current < end && IsDigit(*current))
                    ++current;
            }

            bool isFloating = false;
            if (current < end && *current == '.')
            {
                isFloating = true;
                ++current;
                if (current >= end || !IsDigit(*current))
                {
                    return Failure<void>(
                            MakeError(context, ParseErrorCode::InvalidNumber, "Invalid JSON fraction"));
                }
                while (current < end && IsDigit(*current))
                    ++current;
            }

            if (current < end && (*current == 'e' || *current == 'E'))
            {
                isFloating = true;
                ++current;
                if (current < end && (*current == '+' || *current == '-'))
                    ++current;
                if (current >= end || !IsDigit(*current))
                {
                    return Failure<void>(
                            MakeError(context, ParseErrorCode::InvalidNumber, "Invalid JSON exponent"));
                }
                while (current < end && IsDigit(*current))
                    ++current;
            }

            context.cursor.Advance(static_cast<UIntSize>(current - start));
            Event event {
                    .span = SourceSpan {
                            context.sourceId,
                            startOffset,
                            context.cursor.Offset(),
                    },
            };

            if (isFloating)
            {
                F64        value = 0.0;
                const auto result =
                        std::from_chars(start, current, value, std::chars_format::general);
                if (result.ec != std::errc {} ||
                    result.ptr != current ||
                    !std::isfinite(value))
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidNumber,
                            "JSON floating-point value is out of range",
                            startOffset,
                            context.cursor.Offset()));
                }
                event.kind        = EventKind::Double;
                event.doubleValue = value;
                return Deliver(context, event, emit);
            }

            if (*start == '-')
            {
                Int64      value  = 0;
                const auto result = std::from_chars(start, current, value);
                if (result.ec != std::errc {} || result.ptr != current)
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidNumber,
                            "JSON signed integer is out of range",
                            startOffset,
                            context.cursor.Offset()));
                }
                event.kind     = EventKind::Int64;
                event.intValue = value;
                return Deliver(context, event, emit);
            }

            UInt64     value  = 0;
            const auto result = std::from_chars(start, current, value);
            if (result.ec != std::errc {} || result.ptr != current)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::InvalidNumber,
                        "JSON unsigned integer is out of range",
                        startOffset,
                        context.cursor.Offset()));
            }

            if (value <= static_cast<UInt64>((std::numeric_limits<Int64>::max)()))
            {
                event.kind     = EventKind::Int64;
                event.intValue = static_cast<Int64>(value);
            }
            else
            {
                event.kind      = EventKind::UInt64;
                event.uintValue = value;
            }
            return Deliver(context, event, emit);
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseValue(ParseContext& context, bool emit)
        {
            auto trivia = SkipTrivia(context);
            if (!trivia)
                return trivia;

            const UIntSize start = context.cursor.Offset();
            const char     token = context.cursor.Peek();
            if (token == '{')
                return ParseObject(context, emit);
            if (token == '[')
                return ParseArray(context, emit);
            if (token == '"')
            {
                auto counted = BeginValue(context, start);
                if (!counted)
                    return counted;
                auto string = ParseString(context);
                if (!string)
                    return Failure<void>(std::move(string.Error()));
                return Deliver(
                        context,
                        Event {
                                .kind = EventKind::String,
                                .span = string.Value().span,
                                .text = string.Value().value,
                        },
                        emit);
            }
            if (token == '-' || IsDigit(token))
                return ParseNumber(context, emit);

            auto counted = BeginValue(context, start);
            if (!counted)
                return counted;

            Event event {
                    .span = SourceSpan {
                            .source = context.sourceId,
                            .begin  = start,
                    },
            };
            if (token == 'n' && MatchLiteral(context, "null"))
                event.kind = EventKind::Null;
            else if (token == 't' && MatchLiteral(context, "true"))
            {
                event.kind      = EventKind::Bool;
                event.boolValue = true;
            }
            else if (token == 'f' && MatchLiteral(context, "false"))
            {
                event.kind      = EventKind::Bool;
                event.boolValue = false;
            }
            else
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        context.cursor.IsEof()
                                ? ParseErrorCode::UnexpectedEnd
                                : ParseErrorCode::UnexpectedCharacter,
                        context.cursor.IsEof()
                                ? "Expected JSON value"
                                : "Unexpected JSON token",
                        start,
                        (std::min) (start + 1, context.source.size())));
            }
            event.span.end = context.cursor.Offset();
            return Deliver(context, event, emit);
        }
    }// namespace

    NGIN::Utilities::Expected<void, ParseDiagnostic>
    ParseEventsContiguous(BorrowedTextView    input,
                          void*               handlerContext,
                          EventCallback       callback,
                          ParseScratch&       scratch,
                          const ParseOptions& options,
                          const ParseLimits&  limits)
    {
        const auto   source = input.View();
        ParseContext context {
                .cursor         = InputCursor {source},
                .source         = source,
                .sourceId       = input.Source(),
                .options        = options,
                .limits         = limits,
                .scratch        = &scratch,
                .handlerContext = handlerContext,
                .callback       = callback,
        };

        if (source.size() > limits.maxInputBytes)
        {
            return Failure<void>(MakeErrorAt(context,
                                             ParseErrorCode::LimitExceeded,
                                             "JSON input byte limit exceeded",
                                             0,
                                             source.size()));
        }
        if (options.utf8 == Utf8Policy::Validate &&
            !NGIN::Text::Unicode::IsValidUtf8(source))
        {
            return Failure<void>(MakeErrorAt(context,
                                             ParseErrorCode::InvalidEncoding,
                                             "JSON input is not valid UTF-8",
                                             0,
                                             source.size()));
        }

        scratch.Reset();
        try
        {
            // A full-input reserve keeps all decoded string views stable until
            // ParseContiguous returns, matching the documented event lifetime.
            scratch.Reserve(source.size());
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate JSON event parser scratch storage";
            return Failure<void>(std::move(error));
        }

        auto parsed = ParseValue(context, true);
        if (!parsed)
            return parsed;

        auto trivia = SkipTrivia(context);
        if (!trivia)
            return trivia;
        if (!context.cursor.IsEof())
        {
            return Failure<void>(MakeError(
                    context,
                    ParseErrorCode::TrailingCharacters,
                    "Trailing characters after JSON value"));
        }
        return {};
    }
}// namespace NGIN::Serialization::JSON::detail
