#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/SIMD/Scan.hpp>

#include <cctype>
#include <cstring>
#include <functional>
#include <new>

namespace NGIN::Serialization
{
    namespace
    {
        static constexpr UIntSize kPrecomputeMinBytes = 32 * 1024;

        [[nodiscard]] bool ShouldPrecomputeContainers(const XmlParseOptions& options, UIntSize inputSize) noexcept
        {
            return options.precomputeContainerSizes && inputSize >= kPrecomputeMinBytes;
        }
        struct XmlElementCount;

        struct XmlParseContext
        {
            InputCursor     cursor;
            XmlParseOptions options;
            XmlArena*       arena {nullptr};
            char*           mutableBase {nullptr};
            char*           mutableEnd {nullptr};
            UIntSize        depth {0};
            const NGIN::Containers::Vector<struct XmlElementCount>* elementCounts {nullptr};
            UIntSize        elementIndex {0};
            XmlDocument*    document {nullptr};
        };

        struct XmlElementCount
        {
            UIntSize attributes {0};
            UIntSize children {0};
        };

        [[nodiscard]] ParseError MakeError(const XmlParseContext& ctx, ParseErrorCode code, const char* message)
        {
            ParseError err;
            err.code     = code;
            err.location = ctx.cursor.Location();
            err.message  = message;
            return err;
        }

        [[nodiscard]] bool IsAsciiAlpha(char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        [[nodiscard]] bool IsAsciiDigit(char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        [[nodiscard]] bool IsNameStart(char c) noexcept
        {
            return (c == ':' || c == '_' || IsAsciiAlpha(c));
        }

        [[nodiscard]] bool IsNameChar(char c) noexcept
        {
            return IsNameStart(c) || IsAsciiDigit(c) || c == '-' || c == '.';
        }

        [[nodiscard]] bool IsWhitespace(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        void SkipWhitespace(InputCursor& cursor) noexcept
        {
            while (IsWhitespace(cursor.Peek()))
                cursor.Advance();
        }

        [[nodiscard]] XmlElementCount NextElementCount(XmlParseContext& ctx) noexcept
        {
            if (!ctx.elementCounts)
                return {};
            if (ctx.elementIndex >= ctx.elementCounts->Size())
                return {};
            return (*ctx.elementCounts)[ctx.elementIndex++];
        }

        NGIN::Utilities::Expected<void, ParseError> SkipUntil(XmlParseContext& ctx, std::string_view endMarker)
        {
            while (!ctx.cursor.IsEof())
            {
                bool match = true;
                for (UIntSize i = 0; i < endMarker.size(); ++i)
                {
                    if (ctx.cursor.Peek(i) != endMarker[i])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    ctx.cursor.Advance(static_cast<UIntSize>(endMarker.size()));
                    return {};
                }
                ctx.cursor.Advance();
            }
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unexpected end of XML")));
        }

        NGIN::Utilities::Expected<void, ParseError> SkipComment(XmlParseContext& ctx)
        {
            return SkipUntil(ctx, "-->");
        }

        NGIN::Utilities::Expected<void, ParseError> SkipProcessingInstruction(XmlParseContext& ctx)
        {
            return SkipUntil(ctx, "?>");
        }

        NGIN::Utilities::Expected<void, ParseError> SkipDoctype(XmlParseContext& ctx)
        {
            int bracketDepth = 0;
            while (!ctx.cursor.IsEof())
            {
                const char c = ctx.cursor.Peek();
                if (c == '[')
                    ++bracketDepth;
                else if (c == ']')
                    --bracketDepth;
                else if (c == '>' && bracketDepth <= 0)
                {
                    ctx.cursor.Advance();
                    return {};
                }
                ctx.cursor.Advance();
            }
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated DOCTYPE")));
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> ParseName(XmlParseContext& ctx)
        {
            const char* start = ctx.cursor.CurrentPtr();
            const char  first = ctx.cursor.Peek();
            if (!IsNameStart(first))
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid name")));
            }
            ctx.cursor.Advance();
            while (IsNameChar(ctx.cursor.Peek()))
                ctx.cursor.Advance();
            const char* end = ctx.cursor.CurrentPtr();
            return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {start, static_cast<UIntSize>(end - start)});
        }

        [[nodiscard]] UInt32 HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return static_cast<UInt32>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<UInt32>(c - 'a' + 10);
            return static_cast<UInt32>(c - 'A' + 10);
        }

        bool DecodeHex(const char* start, const char* end, UInt32& out, bool hex) noexcept
        {
            out = 0;
            for (const char* p = start; p < end; ++p)
            {
                const char c = *p;
                if (hex)
                {
                    if (!std::isxdigit(static_cast<unsigned char>(c)))
                        return false;
                    out = static_cast<UInt32>((out << 4) | HexValue(c));
                }
                else
                {
                    if (!std::isdigit(static_cast<unsigned char>(c)))
                        return false;
                    out = static_cast<UInt32>(out * 10U + static_cast<UInt32>(c - '0'));
                }
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

        NGIN::Utilities::Expected<std::string_view, ParseError> DecodeEntitiesInternal(XmlParseContext& ctx, std::string_view input)
        {
            if (!ctx.options.decodeEntities)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            const std::size_t ampPos = input.find('&');
            if (ampPos == std::string_view::npos)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            const char* rawStart = input.data();
            const char* rawEnd   = rawStart + input.size();

            char* output = nullptr;
            if (ctx.options.inSitu && ctx.mutableBase && rawStart >= ctx.mutableBase && rawEnd <= ctx.mutableEnd)
            {
                output = const_cast<char*>(rawStart);
            }
            else
            {
                void* memory = ctx.arena->Allocate(input.size(), alignof(char));
                if (!memory)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::OutOfMemory, "Entity allocation failed")));
                }
                output = static_cast<char*>(memory);
            }
            char* write  = output;

            const char* data = input.data();
            const char* end  = data + input.size();
            const char* ptr  = data;

            while (ptr < end)
            {
                if (*ptr != '&')
                {
                    *write++ = *ptr++;
                    continue;
                }

                const char* entityStart = ptr + 1;
                const char* entityEnd   = entityStart;
                while (entityEnd < end && *entityEnd != ';')
                    ++entityEnd;
                if (entityEnd >= end)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidEntity, "Unterminated entity")));
                }
                std::string_view entity(entityStart, static_cast<std::size_t>(entityEnd - entityStart));
                ptr = entityEnd + 1;

                if (entity == "lt")
                {
                    *write++ = '<';
                }
                else if (entity == "gt")
                {
                    *write++ = '>';
                }
                else if (entity == "amp")
                {
                    *write++ = '&';
                }
                else if (entity == "apos")
                {
                    *write++ = '\'';
                }
                else if (entity == "quot")
                {
                    *write++ = '"';
                }
                else if (!entity.empty() && entity.front() == '#')
                {
                    bool        hex      = false;
                    const char* numStart = entity.data() + 1;
                    if (numStart < entityEnd && (*numStart == 'x' || *numStart == 'X'))
                    {
                        hex = true;
                        ++numStart;
                    }
                    UInt32 codepoint = 0;
                    if (!DecodeHex(numStart, entity.data() + entity.size(), codepoint, hex))
                    {
                        return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidEntity, "Invalid numeric entity")));
                    }
                    write += EncodeUtf8(codepoint, write);
                }
                else
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidEntity, "Unknown entity")));
                }
            }

            return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {output, static_cast<UIntSize>(write - output)});
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> NormalizeWhitespaceInternal(XmlParseContext& ctx, std::string_view input)
        {
            if (!ctx.options.normalizeWhitespace)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            bool hasNonWhitespace = false;
            for (char c: input)
            {
                if (!IsWhitespace(c))
                {
                    hasNonWhitespace = true;
                    break;
                }
            }
            if (!hasNonWhitespace)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {});

            bool needsNormalization = false;
            bool prevSpace          = false;
            for (char c: input)
            {
                const bool isSpace = IsWhitespace(c);
                if (isSpace)
                {
                    if (prevSpace)
                        needsNormalization = true;
                    prevSpace = true;
                }
                else
                {
                    prevSpace = false;
                }
            }
            if (IsWhitespace(input.front()) || IsWhitespace(input.back()))
                needsNormalization = true;

            if (!needsNormalization)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            const char* rawStart = input.data();
            const char* rawEnd   = rawStart + input.size();
            char*       output   = nullptr;

            if (ctx.options.inSitu && ctx.mutableBase && rawStart >= ctx.mutableBase && rawEnd <= ctx.mutableEnd)
            {
                output = const_cast<char*>(rawStart);
            }
            else
            {
                void* memory = ctx.arena->Allocate(input.size(), alignof(char));
                if (!memory)
                {
                    return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::OutOfMemory, "Whitespace normalization failed")));
                }
                output = static_cast<char*>(memory);
            }
            char* write  = output;

            bool inSpace = false;
            for (char c: input)
            {
                if (IsWhitespace(c))
                {
                    if (!inSpace)
                    {
                        *write++ = ' ';
                        inSpace  = true;
                    }
                }
                else
                {
                    *write++ = c;
                    inSpace  = false;
                }
            }
            if (write > output && *(write - 1) == ' ')
                --write;
            if (write > output && *output == ' ')
            {
                ++output;
            }

            return NGIN::Utilities::Expected<std::string_view, ParseError>(std::string_view {output, static_cast<UIntSize>(write - output)});
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> ParseAttributeValue(XmlParseContext& ctx)
        {
            const char quote = ctx.cursor.Peek();
            if (quote != '"' && quote != '\'')
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected attribute quote")));
            }
            ctx.cursor.Advance();
            const char* start = ctx.cursor.CurrentPtr();
            if (!ctx.options.trackLocation)
            {
                const std::size_t remaining = static_cast<std::size_t>(ctx.cursor.EndPtr() - start);
                const std::size_t offset    = NGIN::SIMD::FindEqByte(start, remaining, quote);
                ctx.cursor.Advance(static_cast<UIntSize>(offset));
            }
            else
            {
                while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != quote)
                    ctx.cursor.Advance();
            }
            if (ctx.cursor.IsEof())
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated attribute")));
            }
            const char* end = ctx.cursor.CurrentPtr();
            ctx.cursor.Advance();
            std::string_view raw(start, static_cast<UIntSize>(end - start));
            auto             decoded = DecodeEntitiesInternal(ctx, raw);
            if (!decoded.HasValue())
                return decoded;
            return decoded;
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> ParseText(XmlParseContext& ctx)
        {
            const char* start = ctx.cursor.CurrentPtr();
            if (!ctx.options.trackLocation)
            {
                const std::size_t remaining = static_cast<std::size_t>(ctx.cursor.EndPtr() - start);
                const std::size_t offset    = NGIN::SIMD::FindEqByte(start, remaining, '<');
                ctx.cursor.Advance(static_cast<UIntSize>(offset));
            }
            else
            {
                while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != '<')
                    ctx.cursor.Advance();
            }
            const char*      end = ctx.cursor.CurrentPtr();
            std::string_view raw(start, static_cast<UIntSize>(end - start));
            if (raw.empty())
                return NGIN::Utilities::Expected<std::string_view, ParseError>(raw);
            auto decoded = DecodeEntitiesInternal(ctx, raw);
            if (!decoded.HasValue())
                return decoded;
            return NormalizeWhitespaceInternal(ctx, decoded.ValueUnsafe());
        }

        NGIN::Utilities::Expected<void, ParseError> SkipAttributeValueRaw(XmlParseContext& ctx)
        {
            const char quote = ctx.cursor.Peek();
            if (quote != '"' && quote != '\'')
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected attribute quote")));
            }
            ctx.cursor.Advance();
            const char* start = ctx.cursor.CurrentPtr();
            if (!ctx.options.trackLocation)
            {
                const std::size_t remaining = static_cast<std::size_t>(ctx.cursor.EndPtr() - start);
                const std::size_t offset    = NGIN::SIMD::FindEqByte(start, remaining, quote);
                ctx.cursor.Advance(static_cast<UIntSize>(offset));
            }
            else
            {
                while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != quote)
                    ctx.cursor.Advance();
            }
            if (ctx.cursor.IsEof())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated attribute")));
            }
            ctx.cursor.Advance();
            return {};
        }

        NGIN::Utilities::Expected<bool, ParseError> SkipTextRaw(XmlParseContext& ctx)
        {
            const char* start = ctx.cursor.CurrentPtr();
            if (!ctx.options.trackLocation)
            {
                const std::size_t remaining = static_cast<std::size_t>(ctx.cursor.EndPtr() - start);
                const std::size_t offset    = NGIN::SIMD::FindEqByte(start, remaining, '<');
                ctx.cursor.Advance(static_cast<UIntSize>(offset));
            }
            else
            {
                while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != '<')
                    ctx.cursor.Advance();
            }
            return NGIN::Utilities::Expected<bool, ParseError>(
                    static_cast<UIntSize>(ctx.cursor.CurrentPtr() - start) > 0);
        }

        NGIN::Utilities::Expected<void, ParseError> CountElement(XmlParseContext& ctx,
                                                                 NGIN::Containers::Vector<XmlElementCount>& elementCounts)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Element nesting too deep")));
            }
            ++ctx.depth;

            if (ctx.cursor.Peek() != '<')
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected element start")));
            }
            ctx.cursor.Advance();

            auto nameResult = ParseName(ctx);
            if (!nameResult.HasValue())
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(nameResult.ErrorUnsafe())));

            const std::string_view elementName = nameResult.ValueUnsafe();
            const UIntSize         countIndex  = elementCounts.Size();
            elementCounts.PushBack(XmlElementCount {});

            UIntSize attributeCount = 0;
            while (!ctx.cursor.IsEof())
            {
                SkipWhitespace(ctx.cursor);
                const char c = ctx.cursor.Peek();
                if (c == '/' && ctx.cursor.Peek(1) == '>')
                {
                    ctx.cursor.Advance(2);
                    elementCounts[countIndex] = XmlElementCount {attributeCount, 0};
                    --ctx.depth;
                    return {};
                }
                if (c == '>')
                {
                    ctx.cursor.Advance();
                    break;
                }

                auto attrNameResult = ParseName(ctx);
                if (!attrNameResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(attrNameResult.ErrorUnsafe())));

                SkipWhitespace(ctx.cursor);
                if (ctx.cursor.Peek() != '=')
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '='")));
                }
                ctx.cursor.Advance();
                SkipWhitespace(ctx.cursor);

                auto attrSkipResult = SkipAttributeValueRaw(ctx);
                if (!attrSkipResult.HasValue())
                    return attrSkipResult;
                ++attributeCount;
            }

            UIntSize childCount = 0;
            while (!ctx.cursor.IsEof())
            {
                if (ctx.cursor.Peek() == '<')
                {
                    if (ctx.cursor.Peek(1) == '/')
                    {
                        ctx.cursor.Advance(2);
                        auto endNameResult = ParseName(ctx);
                        if (!endNameResult.HasValue())
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(endNameResult.ErrorUnsafe())));
                        SkipWhitespace(ctx.cursor);
                        if (ctx.cursor.Peek() != '>')
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '>'")));
                        }
                        ctx.cursor.Advance();
                        if (endNameResult.ValueUnsafe() != elementName)
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::MismatchedTag, "Mismatched end tag")));
                        }
                        elementCounts[countIndex] = XmlElementCount {attributeCount, childCount};
                        --ctx.depth;
                        return {};
                    }
                    if (ctx.cursor.Peek(1) == '!')
                    {
                        if (ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
                        {
                            if (!ctx.options.allowComments)
                            {
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                        MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                            }
                            ctx.cursor.Advance(4);
                            auto commentResult = SkipComment(ctx);
                            if (!commentResult.HasValue())
                                return commentResult;
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == '[' && ctx.cursor.Peek(3) == 'C' && ctx.cursor.Peek(4) == 'D' && ctx.cursor.Peek(5) == 'A' && ctx.cursor.Peek(6) == 'T' && ctx.cursor.Peek(7) == 'A' && ctx.cursor.Peek(8) == '[')
                        {
                            ctx.cursor.Advance(9);
                            auto cdataResult = SkipUntil(ctx, "]]>");
                            if (!cdataResult.HasValue())
                                return cdataResult;
                            ++childCount;
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == 'D' && ctx.cursor.Peek(3) == 'O' && ctx.cursor.Peek(4) == 'C' && ctx.cursor.Peek(5) == 'T' && ctx.cursor.Peek(6) == 'Y' && ctx.cursor.Peek(7) == 'P' && ctx.cursor.Peek(8) == 'E')
                        {
                            ctx.cursor.Advance(9);
                            auto doctypeResult = SkipDoctype(ctx);
                            if (!doctypeResult.HasValue())
                                return doctypeResult;
                            continue;
                        }
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid markup declaration")));
                    }
                    if (ctx.cursor.Peek(1) == '?')
                    {
                        if (!ctx.options.allowProcessingInstructions)
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Processing instruction not allowed")));
                        }
                        ctx.cursor.Advance(2);
                        auto piResult = SkipProcessingInstruction(ctx);
                        if (!piResult.HasValue())
                            return piResult;
                        continue;
                    }

                    auto childResult = CountElement(ctx, elementCounts);
                    if (!childResult.HasValue())
                        return childResult;
                    ++childCount;
                    continue;
                }

                auto textResult = SkipTextRaw(ctx);
                if (!textResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(textResult.ErrorUnsafe())));
                if (textResult.ValueUnsafe())
                    ++childCount;
            }

            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unexpected end of XML")));
        }

        NGIN::Utilities::Expected<void, ParseError> CountDocument(XmlParseContext& ctx,
                                                                  NGIN::Containers::Vector<XmlElementCount>& elementCounts)
        {
            SkipWhitespace(ctx.cursor);

            while (!ctx.cursor.IsEof())
            {
                if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '?')
                {
                    ctx.cursor.Advance(2);
                    auto piResult = SkipProcessingInstruction(ctx);
                    if (!piResult.HasValue())
                        return piResult;
                    SkipWhitespace(ctx.cursor);
                    continue;
                }
                if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
                {
                    if (!ctx.options.allowComments)
                    {
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                    }
                    ctx.cursor.Advance(4);
                    auto commentResult = SkipComment(ctx);
                    if (!commentResult.HasValue())
                        return commentResult;
                    SkipWhitespace(ctx.cursor);
                    continue;
                }
                if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == 'D')
                {
                    ctx.cursor.Advance(9);
                    auto doctypeResult = SkipDoctype(ctx);
                    if (!doctypeResult.HasValue())
                        return doctypeResult;
                    SkipWhitespace(ctx.cursor);
                    continue;
                }
                break;
            }

            auto rootResult = CountElement(ctx, elementCounts);
            if (!rootResult.HasValue())
                return rootResult;

            SkipWhitespace(ctx.cursor);
            if (!ctx.cursor.IsEof())
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after XML")));
            }

            return {};
        }

        NGIN::Utilities::Expected<XmlElement*, ParseError> ParseElement(XmlParseContext& ctx, XmlAllocator allocator);

        NGIN::Utilities::Expected<XmlElement*, ParseError> ParseElement(XmlParseContext& ctx, XmlAllocator allocator)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Element nesting too deep")));
            }
            ++ctx.depth;

            if (ctx.cursor.Peek() != '<')
            {
                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected element start")));
            }
            ctx.cursor.Advance();

            auto nameResult = ParseName(ctx);
            if (!nameResult.HasValue())
                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(nameResult.ErrorUnsafe())));

            std::string_view elementName = nameResult.ValueUnsafe();
            if (ctx.options.internNames && ctx.document)
            {
                const std::string_view interned = ctx.document->InternString(elementName);
                if (!interned.data() && !elementName.empty())
                {
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::OutOfMemory, "Element name interning failed")));
                }
                elementName = interned;
            }

            void* memory = ctx.arena->Allocate(sizeof(XmlElement), alignof(XmlElement));
            if (!memory)
            {
                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Element allocation failed")));
            }
            auto* element = new (memory) XmlElement(allocator);
            element->name = elementName;
            const XmlElementCount counts = NextElementCount(ctx);
            if (counts.attributes > 0)
                element->attributes.Reserve(counts.attributes);
            if (counts.children > 0)
                element->children.Reserve(counts.children);

            while (!ctx.cursor.IsEof())
            {
                SkipWhitespace(ctx.cursor);
                const char c = ctx.cursor.Peek();
                if (c == '/' && ctx.cursor.Peek(1) == '>')
                {
                    ctx.cursor.Advance(2);
                    --ctx.depth;
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(element);
                }
                if (c == '>')
                {
                    ctx.cursor.Advance();
                    break;
                }

                auto attrNameResult = ParseName(ctx);
                if (!attrNameResult.HasValue())
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(attrNameResult.ErrorUnsafe())));

                std::string_view attrName = attrNameResult.ValueUnsafe();
                if (ctx.options.internNames && ctx.document)
                {
                    const std::string_view interned = ctx.document->InternString(attrName);
                    if (!interned.data() && !attrName.empty())
                    {
                        return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::OutOfMemory, "Attribute name interning failed")));
                    }
                    attrName = interned;
                }

                SkipWhitespace(ctx.cursor);
                if (ctx.cursor.Peek() != '=')
                {
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '='")));
                }
                ctx.cursor.Advance();
                SkipWhitespace(ctx.cursor);

                auto attrValueResult = ParseAttributeValue(ctx);
                if (!attrValueResult.HasValue())
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(attrValueResult.ErrorUnsafe())));

                element->attributes.PushBack(XmlAttribute {attrName, attrValueResult.ValueUnsafe()});
            }

            while (!ctx.cursor.IsEof())
            {
                if (ctx.cursor.Peek() == '<')
                {
                    if (ctx.cursor.Peek(1) == '/')
                    {
                        ctx.cursor.Advance(2);
                        auto endNameResult = ParseName(ctx);
                        if (!endNameResult.HasValue())
                            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(endNameResult.ErrorUnsafe())));
                        SkipWhitespace(ctx.cursor);
                        if (ctx.cursor.Peek() != '>')
                        {
                            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '>'")));
                        }
                        ctx.cursor.Advance();
                        if (endNameResult.ValueUnsafe() != element->name)
                        {
                            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::MismatchedTag, "Mismatched end tag")));
                        }
                        --ctx.depth;
                        return NGIN::Utilities::Expected<XmlElement*, ParseError>(element);
                    }
                    if (ctx.cursor.Peek(1) == '!')
                    {
                        if (ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
                        {
                            if (!ctx.options.allowComments)
                            {
                                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                        MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                            }
                            ctx.cursor.Advance(4);
                            auto commentResult = SkipComment(ctx);
                            if (!commentResult.HasValue())
                                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commentResult.ErrorUnsafe())));
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == '[' && ctx.cursor.Peek(3) == 'C' && ctx.cursor.Peek(4) == 'D' && ctx.cursor.Peek(5) == 'A' && ctx.cursor.Peek(6) == 'T' && ctx.cursor.Peek(7) == 'A' && ctx.cursor.Peek(8) == '[')
                        {
                            ctx.cursor.Advance(9);
                            const char* start       = ctx.cursor.CurrentPtr();
                            auto        cdataResult = SkipUntil(ctx, "]]>");
                            if (!cdataResult.HasValue())
                                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(cdataResult.ErrorUnsafe())));
                            const char*      end = ctx.cursor.CurrentPtr() - 3;
                            std::string_view cdata(start, static_cast<UIntSize>(end - start));
                            element->children.PushBack(XmlNode::MakeCData(cdata));
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == 'D' && ctx.cursor.Peek(3) == 'O' && ctx.cursor.Peek(4) == 'C' && ctx.cursor.Peek(5) == 'T' && ctx.cursor.Peek(6) == 'Y' && ctx.cursor.Peek(7) == 'P' && ctx.cursor.Peek(8) == 'E')
                        {
                            ctx.cursor.Advance(9);
                            auto doctypeResult = SkipDoctype(ctx);
                            if (!doctypeResult.HasValue())
                                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(doctypeResult.ErrorUnsafe())));
                            continue;
                        }
                        return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid markup declaration")));
                    }
                    if (ctx.cursor.Peek(1) == '?')
                    {
                        if (!ctx.options.allowProcessingInstructions)
                        {
                            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Processing instruction not allowed")));
                        }
                        ctx.cursor.Advance(2);
                        auto piResult = SkipProcessingInstruction(ctx);
                        if (!piResult.HasValue())
                            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(piResult.ErrorUnsafe())));
                        continue;
                    }

                    auto childResult = ParseElement(ctx, allocator);
                    if (!childResult.HasValue())
                        return childResult;
                    element->children.PushBack(XmlNode::MakeElement(childResult.ValueUnsafe()));
                    continue;
                }

                auto textResult = ParseText(ctx);
                if (!textResult.HasValue())
                    return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(textResult.ErrorUnsafe())));
                if (!textResult.ValueUnsafe().empty())
                    element->children.PushBack(XmlNode::MakeText(textResult.ValueUnsafe()));
            }

            return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unexpected end of XML")));
        }

        NGIN::Utilities::Expected<void, ParseError> ParseElementEvents(XmlParseContext& ctx, XmlReader& reader);

        NGIN::Utilities::Expected<void, ParseError> ParseElementEvents(XmlParseContext& ctx, XmlReader& reader)
        {
            if (ctx.depth >= ctx.options.maxDepth)
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::DepthExceeded, "Element nesting too deep")));
            }
            ++ctx.depth;

            if (ctx.cursor.Peek() != '<')
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::InvalidToken, "Expected element start")));
            }
            ctx.cursor.Advance();

            auto nameResult = ParseName(ctx);
            if (!nameResult.HasValue())
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(nameResult.ErrorUnsafe())));

            const std::string_view elementName = nameResult.ValueUnsafe();
            if (!reader.OnStartElement(elementName))
            {
                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected element")));
            }

            while (!ctx.cursor.IsEof())
            {
                SkipWhitespace(ctx.cursor);
                const char c = ctx.cursor.Peek();
                if (c == '/' && ctx.cursor.Peek(1) == '>')
                {
                    ctx.cursor.Advance(2);
                    --ctx.depth;
                    if (!reader.OnEndElement(elementName))
                    {
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected element")));
                    }
                    return {};
                }
                if (c == '>')
                {
                    ctx.cursor.Advance();
                    break;
                }

                auto attrNameResult = ParseName(ctx);
                if (!attrNameResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(attrNameResult.ErrorUnsafe())));

                SkipWhitespace(ctx.cursor);
                if (ctx.cursor.Peek() != '=')
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '='")));
                }
                ctx.cursor.Advance();
                SkipWhitespace(ctx.cursor);

                auto attrValueResult = ParseAttributeValue(ctx);
                if (!attrValueResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(attrValueResult.ErrorUnsafe())));

                if (!reader.OnAttribute(attrNameResult.ValueUnsafe(), attrValueResult.ValueUnsafe()))
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected attribute")));
                }
            }

            while (!ctx.cursor.IsEof())
            {
                if (ctx.cursor.Peek() == '<')
                {
                    if (ctx.cursor.Peek(1) == '/')
                    {
                        ctx.cursor.Advance(2);
                        auto endNameResult = ParseName(ctx);
                        if (!endNameResult.HasValue())
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(endNameResult.ErrorUnsafe())));
                        SkipWhitespace(ctx.cursor);
                        if (ctx.cursor.Peek() != '>')
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Expected '>'")));
                        }
                        ctx.cursor.Advance();
                        if (endNameResult.ValueUnsafe() != elementName)
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::MismatchedTag, "Mismatched end tag")));
                        }
                        --ctx.depth;
                        if (!reader.OnEndElement(elementName))
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected element")));
                        }
                        return {};
                    }
                    if (ctx.cursor.Peek(1) == '!')
                    {
                        if (ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
                        {
                            if (!ctx.options.allowComments)
                            {
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                        MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                            }
                            ctx.cursor.Advance(4);
                            auto commentResult = SkipComment(ctx);
                            if (!commentResult.HasValue())
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commentResult.ErrorUnsafe())));
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == '[' && ctx.cursor.Peek(3) == 'C' && ctx.cursor.Peek(4) == 'D' && ctx.cursor.Peek(5) == 'A' && ctx.cursor.Peek(6) == 'T' && ctx.cursor.Peek(7) == 'A' && ctx.cursor.Peek(8) == '[')
                        {
                            ctx.cursor.Advance(9);
                            const char* start       = ctx.cursor.CurrentPtr();
                            auto        cdataResult = SkipUntil(ctx, "]]>");
                            if (!cdataResult.HasValue())
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(cdataResult.ErrorUnsafe())));
                            const char*      end = ctx.cursor.CurrentPtr() - 3;
                            std::string_view cdata(start, static_cast<UIntSize>(end - start));
                            if (!reader.OnCData(cdata))
                            {
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                        MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected CDATA")));
                            }
                            continue;
                        }
                        if (ctx.cursor.Peek(2) == 'D' && ctx.cursor.Peek(3) == 'O' && ctx.cursor.Peek(4) == 'C' && ctx.cursor.Peek(5) == 'T' && ctx.cursor.Peek(6) == 'Y' && ctx.cursor.Peek(7) == 'P' && ctx.cursor.Peek(8) == 'E')
                        {
                            ctx.cursor.Advance(9);
                            auto doctypeResult = SkipDoctype(ctx);
                            if (!doctypeResult.HasValue())
                                return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(doctypeResult.ErrorUnsafe())));
                            continue;
                        }
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::InvalidToken, "Invalid markup declaration")));
                    }
                    if (ctx.cursor.Peek(1) == '?')
                    {
                        if (!ctx.options.allowProcessingInstructions)
                        {
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                    MakeError(ctx, ParseErrorCode::InvalidToken, "Processing instruction not allowed")));
                        }
                        ctx.cursor.Advance(2);
                        auto piResult = SkipProcessingInstruction(ctx);
                        if (!piResult.HasValue())
                            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(piResult.ErrorUnsafe())));
                        continue;
                    }

                    auto childResult = ParseElementEvents(ctx, reader);
                    if (!childResult.HasValue())
                        return childResult;
                    continue;
                }

                auto textResult = ParseText(ctx);
                if (!textResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(textResult.ErrorUnsafe())));
                if (!textResult.ValueUnsafe().empty())
                {
                    if (!reader.OnText(textResult.ValueUnsafe()))
                    {
                        return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                                MakeError(ctx, ParseErrorCode::HandlerRejected, "Handler rejected text")));
                    }
                }
            }

            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unexpected end of XML")));
        }
    }// namespace

    XmlDocument::XmlDocument(UIntSize arenaBytes)
        : m_arena(arenaBytes)
    {
    }

    std::string_view XmlDocument::InternString(std::string_view value) noexcept
    {
        if (value.empty())
            return value;
        if (!m_interner)
        {
            void* memory = m_arena.Allocate(sizeof(InternMap), alignof(InternMap));
            if (!memory)
                return {};
            m_interner = new (memory) InternMap(0, std::hash<std::string_view> {}, std::equal_to<std::string_view> {}, Allocator());
        }
        if (m_interner->Contains(value))
            return m_interner->GetRef(value);

        void* memory = m_arena.Allocate(value.size(), alignof(char));
        if (!memory)
            return {};
        std::memcpy(memory, value.data(), value.size());
        std::string_view stored(static_cast<const char*>(memory), value.size());
        m_interner->Insert(stored, stored);
        return stored;
    }

    const XmlAttribute* XmlElement::FindAttribute(std::string_view key) const noexcept
    {
        if (m_index)
        {
            if (!m_index->Contains(key))
                return nullptr;
            const UIntSize index = m_index->GetRef(key);
            if (index >= attributes.Size())
                return nullptr;
            return &attributes[index];
        }
        for (UIntSize i = 0; i < attributes.Size(); ++i)
        {
            if (attributes[i].name == key)
                return &attributes[i];
        }
        return nullptr;
    }

    bool XmlElement::BuildAttributeIndex() noexcept
    {
        if (m_index)
            return true;

        void* memory = m_allocator.Allocate(sizeof(AttributeIndex), alignof(AttributeIndex));
        if (!memory)
            return false;
        m_index = new (memory) AttributeIndex(attributes.Size() * 2 + 1,
                                              std::hash<std::string_view> {},
                                              std::equal_to<std::string_view> {},
                                              m_allocator);
        for (UIntSize i = 0; i < attributes.Size(); ++i)
            m_index->Insert(attributes[i].name, i);
        return true;
    }

    NGIN::Utilities::Expected<XmlDocument, ParseError>
    XmlParser::Parse(std::span<const NGIN::Byte> input, const XmlParseOptions& options)
    {
        const UIntSize arenaBytes = options.arenaBytes != 0 ? options.arenaBytes : (input.size() * 2 + 4096);
        XmlDocument    document(arenaBytes);

        const bool doPrecompute = ShouldPrecomputeContainers(options, static_cast<UIntSize>(input.size()));
        NGIN::Containers::Vector<XmlElementCount> elementCounts;
        if (doPrecompute)
        {
            XmlParseContext countCtx {
                    InputCursor(input, options.trackLocation),
                    options,
                    nullptr,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    0,
                    nullptr,
            };

            auto countResult = CountDocument(countCtx, elementCounts);
            if (!countResult.HasValue())
                return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(countResult.ErrorUnsafe())));
        }

        XmlParseContext ctx {
                InputCursor(input, options.trackLocation),
                options,
                &document.Arena(),
                nullptr,
                nullptr,
                0,
                doPrecompute ? &elementCounts : nullptr,
                0,
                &document,
        };

        SkipWhitespace(ctx.cursor);

        while (!ctx.cursor.IsEof())
        {
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '?')
            {
                ctx.cursor.Advance(2);
                auto piResult = SkipProcessingInstruction(ctx);
                if (!piResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(piResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
            {
                if (!ctx.options.allowComments)
                {
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                }
                ctx.cursor.Advance(4);
                auto commentResult = SkipComment(ctx);
                if (!commentResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commentResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == 'D')
            {
                ctx.cursor.Advance(9);
                auto doctypeResult = SkipDoctype(ctx);
                if (!doctypeResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(doctypeResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            break;
        }

        try
        {
            auto rootResult = ParseElement(ctx, document.Allocator());
            if (!rootResult.HasValue())
                return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(rootResult.ErrorUnsafe())));
            document.SetRoot(rootResult.ValueUnsafe());
        } catch (const std::bad_alloc&)
        {
            return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::OutOfMemory, "Allocation failed")));
        }

        SkipWhitespace(ctx.cursor);
        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after XML")));
        }

        return NGIN::Utilities::Expected<XmlDocument, ParseError>(std::move(document));
    }

    NGIN::Utilities::Expected<XmlDocument, ParseError>
    XmlParser::Parse(std::string_view input, const XmlParseOptions& options)
    {
        return Parse(std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(input.data()), input.size()), options);
    }

    NGIN::Utilities::Expected<XmlDocument, ParseError>
    XmlParser::Parse(std::span<NGIN::Byte> input, const XmlParseOptions& options)
    {
        XmlParseOptions inSituOptions = options;
        inSituOptions.inSitu          = true;
        const UIntSize arenaBytes     = inSituOptions.arenaBytes != 0 ? inSituOptions.arenaBytes : (input.size() * 2 + 4096);
        XmlDocument    document(arenaBytes);

        const bool doPrecompute = ShouldPrecomputeContainers(inSituOptions, static_cast<UIntSize>(input.size()));
        NGIN::Containers::Vector<XmlElementCount> elementCounts;
        if (doPrecompute)
        {
            XmlParseContext countCtx {
                    InputCursor(std::span<const NGIN::Byte>(input.data(), input.size()), inSituOptions.trackLocation),
                    inSituOptions,
                    nullptr,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    0,
                    nullptr,
            };

            auto countResult = CountDocument(countCtx, elementCounts);
            if (!countResult.HasValue())
                return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(countResult.ErrorUnsafe())));
        }

        XmlParseContext ctx {
                InputCursor(std::span<const NGIN::Byte>(input.data(), input.size()), inSituOptions.trackLocation),
                inSituOptions,
                &document.Arena(),
                reinterpret_cast<char*>(input.data()),
                reinterpret_cast<char*>(input.data() + input.size()),
                0,
                doPrecompute ? &elementCounts : nullptr,
                0,
                &document,
        };

        SkipWhitespace(ctx.cursor);

        while (!ctx.cursor.IsEof())
        {
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '?')
            {
                ctx.cursor.Advance(2);
                auto piResult = SkipProcessingInstruction(ctx);
                if (!piResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(piResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
            {
                if (!ctx.options.allowComments)
                {
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                }
                ctx.cursor.Advance(4);
                auto commentResult = SkipComment(ctx);
                if (!commentResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commentResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == 'D')
            {
                ctx.cursor.Advance(9);
                auto doctypeResult = SkipDoctype(ctx);
                if (!doctypeResult.HasValue())
                    return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(doctypeResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            break;
        }

        try
        {
            auto rootResult = ParseElement(ctx, document.Allocator());
            if (!rootResult.HasValue())
                return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(rootResult.ErrorUnsafe())));
            document.SetRoot(rootResult.ValueUnsafe());
        } catch (const std::bad_alloc&)
        {
            return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::OutOfMemory, "Allocation failed")));
        }

        SkipWhitespace(ctx.cursor);
        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after XML")));
        }

        return NGIN::Utilities::Expected<XmlDocument, ParseError>(std::move(document));
    }

    NGIN::Utilities::Expected<XmlDocument, ParseError>
    XmlParser::Parse(NGIN::IO::IByteReader& reader, const XmlParseOptions& options)
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
                return NGIN::Utilities::Expected<XmlDocument, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(err)));
            }
            const UIntSize readBytes = readResult.ValueUnsafe();
            if (readBytes == 0)
                break;
            for (UIntSize i = 0; i < readBytes; ++i)
                buffer.PushBack(temp[i]);
        }
        auto result = options.inSitu
                              ? Parse(std::span<NGIN::Byte>(buffer.data(), buffer.Size()), options)
                              : Parse(std::span<const NGIN::Byte>(buffer.data(), buffer.Size()), options);
        if (result.HasValue())
            result.ValueUnsafe().AdoptInput(std::move(buffer));
        return result;
    }

    NGIN::Utilities::Expected<void, ParseError>
    XmlParser::Parse(XmlReader& reader, std::span<const NGIN::Byte> input, const XmlParseOptions& options)
    {
        const UIntSize arenaBytes = options.arenaBytes != 0 ? options.arenaBytes : (input.size() * 2 + 4096);
        XmlArena       arena(arenaBytes);

        XmlParseContext ctx {
                InputCursor(input, options.trackLocation),
                options,
                &arena,
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                nullptr,
        };

        SkipWhitespace(ctx.cursor);

        while (!ctx.cursor.IsEof())
        {
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '?')
            {
                ctx.cursor.Advance(2);
                auto piResult = SkipProcessingInstruction(ctx);
                if (!piResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(piResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == '-' && ctx.cursor.Peek(3) == '-')
            {
                if (!ctx.options.allowComments)
                {
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                            MakeError(ctx, ParseErrorCode::InvalidToken, "Comments not allowed")));
                }
                ctx.cursor.Advance(4);
                auto commentResult = SkipComment(ctx);
                if (!commentResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(commentResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            if (ctx.cursor.Peek() == '<' && ctx.cursor.Peek(1) == '!' && ctx.cursor.Peek(2) == 'D')
            {
                ctx.cursor.Advance(9);
                auto doctypeResult = SkipDoctype(ctx);
                if (!doctypeResult.HasValue())
                    return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(std::move(doctypeResult.ErrorUnsafe())));
                SkipWhitespace(ctx.cursor);
                continue;
            }
            break;
        }

        auto rootResult = ParseElementEvents(ctx, reader);
        if (!rootResult.HasValue())
            return rootResult;

        SkipWhitespace(ctx.cursor);
        if (!ctx.cursor.IsEof())
        {
            return NGIN::Utilities::Expected<void, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                    MakeError(ctx, ParseErrorCode::TrailingCharacters, "Trailing characters after XML")));
        }

        return {};
    }

    NGIN::Utilities::Expected<std::string_view, ParseError>
    XmlParser::DecodeEntities(XmlDocument& document, std::string_view input)
    {
        XmlParseOptions options;
        options.decodeEntities = true;

        XmlParseContext ctx {
                InputCursor(input, false),
                options,
                &document.Arena(),
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                nullptr,
        };

        return DecodeEntitiesInternal(ctx, input);
    }

    NGIN::Utilities::Expected<std::string_view, ParseError>
    XmlParser::NormalizeWhitespace(XmlDocument& document, std::string_view input)
    {
        XmlParseOptions options;
        options.normalizeWhitespace = true;

        XmlParseContext ctx {
                InputCursor(input, false),
                options,
                &document.Arena(),
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                nullptr,
        };

        return NormalizeWhitespaceInternal(ctx, input);
    }

    NGIN::Utilities::Expected<std::string_view, ParseError>
    XmlParser::DecodeText(XmlDocument& document, std::string_view input, bool normalizeWhitespace)
    {
        XmlParseOptions options;
        options.decodeEntities     = true;
        options.normalizeWhitespace = normalizeWhitespace;

        XmlParseContext ctx {
                InputCursor(input, false),
                options,
                &document.Arena(),
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                nullptr,
        };

        auto decoded = DecodeEntitiesInternal(ctx, input);
        if (!decoded.HasValue())
            return decoded;

        if (!normalizeWhitespace)
            return decoded;

        return NormalizeWhitespaceInternal(ctx, decoded.ValueUnsafe());
    }

}// namespace NGIN::Serialization
