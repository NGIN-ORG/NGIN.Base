#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include "JsonDocumentInternal.hpp"

#include <NGIN/SIMD/Scan.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace NGIN::Serialization::JSON
{
    namespace
    {
        struct ParsedString
        {
            detail::StringRef value {};
            SourceSpan        span {};
        };

        struct ParseContext
        {
            InputCursor            cursor;
            ParseOptions           options;
            detail::DocumentState* state {nullptr};
            char*                  mutableBase {nullptr};
            ParseScratch*          scratch {nullptr};
            UIntSize               depth {0};
            UIntSize               decodedBytes {0};
        };

        [[nodiscard]] SourceLocation Locate(std::string_view source, SourceId sourceId, UIntSize offset) noexcept
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

        [[nodiscard]] ParseDiagnostic MakeErrorAt(const ParseContext& ctx,
                                                  ParseErrorCode      code,
                                                  const char*         message,
                                                  UIntSize            begin,
                                                  UIntSize            end)
        {
            const auto      location = Locate(ctx.state->source, ctx.state->sourceId, begin);
            ParseDiagnostic error;
            error.code     = code;
            error.location = ParseLocation {
                    .offset = location.offset,
                    .line   = location.line,
                    .column = location.column,
            };
            error.span = SourceSpan {
                    .source = ctx.state->sourceId,
                    .begin  = begin,
                    .end    = (std::max) (begin, end),
            };
            error.message = message;
            return error;
        }

        [[nodiscard]] ParseDiagnostic MakeError(const ParseContext& ctx,
                                                ParseErrorCode      code,
                                                const char*         message)
        {
            return MakeErrorAt(ctx, code, message, ctx.cursor.Offset(), ctx.cursor.Offset());
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
        SkipComment(ParseContext& ctx)
        {
            const UIntSize start = ctx.cursor.Offset();
            if (ctx.cursor.Peek() != '/')
                return {};

            if (ctx.cursor.Peek(1) == '/')
            {
                ctx.cursor.Advance(2);
                while (!ctx.cursor.IsEof())
                {
                    const char value = ctx.cursor.Peek();
                    if (value == '\r' || value == '\n')
                        break;
                    ctx.cursor.Advance();
                }
                return {};
            }

            if (ctx.cursor.Peek(1) == '*')
            {
                ctx.cursor.Advance(2);
                while (!ctx.cursor.IsEof())
                {
                    if (ctx.cursor.Peek() == '*' && ctx.cursor.Peek(1) == '/')
                    {
                        ctx.cursor.Advance(2);
                        return {};
                    }
                    ctx.cursor.Advance();
                }
                return Failure<void>(MakeErrorAt(ctx,
                                                 ParseErrorCode::UnexpectedEnd,
                                                 "Unterminated block comment",
                                                 start,
                                                 ctx.cursor.Offset()));
            }

            return Failure<void>(MakeErrorAt(ctx,
                                             ParseErrorCode::InvalidToken,
                                             "Invalid comment token",
                                             start,
                                             ctx.cursor.Offset() + 1));
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        SkipTrivia(ParseContext& ctx)
        {
            while (true)
            {
                ctx.cursor.SkipWhitespace();
                if (ctx.cursor.Peek() != '/')
                    return {};
                if (ctx.options.comments == CommentPolicy::Reject)
                {
                    return Failure<void>(MakeErrorAt(ctx,
                                                     ParseErrorCode::InvalidToken,
                                                     "JSON comments are disabled",
                                                     ctx.cursor.Offset(),
                                                     ctx.cursor.Offset() + 1));
                }
                auto comment = SkipComment(ctx);
                if (!comment)
                    return comment;
            }
        }

        [[nodiscard]] NGIN::Utilities::Expected<ParsedString, ParseDiagnostic>
        ParseString(ParseContext& ctx)
        {
            const UIntSize tokenStart = ctx.cursor.Offset();
            if (ctx.cursor.Peek() != '"')
                return Failure<ParsedString>(MakeError(ctx, ParseErrorCode::InvalidToken, "Expected JSON string"));

            ctx.cursor.Advance();
            const char* start      = ctx.cursor.CurrentPtr();
            const char* scan       = start;
            bool        hasEscapes = false;

            while (scan && scan < ctx.cursor.EndPtr())
            {
                const std::size_t remaining = static_cast<std::size_t>(ctx.cursor.EndPtr() - scan);
                const std::size_t offset    = NGIN::SIMD::FindAnyByte(scan, remaining, '"', '\\');
                const char*       next      = scan + offset;

                for (const char* current = scan; current < next; ++current)
                {
                    if (static_cast<unsigned char>(*current) < 0x20U)
                    {
                        const UIntSize errorOffset = tokenStart + 1 + static_cast<UIntSize>(current - start);
                        return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                 ParseErrorCode::InvalidToken,
                                                                 "Control character in JSON string",
                                                                 errorOffset,
                                                                 errorOffset + 1));
                    }
                }

                if (next >= ctx.cursor.EndPtr())
                {
                    return Failure<ParsedString>(MakeErrorAt(ctx,
                                                             ParseErrorCode::UnexpectedEnd,
                                                             "Unterminated JSON string",
                                                             tokenStart,
                                                             ctx.state->source.size()));
                }

                if (*next == '"')
                {
                    scan = next;
                    break;
                }

                hasEscapes = true;
                scan       = next + 1;
                if (scan >= ctx.cursor.EndPtr())
                {
                    return Failure<ParsedString>(MakeErrorAt(ctx,
                                                             ParseErrorCode::UnexpectedEnd,
                                                             "Unterminated JSON escape",
                                                             static_cast<UIntSize>(next - ctx.state->source.data()),
                                                             ctx.state->source.size()));
                }
                ++scan;
            }

            if (!scan || scan >= ctx.cursor.EndPtr())
            {
                return Failure<ParsedString>(MakeErrorAt(ctx,
                                                         ParseErrorCode::UnexpectedEnd,
                                                         "Unterminated JSON string",
                                                         tokenStart,
                                                         ctx.state->source.size()));
            }

            const UIntSize rawLength = static_cast<UIntSize>(scan - start);
            ctx.cursor.Advance(rawLength + 1);
            const SourceSpan span {
                    .source = ctx.state->sourceId,
                    .begin  = tokenStart,
                    .end    = ctx.cursor.Offset(),
            };

            if (!hasEscapes)
            {
                return ParsedString {
                        .value = detail::StringRef {start, rawLength},
                        .span  = span,
                };
            }

            if (ctx.decodedBytes > ctx.state->limits.maxDecodedStringBytes ||
                rawLength > ctx.state->limits.maxDecodedStringBytes - ctx.decodedBytes)
            {
                return Failure<ParsedString>(MakeErrorAt(ctx,
                                                         ParseErrorCode::LimitExceeded,
                                                         "Decoded JSON string limit exceeded",
                                                         tokenStart,
                                                         span.end));
            }

            char* output = nullptr;
            if (ctx.mutableBase)
                output = ctx.mutableBase + tokenStart + 1;
            else if (ctx.scratch)
                output = ctx.scratch->TryAllocate(rawLength);
            else
                output = static_cast<char*>(ctx.state->arena.Allocate(rawLength, alignof(char)));

            if (!output)
            {
                return Failure<ParsedString>(MakeErrorAt(ctx,
                                                         ParseErrorCode::OutOfMemory,
                                                         "JSON string allocation failed",
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
                    return Failure<ParsedString>(MakeErrorAt(ctx,
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
                            return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                     ParseErrorCode::InvalidUnicodeEscape,
                                                                     "Truncated JSON Unicode escape",
                                                                     escapeOffset,
                                                                     span.end));
                        }

                        UInt32 codePoint = 0;
                        if (!DecodeHex4(read, codePoint))
                        {
                            return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                     ParseErrorCode::InvalidUnicodeEscape,
                                                                     "Invalid JSON Unicode escape",
                                                                     escapeOffset,
                                                                     escapeOffset + 6));
                        }
                        read += 4;

                        if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
                        {
                            if (static_cast<UIntSize>(end - read) < 6 || read[0] != '\\' || read[1] != 'u')
                            {
                                return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                         ParseErrorCode::InvalidUnicodeEscape,
                                                                         "Missing low surrogate",
                                                                         escapeOffset,
                                                                         escapeOffset + 6));
                            }

                            UInt32 low = 0;
                            if (!DecodeHex4(read + 2, low) || low < 0xDC00U || low > 0xDFFFU)
                            {
                                return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                         ParseErrorCode::InvalidUnicodeEscape,
                                                                         "Invalid low surrogate",
                                                                         escapeOffset + 6,
                                                                         escapeOffset + 12));
                            }
                            read += 6;
                            codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
                        }
                        else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
                        {
                            return Failure<ParsedString>(MakeErrorAt(ctx,
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
                        return Failure<ParsedString>(MakeErrorAt(ctx,
                                                                 ParseErrorCode::InvalidStringEscape,
                                                                 "Invalid JSON string escape",
                                                                 escapeOffset,
                                                                 escapeOffset + 2));
                }
            }

            const UIntSize decodedLength = static_cast<UIntSize>(write - output);
            ctx.decodedBytes += decodedLength;
            return ParsedString {
                    .value = detail::StringRef {output, decodedLength},
                    .span  = span,
            };
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        AddNode(ParseContext& ctx, detail::NodeRecord node)
        {
            if (ctx.state->nodes.size() >= ctx.state->limits.maxNodes ||
                ctx.state->nodes.size() >= static_cast<UIntSize>((std::numeric_limits<UInt32>::max)()))
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::LimitExceeded,
                                                   "JSON node limit exceeded",
                                                   node.span.begin,
                                                   node.span.end));
            }

            try
            {
                const NodeId id {static_cast<UInt32>(ctx.state->nodes.size())};
                ctx.state->nodes.push_back(node);
                if (!ctx.state->WithinMemoryLimit())
                {
                    ctx.state->nodes.pop_back();
                    return Failure<NodeId>(MakeErrorAt(ctx,
                                                       ParseErrorCode::LimitExceeded,
                                                       "JSON memory limit exceeded",
                                                       node.span.begin,
                                                       node.span.end));
                }
                return id;
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::OutOfMemory,
                                                   "JSON node allocation failed",
                                                   node.span.begin,
                                                   node.span.end));
            }
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseValue(ParseContext& ctx);

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseArray(ParseContext& ctx)
        {
            const UIntSize start = ctx.cursor.Offset();
            if (ctx.depth >= ctx.state->limits.maxDepth)
                return Failure<NodeId>(MakeError(ctx, ParseErrorCode::DepthExceeded, "JSON depth limit exceeded"));

            ++ctx.depth;
            ctx.cursor.Advance();
            auto trivia = SkipTrivia(ctx);
            if (!trivia)
                return Failure<NodeId>(std::move(trivia.Error()));

            std::vector<NodeId> values;
            if (ctx.cursor.Peek() == ']')
            {
                ctx.cursor.Advance();
            }
            else
            {
                while (true)
                {
                    auto value = ParseValue(ctx);
                    if (!value)
                        return value;
                    try
                    {
                        values.push_back(value.Value());
                    } catch (const std::bad_alloc&)
                    {
                        return Failure<NodeId>(MakeError(ctx, ParseErrorCode::OutOfMemory, "JSON array allocation failed"));
                    }

                    auto postValue = SkipTrivia(ctx);
                    if (!postValue)
                        return Failure<NodeId>(std::move(postValue.Error()));

                    if (ctx.cursor.Peek() == ']')
                    {
                        ctx.cursor.Advance();
                        break;
                    }

                    if (ctx.cursor.Peek() != ',')
                    {
                        return Failure<NodeId>(MakeError(ctx,
                                                         ParseErrorCode::UnexpectedCharacter,
                                                         "Expected ',' or ']' in JSON array"));
                    }

                    ctx.cursor.Advance();
                    auto postComma = SkipTrivia(ctx);
                    if (!postComma)
                        return Failure<NodeId>(std::move(postComma.Error()));

                    if (ctx.cursor.Peek() == ']')
                    {
                        if (ctx.options.trailingCommas == TrailingCommaPolicy::Reject)
                        {
                            return Failure<NodeId>(MakeError(ctx,
                                                             ParseErrorCode::InvalidToken,
                                                             "Trailing comma in JSON array"));
                        }
                        ctx.cursor.Advance();
                        break;
                    }
                }
            }

            --ctx.depth;
            if (ctx.state->elements.size() > ctx.state->limits.maxMembers ||
                values.size() > ctx.state->limits.maxMembers - ctx.state->elements.size())
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::LimitExceeded,
                                                   "JSON array element limit exceeded",
                                                   start,
                                                   ctx.cursor.Offset()));
            }

            const UIntSize begin = ctx.state->elements.size();
            try
            {
                ctx.state->elements.insert(ctx.state->elements.end(), values.begin(), values.end());
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::OutOfMemory,
                                                   "JSON array storage allocation failed",
                                                   start,
                                                   ctx.cursor.Offset()));
            }

            detail::NodeRecord node;
            node.kind               = ValueKind::Array;
            node.span               = SourceSpan {ctx.state->sourceId, start, ctx.cursor.Offset()};
            node.payload.rangeValue = detail::NodeRange {begin, values.size()};
            return AddNode(ctx, node);
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseObject(ParseContext& ctx)
        {
            const UIntSize start = ctx.cursor.Offset();
            if (ctx.depth >= ctx.state->limits.maxDepth)
                return Failure<NodeId>(MakeError(ctx, ParseErrorCode::DepthExceeded, "JSON depth limit exceeded"));

            ++ctx.depth;
            ctx.cursor.Advance();
            auto trivia = SkipTrivia(ctx);
            if (!trivia)
                return Failure<NodeId>(std::move(trivia.Error()));

            std::vector<detail::MemberRecord> members;
            if (ctx.cursor.Peek() == '}')
            {
                ctx.cursor.Advance();
            }
            else
            {
                while (true)
                {
                    auto key = ParseString(ctx);
                    if (!key)
                        return Failure<NodeId>(std::move(key.Error()));

                    auto postKey = SkipTrivia(ctx);
                    if (!postKey)
                        return Failure<NodeId>(std::move(postKey.Error()));
                    if (ctx.cursor.Peek() != ':')
                        return Failure<NodeId>(MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ':' after JSON key"));
                    ctx.cursor.Advance();

                    auto postColon = SkipTrivia(ctx);
                    if (!postColon)
                        return Failure<NodeId>(std::move(postColon.Error()));

                    auto value = ParseValue(ctx);
                    if (!value)
                        return value;

                    detail::MemberRecord member {
                            .key   = key.Value().value,
                            .value = value.Value(),
                            .span  = SourceSpan {
                                    .source = ctx.state->sourceId,
                                    .begin  = key.Value().span.begin,
                                    .end    = ctx.state->Node(value.Value())->span.end,
                            },
                    };

                    auto duplicate = members.end();
                    for (auto iterator = members.begin(); iterator != members.end(); ++iterator)
                    {
                        if (iterator->key.View() == member.key.View())
                        {
                            duplicate = iterator;
                            break;
                        }
                    }

                    if (duplicate != members.end())
                    {
                        switch (ctx.options.duplicateKeys)
                        {
                            case DuplicateKeyPolicy::Reject: {
                                auto error    = MakeErrorAt(ctx,
                                                            ParseErrorCode::DuplicateName,
                                                            "Duplicate JSON object key",
                                                            key.Value().span.begin,
                                                            key.Value().span.end);
                                error.related = duplicate->span;
                                return Failure<NodeId>(std::move(error));
                            }
                            case DuplicateKeyPolicy::KeepFirst:
                                break;
                            case DuplicateKeyPolicy::KeepLast:
                                *duplicate = member;
                                break;
                            case DuplicateKeyPolicy::Preserve:
                                try
                                {
                                    members.push_back(member);
                                } catch (const std::bad_alloc&)
                                {
                                    return Failure<NodeId>(MakeError(ctx,
                                                                     ParseErrorCode::OutOfMemory,
                                                                     "JSON object allocation failed"));
                                }
                                break;
                        }
                    }
                    else
                    {
                        try
                        {
                            members.push_back(member);
                        } catch (const std::bad_alloc&)
                        {
                            return Failure<NodeId>(MakeError(ctx,
                                                             ParseErrorCode::OutOfMemory,
                                                             "JSON object allocation failed"));
                        }
                    }

                    auto postValue = SkipTrivia(ctx);
                    if (!postValue)
                        return Failure<NodeId>(std::move(postValue.Error()));

                    if (ctx.cursor.Peek() == '}')
                    {
                        ctx.cursor.Advance();
                        break;
                    }

                    if (ctx.cursor.Peek() != ',')
                    {
                        return Failure<NodeId>(MakeError(ctx,
                                                         ParseErrorCode::UnexpectedCharacter,
                                                         "Expected ',' or '}' in JSON object"));
                    }

                    ctx.cursor.Advance();
                    auto postComma = SkipTrivia(ctx);
                    if (!postComma)
                        return Failure<NodeId>(std::move(postComma.Error()));

                    if (ctx.cursor.Peek() == '}')
                    {
                        if (ctx.options.trailingCommas == TrailingCommaPolicy::Reject)
                        {
                            return Failure<NodeId>(MakeError(ctx,
                                                             ParseErrorCode::InvalidToken,
                                                             "Trailing comma in JSON object"));
                        }
                        ctx.cursor.Advance();
                        break;
                    }
                }
            }

            --ctx.depth;
            if (ctx.state->members.size() > ctx.state->limits.maxMembers ||
                members.size() > ctx.state->limits.maxMembers - ctx.state->members.size())
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::LimitExceeded,
                                                   "JSON object member limit exceeded",
                                                   start,
                                                   ctx.cursor.Offset()));
            }

            const UIntSize begin = ctx.state->members.size();
            try
            {
                ctx.state->members.insert(ctx.state->members.end(), members.begin(), members.end());
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::OutOfMemory,
                                                   "JSON object storage allocation failed",
                                                   start,
                                                   ctx.cursor.Offset()));
            }

            detail::NodeRecord node;
            node.kind               = ValueKind::Object;
            node.span               = SourceSpan {ctx.state->sourceId, start, ctx.cursor.Offset()};
            node.payload.rangeValue = detail::NodeRange {begin, members.size()};
            return AddNode(ctx, node);
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseNumber(ParseContext& ctx)
        {
            const UIntSize startOffset = ctx.cursor.Offset();
            const char*    start       = ctx.cursor.CurrentPtr();
            const char*    current     = start;
            const char*    end         = ctx.cursor.EndPtr();

            if (*current == '-')
                ++current;
            if (current >= end)
                return Failure<NodeId>(MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Truncated JSON number"));

            if (*current == '0')
            {
                ++current;
                if (current < end && IsDigit(*current))
                {
                    return Failure<NodeId>(MakeErrorAt(ctx,
                                                       ParseErrorCode::InvalidNumber,
                                                       "Leading zero in JSON number",
                                                       startOffset,
                                                       startOffset + static_cast<UIntSize>(current - start) + 1));
                }
            }
            else
            {
                if (!IsDigit(*current))
                    return Failure<NodeId>(MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid JSON number"));
                while (current < end && IsDigit(*current))
                    ++current;
            }

            bool isFloating = false;
            if (current < end && *current == '.')
            {
                isFloating = true;
                ++current;
                if (current >= end || !IsDigit(*current))
                    return Failure<NodeId>(MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid JSON fraction"));
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
                    return Failure<NodeId>(MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid JSON exponent"));
                while (current < end && IsDigit(*current))
                    ++current;
            }

            const UIntSize length = static_cast<UIntSize>(current - start);
            ctx.cursor.Advance(length);

            detail::NodeRecord node;
            node.span = SourceSpan {ctx.state->sourceId, startOffset, ctx.cursor.Offset()};

            if (isFloating)
            {
                F64        value  = 0.0;
                const auto result = std::from_chars(start, current, value, std::chars_format::general);
                if (result.ec != std::errc {} || result.ptr != current || !std::isfinite(value))
                {
                    return Failure<NodeId>(MakeErrorAt(ctx,
                                                       ParseErrorCode::InvalidNumber,
                                                       "JSON floating-point value is out of range",
                                                       startOffset,
                                                       ctx.cursor.Offset()));
                }
                node.kind                = ValueKind::Double;
                node.payload.doubleValue = value;
                return AddNode(ctx, node);
            }

            if (*start == '-')
            {
                Int64      value  = 0;
                const auto result = std::from_chars(start, current, value);
                if (result.ec != std::errc {} || result.ptr != current)
                {
                    return Failure<NodeId>(MakeErrorAt(ctx,
                                                       ParseErrorCode::InvalidNumber,
                                                       "JSON signed integer is out of range",
                                                       startOffset,
                                                       ctx.cursor.Offset()));
                }
                node.kind                = ValueKind::Int64;
                node.payload.signedValue = value;
                return AddNode(ctx, node);
            }

            UInt64     value  = 0;
            const auto result = std::from_chars(start, current, value);
            if (result.ec != std::errc {} || result.ptr != current)
            {
                return Failure<NodeId>(MakeErrorAt(ctx,
                                                   ParseErrorCode::InvalidNumber,
                                                   "JSON unsigned integer is out of range",
                                                   startOffset,
                                                   ctx.cursor.Offset()));
            }

            if (value <= static_cast<UInt64>((std::numeric_limits<Int64>::max)()))
            {
                node.kind                = ValueKind::Int64;
                node.payload.signedValue = static_cast<Int64>(value);
            }
            else
            {
                node.kind                  = ValueKind::UInt64;
                node.payload.unsignedValue = value;
            }
            return AddNode(ctx, node);
        }

        [[nodiscard]] bool MatchLiteral(ParseContext& ctx, std::string_view literal) noexcept
        {
            if (ctx.cursor.Remaining().size() < literal.size())
                return false;
            if (ctx.cursor.Remaining().substr(0, literal.size()) != literal)
                return false;
            ctx.cursor.Advance(literal.size());
            return true;
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseValue(ParseContext& ctx)
        {
            auto trivia = SkipTrivia(ctx);
            if (!trivia)
                return Failure<NodeId>(std::move(trivia.Error()));

            const UIntSize start = ctx.cursor.Offset();
            const char     token = ctx.cursor.Peek();

            if (token == '{')
                return ParseObject(ctx);
            if (token == '[')
                return ParseArray(ctx);
            if (token == '"')
            {
                auto string = ParseString(ctx);
                if (!string)
                    return Failure<NodeId>(std::move(string.Error()));
                detail::NodeRecord node;
                node.kind                = ValueKind::String;
                node.span                = string.Value().span;
                node.payload.stringValue = string.Value().value;
                return AddNode(ctx, node);
            }
            if (token == '-' || IsDigit(token))
                return ParseNumber(ctx);

            detail::NodeRecord node;
            node.span.source = ctx.state->sourceId;
            node.span.begin  = start;
            if (token == 'n' && MatchLiteral(ctx, "null"))
            {
                node.kind     = ValueKind::Null;
                node.span.end = ctx.cursor.Offset();
                return AddNode(ctx, node);
            }
            if (token == 't' && MatchLiteral(ctx, "true"))
            {
                node.kind              = ValueKind::Bool;
                node.payload.boolValue = true;
                node.span.end          = ctx.cursor.Offset();
                return AddNode(ctx, node);
            }
            if (token == 'f' && MatchLiteral(ctx, "false"))
            {
                node.kind              = ValueKind::Bool;
                node.payload.boolValue = false;
                node.span.end          = ctx.cursor.Offset();
                return AddNode(ctx, node);
            }

            return Failure<NodeId>(MakeErrorAt(ctx,
                                               ctx.cursor.IsEof()
                                                       ? ParseErrorCode::UnexpectedEnd
                                                       : ParseErrorCode::UnexpectedCharacter,
                                               ctx.cursor.IsEof()
                                                       ? "Expected JSON value"
                                                       : "Unexpected JSON token",
                                               start,
                                               (std::min) (start + 1, ctx.state->source.size())));
        }

        template<class DocumentType>
        [[nodiscard]] NGIN::Utilities::Expected<DocumentType, ParseDiagnostic>
        ParseState(std::unique_ptr<detail::DocumentState> state,
                   const ParseOptions&                    options,
                   char*                                  mutableBase = nullptr,
                   ParseScratch*                          scratch     = nullptr)
        {
            if (state->source.size() > state->limits.maxInputBytes)
            {
                ParseContext context {
                        .cursor  = InputCursor(state->source),
                        .options = options,
                        .state   = state.get(),
                };
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::LimitExceeded,
                                                         "JSON input byte limit exceeded",
                                                         0,
                                                         state->source.size()));
            }

            if (options.utf8 == Utf8Policy::Validate &&
                !NGIN::Text::Unicode::IsValidUtf8(state->source))
            {
                ParseContext context {
                        .cursor  = InputCursor(state->source),
                        .options = options,
                        .state   = state.get(),
                };
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::InvalidEncoding,
                                                         "JSON input is not valid UTF-8",
                                                         0,
                                                         state->source.size()));
            }

            ParseContext context {
                    .cursor      = InputCursor(state->source),
                    .options     = options,
                    .state       = state.get(),
                    .mutableBase = mutableBase,
                    .scratch     = scratch,
            };

            auto root = ParseValue(context);
            if (!root)
                return Failure<DocumentType>(std::move(root.Error()));
            state->root = root.Value();

            auto trivia = SkipTrivia(context);
            if (!trivia)
                return Failure<DocumentType>(std::move(trivia.Error()));
            if (!context.cursor.IsEof())
            {
                return Failure<DocumentType>(MakeError(context,
                                                       ParseErrorCode::TrailingCharacters,
                                                       "Trailing characters after JSON value"));
            }

            state->FinalizeViews();
            if (!state->WithinMemoryLimit())
            {
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::LimitExceeded,
                                                         "JSON total memory limit exceeded",
                                                         0,
                                                         state->source.size()));
            }
            if constexpr (std::same_as<DocumentType, Document>)
                return detail::DocumentAccess::MakeDocument(std::move(state));
            else
                return detail::DocumentAccess::MakeBorrowedDocument(std::move(state));
        }
    }// namespace

    NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parser::Parse(OwnedTextBuffer       input,
                  const ParseOptions&   options,
                  const ParseLimits&    limits,
                  const ParseResources& resources)
    {
        try
        {
            auto state = std::make_unique<detail::DocumentState>(
                    std::move(input), limits, resources);
            return ParseState<Document>(std::move(state), options);
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate JSON document";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }
    }

    NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parser::ParseInSitu(MutableTextBuffer     input,
                        const ParseOptions&   options,
                        const ParseLimits&    limits,
                        const ParseResources& resources)
    {
        try
        {
            auto owned = std::move(input).TakeOwned();
            auto state = std::make_unique<detail::DocumentState>(
                    std::move(owned), limits, resources);
            char* mutableBase = state->ownedSource->Text().Data();
            return ParseState<Document>(std::move(state), options, mutableBase);
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate in-situ JSON document";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }
    }

    NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
    Parser::ParseBorrowed(BorrowedTextView      input,
                          ParseScratch&         scratch,
                          const ParseOptions&   options,
                          const ParseLimits&    limits,
                          const ParseResources& resources)
    {
        scratch.Reset();
        try
        {
            scratch.Reserve(input.View().size());
            auto state = std::make_unique<detail::DocumentState>(input, limits, resources);
            return ParseState<BorrowedDocument>(std::move(state), options, nullptr, &scratch);
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate borrowed JSON document";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }
    }
}// namespace NGIN::Serialization::JSON
