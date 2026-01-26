#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <NGIN/Containers/Vector.hpp>

#include <cctype>
#include <charconv>
#include <cstring>
#include <new>

namespace NGIN::Serialization
{
    namespace
    {
        struct JsonParseContext
        {
            InputCursor      cursor;
            JsonParseOptions options;
            JsonArena*       arena {nullptr};
            char*            mutableBase {nullptr};
            UIntSize         depth {0};
        };

        [[nodiscard]] ParseError MakeError(const JsonParseContext& ctx, ParseErrorCode code, const char* message)
        {
            ParseError err;
            err.code     = code;
            err.location = ctx.cursor.Location();
            err.message  = message;
            return err;
        }

        [[nodiscard]] bool IsDigit(char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        [[nodiscard]] bool IsHexDigit(char c) noexcept
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        [[nodiscard]] UInt32 HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return static_cast<UInt32>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<UInt32>(c - 'a' + 10);
            return static_cast<UInt32>(c - 'A' + 10);
        }

        bool DecodeHex4(const char* digits, UInt32& out) noexcept
        {
            out = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char c = digits[i];
                if (!IsHexDigit(c))
                    return false;
                out = static_cast<UInt32>((out << 4) | HexValue(c));
            }
            return true;
        }

        [[nodiscard]] UIntSize EncodeUtf8(UInt32 codepoint, char* out) noexcept
        {
            if (codepoint <= 0x7F)
            {
                out[0] = static_cast<char>(codepoint);
                return 1;
            }
            if (codepoint <= 0x7FF)
            {
                out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
                out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
                return 2;
            }
            if (codepoint <= 0xFFFF)
            {
                out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
                out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
                return 3;
            }
            out[0] = static_cast<char>(0xF0 | (codepoint >> 18));
            out[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
            return 4;
        }

        NGIN::Utilities::Expected<void, ParseError> SkipComment(JsonParseContext& ctx)
        {
            if (ctx.cursor.Peek() != '/')
                return {};
            const char next = ctx.cursor.Peek(1);
            if (next == '/')
            {
                ctx.cursor.Advance(2);
                while (!ctx.cursor.IsEof())
                {
                    const char c = ctx.cursor.Peek();
                    if (c == '\n' || c == '\r')
                        break;
                    ctx.cursor.Advance();
                }
                return {};
            }
            if (next == '*')
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
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated comment")));
            }
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid comment token")));
        }

        NGIN::Utilities::Expected<void, ParseError> SkipWhitespaceAndComments(JsonParseContext& ctx)
        {
            while (true)
            {
                ctx.cursor.SkipWhitespace();
                if (!ctx.options.allowComments)
                    return {};
                if (ctx.cursor.Peek() != '/')
                    return {};
                auto commentResult = SkipComment(ctx);
                if (!commentResult.HasValue())
                    return commentResult;
            }
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> ParseString(JsonParseContext& ctx)
        {
            if (ctx.cursor.Peek() != '"')
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected string")));
            }

            ctx.cursor.Advance();
            const char* start      = ctx.cursor.CurrentPtr();
            const char* scan       = start;
            bool        hasEscapes = false;

            while (scan < ctx.cursor.EndPtr())
            {
                const char c = *scan;
                if (c == '"')
                    break;
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Control character in string")));
                }
                if (c == '\\')
                {
                    hasEscapes = true;
                    ++scan;
                    if (scan >= ctx.cursor.EndPtr())
                    {
                        return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated escape")));
                    }
                    if (*scan == 'u')
                    {
                        if ((scan + 4) >= ctx.cursor.EndPtr())
                        {
                            return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Truncated unicode escape")));
                        }
                        scan += 4;
                    }
                }
                ++scan;
            }

            if (scan >= ctx.cursor.EndPtr())
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated string")));
            }

            const UIntSize rawLength = static_cast<UIntSize>(scan - start);
            if (!hasEscapes)
            {
                ctx.cursor.Advance(rawLength + 1);
                return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {start, rawLength});
            }

            char* output = nullptr;
            if (ctx.options.inSitu && ctx.mutableBase)
            {
                output = const_cast<char*>(start);
            }
            else
            {
                void* memory = ctx.arena->Allocate(rawLength, alignof(char));
                if (!memory)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::OutOfMemory, "String allocation failed")));
                }
                output = static_cast<char*>(memory);
            }

            const char* read  = start;
            char*       write = output;
            const char* end   = start + rawLength;

            while (read < end)
            {
                char c = *read++;
                if (c != '\\')
                {
                    *write++ = c;
                    continue;
                }
                if (read >= end)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidStringEscape, "Invalid escape")));
                }
                const char esc = *read++;
                switch (esc)
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
                        if ((read + 3) >= end)
                        {
                            return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidUnicodeEscape, "Truncated unicode escape")));
                        }
                        UInt32 codepoint = 0;
                        if (!DecodeHex4(read, codepoint))
                        {
                            return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidUnicodeEscape, "Invalid unicode escape")));
                        }
                        read += 4;
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
                        {
                            if ((read + 6) <= end && read[0] == '\\' && read[1] == 'u')
                            {
                                UInt32 low = 0;
                                if (!DecodeHex4(read + 2, low))
                                {
                                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                            MakeError(ctx, ParseErrorCode::InvalidUnicodeEscape, "Invalid surrogate escape")));
                                }
                                if (low < 0xDC00 || low > 0xDFFF)
                                {
                                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                            MakeError(ctx, ParseErrorCode::InvalidUnicodeEscape, "Invalid surrogate pair")));
                                }
                                read += 6;
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                            }
                            else
                            {
                                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                        MakeError(ctx, ParseErrorCode::InvalidUnicodeEscape, "Missing low surrogate")));
                            }
                        }
                        write += EncodeUtf8(codepoint, write);
                        break;
                    }
                    default:
                        return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidStringEscape, "Invalid escape")));
                }
            }

            ctx.cursor.Advance(rawLength + 1);
            return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {output, static_cast<UIntSize>(write - output)});
        }

        NGIN::Utilities::Expected<F64, ParseError> ParseNumber(JsonParseContext& ctx)
        {
            const char* start = ctx.cursor.CurrentPtr();
            const char* p     = start;

            if (*p == '-')
                ++p;
            if (p >= ctx.cursor.EndPtr())
            {
                return NGIN::Utilities::Expected<F64, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unexpected end in number")));
            }
            if (*p == '0')
            {
                ++p;
            }
            else
            {
                if (!IsDigit(*p))
                {
                    return NGIN::Utilities::Expected<F64, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid number")));
                }
                while (p < ctx.cursor.EndPtr() && IsDigit(*p))
                    ++p;
            }

            if (p < ctx.cursor.EndPtr() && *p == '.')
            {
                ++p;
                if (p >= ctx.cursor.EndPtr() || !IsDigit(*p))
                {
                    return NGIN::Utilities::Expected<F64, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid fraction")));
                }
                while (p < ctx.cursor.EndPtr() && IsDigit(*p))
                    ++p;
            }

            if (p < ctx.cursor.EndPtr() && (*p == 'e' || *p == 'E'))
            {
                ++p;
                if (p < ctx.cursor.EndPtr() && (*p == '+' || *p == '-'))
                    ++p;
                if (p >= ctx.cursor.EndPtr() || !IsDigit(*p))
                {
                    return NGIN::Utilities::Expected<F64, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid exponent")));
                }
                while (p < ctx.cursor.EndPtr() && IsDigit(*p))
                    ++p;
            }

            const auto len    = static_cast<std::size_t>(p - start);
            F64        value  = 0.0;
            const auto result = std::from_chars(start, start + len, value, std::chars_format::general);
            if (result.ec != std::errc {})
            {
                return NGIN::Utilities::Expected<F64, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidNumber, "Invalid number")));
            }
            ctx.cursor.Advance(static_cast<UIntSize>(len));
            return NGIN::Utilities::Expected<F64, ParseError>(value);
        }

        NGIN::Utilities::Expected<JsonValue, ParseError> ParseValue(JsonParseContext& ctx, JsonAllocator allocator);

        NGIN::Utilities::Expected<JsonValue, ParseError> ParseArray(JsonParseContext& ctx, JsonAllocator allocator)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Array nesting too deep")));
            }
            ++ctx.depth;

            ctx.cursor.Advance();
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(skipResult.ErrorUnsafe())));

            void* memory = ctx.arena->Allocate(sizeof(JsonArray), alignof(JsonArray));
            if (!memory)
            {
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Array allocation failed")));
            }
            auto* array = new (memory) JsonArray(allocator);

            if (ctx.cursor.Peek() == ']')
            {
                ctx.cursor.Advance();
                --ctx.depth;
                return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeArray(array));
            }

            while (true)
            {
                auto valueResult = ParseValue(ctx, allocator);
                if (!valueResult.HasValue())
                    return valueResult;
                array->values.PushBack(std::move(valueResult.ValueUnsafe()));

                auto postResult = SkipWhitespaceAndComments(ctx);
                if (!postResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(postResult.ErrorUnsafe())));

                const char next = ctx.cursor.Peek();
                if (next == ',')
                {
                    ctx.cursor.Advance();
                    auto commaResult = SkipWhitespaceAndComments(ctx);
                    if (!commaResult.HasValue())
                        return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commaResult.ErrorUnsafe())));
                    if (ctx.cursor.Peek() == ']' && !ctx.options.allowTrailingCommas)
                    {
                        return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Trailing comma in array")));
                    }
                    continue;
                }
                if (next == ']')
                {
                    ctx.cursor.Advance();
                    break;
                }
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ',' or ']'")));
            }

            --ctx.depth;
            return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeArray(array));
        }

        NGIN::Utilities::Expected<JsonValue, ParseError> ParseObject(JsonParseContext& ctx, JsonAllocator allocator)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Object nesting too deep")));
            }
            ++ctx.depth;

            ctx.cursor.Advance();
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(skipResult.ErrorUnsafe())));

            void* memory = ctx.arena->Allocate(sizeof(JsonObject), alignof(JsonObject));
            if (!memory)
            {
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Object allocation failed")));
            }
            auto* object = new (memory) JsonObject(allocator);

            if (ctx.cursor.Peek() == '}')
            {
                ctx.cursor.Advance();
                --ctx.depth;
                return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeObject(object));
            }

            while (true)
            {
                auto keyResult = ParseString(ctx);
                if (!keyResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(keyResult.ErrorUnsafe())));
                const std::string_view key = keyResult.ValueUnsafe();

                auto colonResult = SkipWhitespaceAndComments(ctx);
                if (!colonResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(colonResult.ErrorUnsafe())));
                if (ctx.cursor.Peek() != ':')
                {
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ':'")));
                }
                ctx.cursor.Advance();

                auto valueSkip = SkipWhitespaceAndComments(ctx);
                if (!valueSkip.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(valueSkip.ErrorUnsafe())));

                auto valueResult = ParseValue(ctx, allocator);
                if (!valueResult.HasValue())
                    return valueResult;

                object->members.PushBack(JsonMember {key, std::move(valueResult.ValueUnsafe())});

                auto postResult = SkipWhitespaceAndComments(ctx);
                if (!postResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(postResult.ErrorUnsafe())));

                const char next = ctx.cursor.Peek();
                if (next == ',')
                {
                    ctx.cursor.Advance();
                    auto commaResult = SkipWhitespaceAndComments(ctx);
                    if (!commaResult.HasValue())
                        return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commaResult.ErrorUnsafe())));
                    if (ctx.cursor.Peek() == '}' && !ctx.options.allowTrailingCommas)
                    {
                        return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Trailing comma in object")));
                    }
                    continue;
                }
                if (next == '}')
                {
                    ctx.cursor.Advance();
                    break;
                }
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ',' or '}'")));
            }

            --ctx.depth;
            return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeObject(object));
        }

        NGIN::Utilities::Expected<JsonValue, ParseError> ParseValue(JsonParseContext& ctx, JsonAllocator allocator)
        {
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(skipResult.ErrorUnsafe())));

            const char c = ctx.cursor.Peek();
            if (c == 'n')
            {
                if (ctx.cursor.Peek(1) == 'u' && ctx.cursor.Peek(2) == 'l' && ctx.cursor.Peek(3) == 'l')
                {
                    ctx.cursor.Advance(4);
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeNull());
                }
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == 't')
            {
                if (ctx.cursor.Peek(1) == 'r' && ctx.cursor.Peek(2) == 'u' && ctx.cursor.Peek(3) == 'e')
                {
                    ctx.cursor.Advance(4);
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeBool(true));
                }
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == 'f')
            {
                if (ctx.cursor.Peek(1) == 'a' && ctx.cursor.Peek(2) == 'l' && ctx.cursor.Peek(3) == 's' && ctx.cursor.Peek(4) == 'e')
                {
                    ctx.cursor.Advance(5);
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeBool(false));
                }
                return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == '"')
            {
                auto stringResult = ParseString(ctx);
                if (!stringResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(stringResult.ErrorUnsafe())));
                return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeString(stringResult.ValueUnsafe()));
            }
            if (c == '{')
                return ParseObject(ctx, allocator);
            if (c == '[')
                return ParseArray(ctx, allocator);
            if (c == '-' || IsDigit(c))
            {
                auto numberResult = ParseNumber(ctx);
                if (!numberResult.HasValue())
                    return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(numberResult.ErrorUnsafe())));
                return NGIN::Utilities::Expected<JsonValue, ParseError>(JsonValue::MakeNumber(numberResult.ValueUnsafe()));
            }
            return NGIN::Utilities::Expected<JsonValue, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Unexpected token")));
        }

        NGIN::Utilities::Expected<void, ParseError> ParseValueEvents(JsonParseContext& ctx, JsonReader& reader);

        NGIN::Utilities::Expected<void, ParseError> ParseArrayEvents(JsonParseContext& ctx, JsonReader& reader)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Array nesting too deep")));
            }
            ++ctx.depth;

            ctx.cursor.Advance();
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return skipResult;

            if (!reader.OnStartArray())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected array")));
            }

            if (ctx.cursor.Peek() == ']')
            {
                ctx.cursor.Advance();
                --ctx.depth;
                if (!reader.OnEndArray())
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected array")));
                }
                return {};
            }

            while (true)
            {
                auto valueResult = ParseValueEvents(ctx, reader);
                if (!valueResult.HasValue())
                    return valueResult;

                auto postResult = SkipWhitespaceAndComments(ctx);
                if (!postResult.HasValue())
                    return postResult;

                const char next = ctx.cursor.Peek();
                if (next == ',')
                {
                    ctx.cursor.Advance();
                    auto commaResult = SkipWhitespaceAndComments(ctx);
                    if (!commaResult.HasValue())
                        return commaResult;
                    if (ctx.cursor.Peek() == ']' && !ctx.options.allowTrailingCommas)
                    {
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Trailing comma in array")));
                    }
                    continue;
                }
                if (next == ']')
                {
                    ctx.cursor.Advance();
                    break;
                }
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ',' or ']'")));
            }

            --ctx.depth;
            if (!reader.OnEndArray())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected array")));
            }
            return {};
        }

        NGIN::Utilities::Expected<void, ParseError> ParseObjectEvents(JsonParseContext& ctx, JsonReader& reader)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Object nesting too deep")));
            }
            ++ctx.depth;

            ctx.cursor.Advance();
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return skipResult;

            if (!reader.OnStartObject())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected object")));
            }

            if (ctx.cursor.Peek() == '}')
            {
                ctx.cursor.Advance();
                --ctx.depth;
                if (!reader.OnEndObject())
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected object")));
                }
                return {};
            }

            while (true)
            {
                auto keyResult = ParseString(ctx);
                if (!keyResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(keyResult.ErrorUnsafe())));
                if (!reader.OnKey(keyResult.ValueUnsafe()))
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected key")));
                }

                auto colonResult = SkipWhitespaceAndComments(ctx);
                if (!colonResult.HasValue())
                    return colonResult;
                if (ctx.cursor.Peek() != ':')
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ':'")));
                }
                ctx.cursor.Advance();

                auto valueSkip = SkipWhitespaceAndComments(ctx);
                if (!valueSkip.HasValue())
                    return valueSkip;

                auto valueResult = ParseValueEvents(ctx, reader);
                if (!valueResult.HasValue())
                    return valueResult;

                auto postResult = SkipWhitespaceAndComments(ctx);
                if (!postResult.HasValue())
                    return postResult;

                const char next = ctx.cursor.Peek();
                if (next == ',')
                {
                    ctx.cursor.Advance();
                    auto commaResult = SkipWhitespaceAndComments(ctx);
                    if (!commaResult.HasValue())
                        return commaResult;
                    if (ctx.cursor.Peek() == '}' && !ctx.options.allowTrailingCommas)
                    {
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Trailing comma in object")));
                    }
                    continue;
                }
                if (next == '}')
                {
                    ctx.cursor.Advance();
                    break;
                }
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Expected ',' or '}'")));
            }

            --ctx.depth;
            if (!reader.OnEndObject())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected object")));
            }
            return {};
        }

        NGIN::Utilities::Expected<void, ParseError> ParseValueEvents(JsonParseContext& ctx, JsonReader& reader)
        {
            auto skipResult = SkipWhitespaceAndComments(ctx);
            if (!skipResult.HasValue())
                return skipResult;

            const char c = ctx.cursor.Peek();
            if (c == 'n')
            {
                if (ctx.cursor.Peek(1) == 'u' && ctx.cursor.Peek(2) == 'l' && ctx.cursor.Peek(3) == 'l')
                {
                    ctx.cursor.Advance(4);
                    if (!reader.OnNull())
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected null")));
                    return {};
                }
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == 't')
            {
                if (ctx.cursor.Peek(1) == 'r' && ctx.cursor.Peek(2) == 'u' && ctx.cursor.Peek(3) == 'e')
                {
                    ctx.cursor.Advance(4);
                    if (!reader.OnBool(true))
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected bool")));
                    return {};
                }
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == 'f')
            {
                if (ctx.cursor.Peek(1) == 'a' && ctx.cursor.Peek(2) == 'l' && ctx.cursor.Peek(3) == 's' && ctx.cursor.Peek(4) == 'e')
                {
                    ctx.cursor.Advance(5);
                    if (!reader.OnBool(false))
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected bool")));
                    return {};
                }
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid literal")));
            }
            if (c == '"')
            {
                auto stringResult = ParseString(ctx);
                if (!stringResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(stringResult.ErrorUnsafe())));
                if (!reader.OnString(stringResult.ValueUnsafe()))
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected string")));
                return {};
            }
            if (c == '{')
                return ParseObjectEvents(ctx, reader);
            if (c == '[')
                return ParseArrayEvents(ctx, reader);
            if (c == '-' || IsDigit(c))
            {
                auto numberResult = ParseNumber(ctx);
                if (!numberResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(numberResult.ErrorUnsafe())));
                if (!reader.OnNumber(numberResult.ValueUnsafe()))
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected number")));
                return {};
            }
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedCharacter, "Unexpected token")));
        }
    }// namespace

    JsonDocument::JsonDocument(UIntSize arenaBytes)
        : m_arena(arenaBytes)
    {
    }

    JsonValue* JsonObject::Find(JsonStringView key) noexcept
    {
        if (m_index)
        {
            if (!m_index->Contains(key))
                return nullptr;
            const UIntSize index = m_index->GetRef(key);
            if (index >= members.Size())
                return nullptr;
            return &members[index].value;
        }
        for (UIntSize i = 0; i < members.Size(); ++i)
        {
            if (members[i].name == key)
                return &members[i].value;
        }
        return nullptr;
    }

    const JsonValue* JsonObject::Find(JsonStringView key) const noexcept
    {
        if (m_index)
        {
            if (!m_index->Contains(key))
                return nullptr;
            const UIntSize index = m_index->GetRef(key);
            if (index >= members.Size())
                return nullptr;
            return &members[index].value;
        }
        for (UIntSize i = 0; i < members.Size(); ++i)
        {
            if (members[i].name == key)
                return &members[i].value;
        }
        return nullptr;
    }

    bool JsonObject::Set(JsonStringView key, const JsonValue& value) noexcept
    {
        for (UIntSize i = 0; i < members.Size(); ++i)
        {
            if (members[i].name == key)
            {
                members[i].value = value;
                if (m_index)
                    m_index->Insert(key, i);
                return true;
            }
        }
        members.PushBack(JsonMember {key, value});
        if (m_index)
            m_index->Insert(key, members.Size() - 1);
        return true;
    }

    bool JsonObject::BuildIndex() noexcept
    {
        if (m_index)
            return true;

        void* memory = m_allocator.Allocate(sizeof(IndexMap), alignof(IndexMap));
        if (!memory)
            return false;
        m_index = new (memory) IndexMap(members.Size() * 2 + 1, std::hash<JsonStringView> {}, std::equal_to<JsonStringView> {}, m_allocator);
        for (UIntSize i = 0; i < members.Size(); ++i)
            m_index->Insert(members[i].name, i);
        return true;
    }

    NGIN::Utilities::Expected<JsonDocument, ParseError>
    JsonParser::Parse(std::span<const NGIN::Byte> input, const JsonParseOptions& options)
    {
        const UIntSize arenaBytes = options.arenaBytes != 0 ? options.arenaBytes : (input.size() * 2 + 4096);
        JsonDocument   document(arenaBytes);

        JsonParseContext ctx {
                InputCursor(input, options.trackLocation),
                options,
                &document.Arena(),
                nullptr,
                0,
        };

        try
        {
            auto valueResult = ParseValue(ctx, document.Allocator());
            if (!valueResult.HasValue())
                return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(valueResult.ErrorUnsafe())));

            document.Root() = std::move(valueResult.ValueUnsafe());

            auto tailResult = SkipWhitespaceAndComments(ctx);
            if (!tailResult.HasValue())
                return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(tailResult.ErrorUnsafe())));
        } catch (const std::bad_alloc&)
        {
            return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::OutOfMemory, "Allocation failed")));
        }

        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after JSON")));
        }

        return NGIN::Utilities::Expected<JsonDocument, ParseError>(std::move(document));
    }

    NGIN::Utilities::Expected<JsonDocument, ParseError>
    JsonParser::Parse(std::span<NGIN::Byte> input, const JsonParseOptions& options)
    {
        JsonParseOptions inSituOptions = options;
        inSituOptions.inSitu           = true;
        const UIntSize arenaBytes      = inSituOptions.arenaBytes != 0 ? inSituOptions.arenaBytes : (input.size() * 2 + 4096);
        JsonDocument   document(arenaBytes);

        JsonParseContext ctx {
                InputCursor(std::span<const NGIN::Byte>(input.data(), input.size()), inSituOptions.trackLocation),
                inSituOptions,
                &document.Arena(),
                reinterpret_cast<char*>(input.data()),
                0,
        };

        try
        {
            auto valueResult = ParseValue(ctx, document.Allocator());
            if (!valueResult.HasValue())
                return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(valueResult.ErrorUnsafe())));

            document.Root() = std::move(valueResult.ValueUnsafe());

            auto tailResult = SkipWhitespaceAndComments(ctx);
            if (!tailResult.HasValue())
                return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(tailResult.ErrorUnsafe())));
        } catch (const std::bad_alloc&)
        {
            return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::OutOfMemory, "Allocation failed")));
        }

        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after JSON")));
        }

        return NGIN::Utilities::Expected<JsonDocument, ParseError>(std::move(document));
    }

    NGIN::Utilities::Expected<JsonDocument, ParseError>
    JsonParser::Parse(std::string_view input, const JsonParseOptions& options)
    {
        return Parse(std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(input.data()), input.size()), options);
    }

    NGIN::Utilities::Expected<JsonDocument, ParseError>
    JsonParser::Parse(NGIN::IO::IByteReader& reader, const JsonParseOptions& options)
    {
        NGIN::Containers::Vector<NGIN::Byte> buffer;
        static constexpr UIntSize            chunkSize = 64 * 1024;
        NGIN::Byte                           temp[chunkSize];
        while (true)
        {
            auto readResult = reader.Read(std::span<NGIN::Byte>(temp, chunkSize));
            if (!readResult.HasValue())
            {
                ParseError err;
                err.code    = ParseErrorCode::InvalidToken;
                err.message = "Failed to read from reader";
                return NGIN::Utilities::Expected<JsonDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(err)));
            }
            const UIntSize readBytes = readResult.ValueUnsafe();
            if (readBytes == 0)
                break;
            for (UIntSize i = 0; i < readBytes; ++i)
                buffer.PushBack(temp[i]);
        }
        auto result = Parse(std::span<const NGIN::Byte>(buffer.data(), buffer.Size()), options);
        if (result.HasValue())
            result.ValueUnsafe().AdoptInput(std::move(buffer));
        return result;
    }

    NGIN::Utilities::Expected<void, ParseError>
    JsonParser::Parse(JsonReader& reader, std::span<const NGIN::Byte> input, const JsonParseOptions& options)
    {
        const UIntSize arenaBytes = options.arenaBytes != 0 ? options.arenaBytes : (input.size() * 2 + 4096);
        JsonArena      arena(arenaBytes);

        JsonParseContext ctx {
                InputCursor(input, options.trackLocation),
                options,
                &arena,
                nullptr,
                0,
        };

        auto valueResult = ParseValueEvents(ctx, reader);
        if (!valueResult.HasValue())
            return valueResult;

        auto tailResult = SkipWhitespaceAndComments(ctx);
        if (!tailResult.HasValue())
            return tailResult;

        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after JSON")));
        }

        return {};
    }

    NGIN::Utilities::Expected<void, ParseError>
    JsonParser::Parse(JsonReader& reader, std::span<NGIN::Byte> input, const JsonParseOptions& options)
    {
        JsonParseOptions inSituOptions = options;
        inSituOptions.inSitu           = true;
        const UIntSize arenaBytes      = inSituOptions.arenaBytes != 0 ? inSituOptions.arenaBytes : (input.size() * 2 + 4096);
        JsonArena      arena(arenaBytes);

        JsonParseContext ctx {
                InputCursor(std::span<const NGIN::Byte>(input.data(), input.size()), inSituOptions.trackLocation),
                inSituOptions,
                &arena,
                reinterpret_cast<char*>(input.data()),
                0,
        };

        auto valueResult = ParseValueEvents(ctx, reader);
        if (!valueResult.HasValue())
            return valueResult;

        auto tailResult = SkipWhitespaceAndComments(ctx);
        if (!tailResult.HasValue())
            return tailResult;

        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after JSON")));
        }

        return {};
    }

}// namespace NGIN::Serialization
