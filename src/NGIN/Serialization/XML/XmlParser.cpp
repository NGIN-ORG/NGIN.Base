#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <NGIN/Containers/Vector.hpp>

#include <cctype>
#include <cstring>
#include <new>

namespace NGIN::Serialization
{
    namespace
    {
        struct XmlParseContext
        {
            InputCursor     cursor;
            XmlParseOptions options;
            XmlArena*       arena {nullptr};
            UIntSize        depth {0};
        };

        [[nodiscard]] ParseError MakeError(const XmlParseContext& ctx, ParseErrorCode code, const char* message)
        {
            ParseError err;
            err.code     = code;
            err.location = ctx.cursor.Location();
            err.message  = message;
            return err;
        }

        [[nodiscard]] bool IsNameStart(char c) noexcept
        {
            return (c == ':' || c == '_' || std::isalpha(static_cast<unsigned char>(c)) != 0);
        }

        [[nodiscard]] bool IsNameChar(char c) noexcept
        {
            return IsNameStart(c) || (std::isdigit(static_cast<unsigned char>(c)) != 0) || c == '-' || c == '.';
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

        NGIN::Utilities::Expected<std::string_view, ParseError> DecodeEntities(XmlParseContext& ctx, std::string_view input)
        {
            if (!ctx.options.decodeEntities)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            const std::size_t ampPos = input.find('&');
            if (ampPos == std::string_view::npos)
                return NGIN::Utilities::Expected<std::string_view, ParseError>(input);

            void* memory = ctx.arena->Allocate(input.size(), alignof(char));
            if (!memory)
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Entity allocation failed")));
            }
            char* output = static_cast<char*>(memory);
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

        NGIN::Utilities::Expected<std::string_view, ParseError> NormalizeWhitespace(XmlParseContext& ctx, std::string_view input)
        {
            if (ctx.options.preserveWhitespace)
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

            void* memory = ctx.arena->Allocate(input.size(), alignof(char));
            if (!memory)
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Whitespace normalization failed")));
            }
            char* output = static_cast<char*>(memory);
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
            while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != quote)
                ctx.cursor.Advance();
            if (ctx.cursor.IsEof())
            {
                return NGIN::Utilities::Expected<std::string_view, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::UnexpectedEnd, "Unterminated attribute")));
            }
            const char* end = ctx.cursor.CurrentPtr();
            ctx.cursor.Advance();
            std::string_view raw(start, static_cast<UIntSize>(end - start));
            auto             decoded = DecodeEntities(ctx, raw);
            if (!decoded.HasValue())
                return decoded;
            return decoded;
        }

        NGIN::Utilities::Expected<std::string_view, ParseError> ParseText(XmlParseContext& ctx)
        {
            const char* start = ctx.cursor.CurrentPtr();
            while (!ctx.cursor.IsEof() && ctx.cursor.Peek() != '<')
                ctx.cursor.Advance();
            const char*      end = ctx.cursor.CurrentPtr();
            std::string_view raw(start, static_cast<UIntSize>(end - start));
            if (raw.empty())
                return NGIN::Utilities::Expected<std::string_view, ParseError>(raw);
            auto decoded = DecodeEntities(ctx, raw);
            if (!decoded.HasValue())
                return decoded;
            return NormalizeWhitespace(ctx, decoded.ValueUnsafe());
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

            void* memory = ctx.arena->Allocate(sizeof(XmlElement), alignof(XmlElement));
            if (!memory)
            {
                return NGIN::Utilities::Expected<XmlElement*, ParseError>(NGIN::Utilities::Unexpected<ParseError>(
                        MakeError(ctx, ParseErrorCode::OutOfMemory, "Element allocation failed")));
            }
            auto* element = new (memory) XmlElement(allocator);
            element->name = nameResult.ValueUnsafe();

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

                element->attributes.PushBack(XmlAttribute {attrNameResult.ValueUnsafe(), attrValueResult.ValueUnsafe()});
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

    const XmlAttribute* XmlElement::FindAttribute(std::string_view key) const noexcept
    {
        for (UIntSize i = 0; i < attributes.Size(); ++i)
        {
            if (attributes[i].name == key)
                return &attributes[i];
        }
        return nullptr;
    }

    NGIN::Utilities::Expected<XmlDocument, ParseError>
    XmlParser::Parse(std::span<const NGIN::Byte> input, const XmlParseOptions& options)
    {
        const UIntSize arenaBytes = options.arenaBytes != 0 ? options.arenaBytes : (input.size() * 2 + 4096);
        XmlDocument    document(arenaBytes);

        XmlParseContext ctx {
                InputCursor(input, options.trackLocation),
                options,
                &document.Arena(),
                0,
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
        auto result = Parse(std::span<const NGIN::Byte>(buffer.data(), buffer.Size()), options);
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
                0,
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

}// namespace NGIN::Serialization
