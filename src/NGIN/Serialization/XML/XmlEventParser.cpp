#include <NGIN/Serialization/XML/XmlEventParser.hpp>

#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/SourceMap.hpp>
#include <NGIN/Text/Unicode/Utf8.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

namespace NGIN::Serialization::XML::detail
{
    namespace
    {
        struct ParsedName
        {
            std::string_view value {};
            SourceSpan       span {};
        };

        struct SeenAttribute
        {
            std::string_view value {};
            SourceSpan       span {};
        };

        class SeenAttributes
        {
        public:
            [[nodiscard]] const SeenAttribute* Find(std::string_view name) const noexcept
            {
                for (UIntSize index = 0; index < m_inlineSize; ++index)
                {
                    if (m_inline[index].value == name)
                        return &m_inline[index];
                }
                for (const auto& attribute: m_overflow)
                {
                    if (attribute.value == name)
                        return &attribute;
                }
                return nullptr;
            }

            void Add(SeenAttribute attribute)
            {
                if (m_inlineSize < m_inline.size())
                {
                    m_inline[m_inlineSize++] = attribute;
                    return;
                }
                m_overflow.push_back(attribute);
            }

        private:
            std::array<SeenAttribute, 4> m_inline {};
            UIntSize                     m_inlineSize {0};
            std::vector<SeenAttribute>   m_overflow {};
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
            UIntSize         attributeCount {0};
            UIntSize         childCount {0};
            UIntSize         decodedBytes {0};
        };

        struct DepthGuard
        {
            UIntSize& depth;
            ~DepthGuard() { --depth; }
        };

        template<class T>
        [[nodiscard]] NGIN::Utilities::Expected<T, ParseDiagnostic>
        Failure(ParseDiagnostic error)
        {
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }

        [[nodiscard]] ParseDiagnostic MakeErrorAt(ParseContext&    context,
                                                  ParseErrorCode   code,
                                                  std::string_view message,
                                                  UIntSize         begin,
                                                  UIntSize         end)
        {
            const auto location = SourceMap {context.source, context.sourceId}.Locate(begin);
            return ParseDiagnostic {
                    .code     = code,
                    .location = ParseLocation {
                            .offset = location.offset,
                            .line   = location.line,
                            .column = location.column,
                    },
                    .span    = SourceSpan {context.sourceId, begin, end},
                    .message = NGIN::Text::String {message},
            };
        }

        [[nodiscard]] ParseDiagnostic MakeError(ParseContext&    context,
                                                ParseErrorCode   code,
                                                std::string_view message)
        {
            const UIntSize begin = context.cursor.Offset();
            return MakeErrorAt(
                    context,
                    code,
                    message,
                    begin,
                    (std::min) (begin + 1, context.source.size()));
        }

        [[nodiscard]] bool StartsWith(const ParseContext& context,
                                      std::string_view    value) noexcept
        {
            const auto remaining = context.cursor.Remaining();
            return remaining.size() >= value.size() &&
                   remaining.substr(0, value.size()) == value;
        }

        [[nodiscard]] bool IsXmlDeclaration(const ParseContext& context) noexcept
        {
            if (!StartsWith(context, "<?xml"))
                return false;
            const char next = context.cursor.Peek(5);
            return next == ' ' || next == '\t' || next == '\r' || next == '\n';
        }

        void SkipWhitespace(ParseContext& context) noexcept
        {
            while (true)
            {
                const char value = context.cursor.Peek();
                if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                    return;
                context.cursor.Advance();
            }
        }

        [[nodiscard]] bool IsWhitespaceOnly(std::string_view value) noexcept
        {
            return value.find_first_not_of(" \t\r\n") == std::string_view::npos;
        }

        [[nodiscard]] bool IsNameStart(unsigned char value) noexcept
        {
            return value == ':' || value == '_' ||
                   (value >= 'A' && value <= 'Z') ||
                   (value >= 'a' && value <= 'z') ||
                   value >= 0x80;
        }

        [[nodiscard]] bool IsNameContinue(unsigned char value) noexcept
        {
            return IsNameStart(value) || value == '-' || value == '.' ||
                   (value >= '0' && value <= '9');
        }

        [[nodiscard]] NGIN::Utilities::Expected<ParsedName, ParseDiagnostic>
        ParseName(ParseContext& context)
        {
            const UIntSize begin = context.cursor.Offset();
            if (!IsNameStart(static_cast<unsigned char>(context.cursor.Peek())))
            {
                return Failure<ParsedName>(
                        MakeError(context, ParseErrorCode::InvalidToken, "Expected an XML name"));
            }
            context.cursor.Advance();
            while (IsNameContinue(static_cast<unsigned char>(context.cursor.Peek())))
                context.cursor.Advance();
            return ParsedName {
                    .value = context.source.substr(begin, context.cursor.Offset() - begin),
                    .span  = SourceSpan {context.sourceId, begin, context.cursor.Offset()},
            };
        }

        [[nodiscard]] bool IsXmlCharacter(UInt32 value) noexcept
        {
            return value == 0x09 || value == 0x0a || value == 0x0d ||
                   (value >= 0x20 && value <= 0xd7ff) ||
                   (value >= 0xe000 && value <= 0xfffd) ||
                   (value >= 0x10000 && value <= 0x10ffff);
        }

        [[nodiscard]] bool ContainsOnlyXmlCharacters(std::string_view value) noexcept
        {
            UIntSize offset = 0;
            while (offset < value.size())
            {
                const auto first = static_cast<unsigned char>(value[offset]);
                if (first < 0x80)
                {
                    if (!IsXmlCharacter(first))
                        return false;
                    ++offset;
                    continue;
                }
                const auto decoded = NGIN::Text::Unicode::DecodeUtf8(value, offset);
                if (decoded.error != NGIN::Text::Unicode::EncodingError::None ||
                    !IsXmlCharacter(decoded.codePoint))
                    return false;
                offset += decoded.unitsConsumed;
            }
            return true;
        }

        [[nodiscard]] UIntSize AppendUtf8(char* output, UInt32 value) noexcept
        {
            if (!IsXmlCharacter(value))
                return 0;
            return NGIN::Text::Unicode::EncodeUtf8(
                    static_cast<NGIN::Text::Unicode::CodePoint>(value), output);
        }

        [[nodiscard]] NGIN::Utilities::Expected<std::string_view, ParseDiagnostic>
        DecodeText(ParseContext&    context,
                   std::string_view raw,
                   UIntSize         sourceOffset,
                   bool             attribute)
        {
            const bool needsDecode = raw.find('&') != std::string_view::npos ||
                                     raw.find('\r') != std::string_view::npos;
            if (!needsDecode)
                return raw;

            context.scratch->Reset();
            char* output = context.scratch->TryAllocate(raw.size());
            if (!output)
            {
                return Failure<std::string_view>(MakeErrorAt(
                        context,
                        ParseErrorCode::OutOfMemory,
                        attribute
                                ? "XML event attribute allocation failed"
                                : "XML event text allocation failed",
                        sourceOffset,
                        sourceOffset + raw.size()));
            }

            char* write = output;
            for (UIntSize index = 0; index < raw.size();)
            {
                const char value = raw[index];
                if (value == '\r')
                {
                    *write++ = '\n';
                    ++index;
                    if (index < raw.size() && raw[index] == '\n')
                        ++index;
                    continue;
                }
                if (value != '&')
                {
                    *write++ = value;
                    ++index;
                    continue;
                }

                const UIntSize entityBegin = index;
                const UIntSize semicolon   = raw.find(';', index + 1);
                if (semicolon == std::string_view::npos)
                {
                    return Failure<std::string_view>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidEntity,
                            "Unterminated XML entity reference",
                            sourceOffset + entityBegin,
                            sourceOffset + raw.size()));
                }

                const auto entity = raw.substr(index + 1, semicolon - index - 1);
                if (entity == "amp")
                    *write++ = '&';
                else if (entity == "lt")
                    *write++ = '<';
                else if (entity == "gt")
                    *write++ = '>';
                else if (entity == "apos")
                    *write++ = '\'';
                else if (entity == "quot")
                    *write++ = '"';
                else if (!entity.empty() && entity.front() == '#')
                {
                    UInt32     codePoint = 0;
                    const bool hex =
                            entity.size() > 1 &&
                            (entity[1] == 'x' || entity[1] == 'X');
                    const auto digits = entity.substr(hex ? 2 : 1);
                    if (digits.empty())
                    {
                        return Failure<std::string_view>(MakeErrorAt(
                                context,
                                ParseErrorCode::InvalidEntity,
                                "Empty XML numeric character reference",
                                sourceOffset + entityBegin,
                                sourceOffset + semicolon + 1));
                    }
                    const auto converted = std::from_chars(
                            digits.data(),
                            digits.data() + digits.size(),
                            codePoint,
                            hex ? 16 : 10);
                    const UIntSize written = AppendUtf8(write, codePoint);
                    if (converted.ec != std::errc {} ||
                        converted.ptr != digits.data() + digits.size() ||
                        written == 0)
                    {
                        return Failure<std::string_view>(MakeErrorAt(
                                context,
                                ParseErrorCode::InvalidEntity,
                                "Invalid XML numeric character reference",
                                sourceOffset + entityBegin,
                                sourceOffset + semicolon + 1));
                    }
                    write += written;
                }
                else
                {
                    return Failure<std::string_view>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidEntity,
                            "Unknown XML entity reference",
                            sourceOffset + entityBegin,
                            sourceOffset + semicolon + 1));
                }
                index = semicolon + 1;
            }

            const UIntSize decoded = static_cast<UIntSize>(write - output);
            if (context.decodedBytes > context.limits.maxDecodedStringBytes ||
                decoded > context.limits.maxDecodedStringBytes - context.decodedBytes)
            {
                return Failure<std::string_view>(MakeErrorAt(
                        context,
                        ParseErrorCode::LimitExceeded,
                        "Decoded XML text limit exceeded",
                        sourceOffset,
                        sourceOffset + raw.size()));
            }
            context.decodedBytes += decoded;
            return std::string_view {output, decoded};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        Deliver(ParseContext& context, const Event& event)
        {
            const EventAction action = context.callback(context.handlerContext, event);
            if (action.continueParsing)
                return {};

            ParseDiagnostic diagnostic;
            diagnostic.code            = ParseErrorCode::HandlerRejected;
            diagnostic.span            = event.span;
            diagnostic.location.offset = event.span.begin;
            diagnostic.consumerContext = action.consumerContext;
            diagnostic.message         = "XML event handler stopped parsing";
            return Failure<void>(std::move(diagnostic));
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        CountNode(ParseContext& context, UIntSize begin, UIntSize end)
        {
            if (context.nodeCount >= context.limits.maxNodes ||
                context.nodeCount >= static_cast<UIntSize>((std::numeric_limits<UInt32>::max)()))
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::LimitExceeded,
                        "XML node limit exceeded",
                        begin,
                        end));
            }
            ++context.nodeCount;
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        CountAttribute(ParseContext& context, UIntSize begin, UIntSize end)
        {
            if (context.attributeCount >= context.limits.maxMembers)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::LimitExceeded,
                        "XML attribute limit exceeded",
                        begin,
                        end));
            }
            ++context.attributeCount;
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        CountChild(ParseContext& context, UIntSize begin, UIntSize end)
        {
            if (context.childCount >= context.limits.maxMembers)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::LimitExceeded,
                        "XML child limit exceeded",
                        begin,
                        end));
            }
            ++context.childCount;
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseComment(ParseContext& context, bool insideElement)
        {
            const UIntSize start = context.cursor.Offset();
            context.cursor.Advance(4);
            const UIntSize textStart = context.cursor.Offset();
            const auto     remaining = context.cursor.Remaining();
            const UIntSize close     = remaining.find("-->");
            if (close == std::string_view::npos)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::UnexpectedEnd,
                        "Unterminated XML comment",
                        start,
                        context.source.size()));
            }
            const auto text = remaining.substr(0, close);
            if (text.find("--") != std::string_view::npos ||
                (!text.empty() && text.back() == '-'))
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::InvalidToken,
                        "XML comments cannot contain '--' or end with '-'",
                        textStart,
                        textStart + text.size()));
            }
            context.cursor.Advance(close + 3);

            if (!insideElement || context.options.trivia != TriviaPolicy::Preserve)
                return {};
            auto node = CountNode(context, start, context.cursor.Offset());
            if (!node)
                return node;
            auto child = CountChild(context, start, context.cursor.Offset());
            if (!child)
                return child;
            return Deliver(
                    context,
                    Event {
                            .kind  = EventKind::Comment,
                            .span  = SourceSpan {context.sourceId, start, context.cursor.Offset()},
                            .value = text,
                    });
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseProcessingInstruction(ParseContext& context,
                                   bool          insideElement,
                                   bool          declaration)
        {
            const UIntSize start = context.cursor.Offset();
            context.cursor.Advance(2);
            auto target = ParseName(context);
            if (!target)
                return Failure<void>(std::move(target.Error()));
            const auto targetText = target.Value().value;
            const bool reservedXml =
                    targetText.size() == 3 &&
                    (targetText[0] == 'x' || targetText[0] == 'X') &&
                    (targetText[1] == 'm' || targetText[1] == 'M') &&
                    (targetText[2] == 'l' || targetText[2] == 'L');
            if ((declaration && targetText != "xml") || (!declaration && reservedXml))
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::InvalidToken,
                        "The XML processing-instruction target 'xml' is reserved",
                        start,
                        context.cursor.Offset()));
            }

            const auto     remaining = context.cursor.Remaining();
            const UIntSize close     = remaining.find("?>");
            if (close == std::string_view::npos)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::UnexpectedEnd,
                        "Unterminated XML processing instruction",
                        start,
                        context.source.size()));
            }
            const UIntSize bodyStart = context.cursor.Offset();
            const auto     body      = remaining.substr(0, close);
            if (declaration &&
                (body.empty() ||
                 (body.front() != ' ' && body.front() != '\t' &&
                  body.front() != '\r' && body.front() != '\n') ||
                 body.find("version") == std::string_view::npos))
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::InvalidToken,
                        "XML declaration requires a version pseudo-attribute",
                        start,
                        context.cursor.Offset() + close + 2));
            }
            context.cursor.Advance(close + 2);

            if (!insideElement || declaration ||
                context.options.trivia != TriviaPolicy::Preserve)
                return {};
            auto node = CountNode(context, start, context.cursor.Offset());
            if (!node)
                return node;
            auto child = CountChild(context, start, context.cursor.Offset());
            if (!child)
                return child;
            return Deliver(
                    context,
                    Event {
                            .kind  = EventKind::ProcessingInstruction,
                            .span  = SourceSpan {context.sourceId, start, context.cursor.Offset()},
                            .name  = targetText,
                            .value = context.source.substr(bodyStart, body.size()),
                    });
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseDoctype(ParseContext& context)
        {
            const UIntSize start = context.cursor.Offset();
            if (context.options.doctype == DoctypePolicy::Reject)
            {
                return Failure<void>(MakeErrorAt(
                        context,
                        ParseErrorCode::UnsupportedConstruct,
                        "DOCTYPE is disabled by the XML profile",
                        start,
                        start + 9));
            }

            bool     inSingle    = false;
            bool     inDouble    = false;
            UIntSize subsetDepth = 0;
            while (!context.cursor.IsEof())
            {
                const char value = context.cursor.Peek();
                context.cursor.Advance();
                if (value == '\'' && !inDouble)
                    inSingle = !inSingle;
                else if (value == '"' && !inSingle)
                    inDouble = !inDouble;
                else if (!inSingle && !inDouble)
                {
                    if (value == '[')
                        ++subsetDepth;
                    else if (value == ']' && subsetDepth > 0)
                        --subsetDepth;
                    else if (value == '>' && subsetDepth == 0)
                    {
                        const auto declaration =
                                context.source.substr(start, context.cursor.Offset() - start);
                        if (declaration.find("SYSTEM") != std::string_view::npos ||
                            declaration.find("PUBLIC") != std::string_view::npos)
                        {
                            return Failure<void>(MakeErrorAt(
                                    context,
                                    ParseErrorCode::UnsupportedConstruct,
                                    "External XML identifiers are not supported",
                                    start,
                                    context.cursor.Offset()));
                        }
                        return {};
                    }
                }
            }
            return Failure<void>(MakeErrorAt(
                    context,
                    ParseErrorCode::UnexpectedEnd,
                    "Unterminated XML DOCTYPE",
                    start,
                    context.source.size()));
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseElement(ParseContext& context)
        {
            const UIntSize start = context.cursor.Offset();
            if (context.depth >= context.limits.maxDepth)
            {
                return Failure<void>(
                        MakeError(context, ParseErrorCode::DepthExceeded, "XML depth limit exceeded"));
            }
            auto counted = CountNode(context, start, start + 1);
            if (!counted)
                return counted;

            ++context.depth;
            DepthGuard depthGuard {context.depth};
            context.cursor.Advance();
            auto name = ParseName(context);
            if (!name)
                return Failure<void>(std::move(name.Error()));

            auto started = Deliver(
                    context,
                    Event {
                            .kind = EventKind::StartElement,
                            .span = SourceSpan {
                                    context.sourceId,
                                    start,
                                    context.cursor.Offset(),
                            },
                            .name = name.Value().value,
                    });
            if (!started)
                return started;

            SeenAttributes attributes;
            while (true)
            {
                const UIntSize beforeWhitespace = context.cursor.Offset();
                SkipWhitespace(context);
                const bool separated = context.cursor.Offset() != beforeWhitespace;
                if (StartsWith(context, "/>"))
                {
                    const UIntSize endStart = context.cursor.Offset();
                    context.cursor.Advance(2);
                    auto ended = Deliver(
                            context,
                            Event {
                                    .kind = EventKind::EndElement,
                                    .span = SourceSpan {
                                            context.sourceId,
                                            endStart,
                                            context.cursor.Offset(),
                                    },
                                    .name = name.Value().value,
                            });
                    return ended;
                }
                if (context.cursor.Peek() == '>')
                {
                    context.cursor.Advance();
                    break;
                }
                if (!separated)
                {
                    return Failure<void>(MakeError(
                            context,
                            ParseErrorCode::InvalidToken,
                            "XML attributes must be separated by whitespace"));
                }
                if (context.cursor.IsEof())
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::UnexpectedEnd,
                            "Unterminated XML start tag",
                            start,
                            context.source.size()));
                }

                const UIntSize attributeStart = context.cursor.Offset();
                auto           attributeName  = ParseName(context);
                if (!attributeName)
                    return Failure<void>(std::move(attributeName.Error()));

                const auto* duplicate = attributes.Find(attributeName.Value().value);
                if (duplicate)
                {
                    auto error = MakeErrorAt(
                            context,
                            ParseErrorCode::DuplicateName,
                            "Duplicate XML attribute",
                            attributeName.Value().span.begin,
                            attributeName.Value().span.end);
                    error.related = duplicate->span;
                    return Failure<void>(std::move(error));
                }

                SkipWhitespace(context);
                if (context.cursor.Peek() != '=')
                {
                    return Failure<void>(MakeError(
                            context,
                            ParseErrorCode::InvalidToken,
                            "Expected '=' after XML attribute name"));
                }
                context.cursor.Advance();
                SkipWhitespace(context);
                const char quote = context.cursor.Peek();
                if (quote != '\'' && quote != '"')
                {
                    return Failure<void>(MakeError(
                            context,
                            ParseErrorCode::InvalidToken,
                            "XML attribute values must be quoted"));
                }
                context.cursor.Advance();
                const UIntSize valueStart = context.cursor.Offset();
                while (!context.cursor.IsEof() && context.cursor.Peek() != quote)
                {
                    if (context.cursor.Peek() == '<')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::InvalidToken,
                                "'<' is not allowed in XML attributes"));
                    }
                    context.cursor.Advance();
                }
                if (context.cursor.IsEof())
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::UnexpectedEnd,
                            "Unterminated XML attribute value",
                            valueStart,
                            context.source.size()));
                }

                const UIntSize valueEnd = context.cursor.Offset();
                const auto     rawValue =
                        context.source.substr(valueStart, valueEnd - valueStart);
                auto value = DecodeText(context, rawValue, valueStart, true);
                if (!value)
                    return Failure<void>(std::move(value.Error()));
                context.cursor.Advance();

                auto attributeLimit =
                        CountAttribute(context, attributeStart, context.cursor.Offset());
                if (!attributeLimit)
                    return attributeLimit;
                try
                {
                    attributes.Add(SeenAttribute {
                            .value = attributeName.Value().value,
                            .span  = attributeName.Value().span,
                    });
                } catch (const std::bad_alloc&)
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::OutOfMemory,
                            "XML event attribute tracking allocation failed",
                            attributeStart,
                            context.cursor.Offset()));
                }

                auto delivered = Deliver(
                        context,
                        Event {
                                .kind = EventKind::Attribute,
                                .span = SourceSpan {
                                        context.sourceId,
                                        attributeStart,
                                        context.cursor.Offset(),
                                },
                                .name  = attributeName.Value().value,
                                .value = value.Value(),
                        });
                if (!delivered)
                    return delivered;
            }

            while (true)
            {
                if (context.cursor.IsEof())
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::UnexpectedEnd,
                            "Unterminated XML element",
                            start,
                            context.source.size()));
                }
                if (StartsWith(context, "</"))
                {
                    const UIntSize closeStart = context.cursor.Offset();
                    context.cursor.Advance(2);
                    auto closeName = ParseName(context);
                    if (!closeName)
                        return Failure<void>(std::move(closeName.Error()));
                    SkipWhitespace(context);
                    if (context.cursor.Peek() != '>')
                    {
                        return Failure<void>(MakeError(
                                context,
                                ParseErrorCode::InvalidToken,
                                "Expected '>' after XML end tag"));
                    }
                    context.cursor.Advance();
                    if (closeName.Value().value != name.Value().value)
                    {
                        return Failure<void>(MakeErrorAt(
                                context,
                                ParseErrorCode::MismatchedTag,
                                "XML end tag does not match start tag",
                                closeStart,
                                context.cursor.Offset()));
                    }
                    return Deliver(
                            context,
                            Event {
                                    .kind = EventKind::EndElement,
                                    .span = SourceSpan {
                                            context.sourceId,
                                            closeStart,
                                            context.cursor.Offset(),
                                    },
                                    .name = name.Value().value,
                            });
                }
                if (StartsWith(context, "<!--"))
                {
                    auto comment = ParseComment(context, true);
                    if (!comment)
                        return comment;
                    continue;
                }
                if (StartsWith(context, "<![CDATA["))
                {
                    const UIntSize cdataStart = context.cursor.Offset();
                    context.cursor.Advance(9);
                    const UIntSize close = context.cursor.Remaining().find("]]>");
                    if (close == std::string_view::npos)
                    {
                        return Failure<void>(MakeErrorAt(
                                context,
                                ParseErrorCode::UnexpectedEnd,
                                "Unterminated XML CDATA section",
                                cdataStart,
                                context.source.size()));
                    }
                    const auto text = context.cursor.Remaining().substr(0, close);
                    context.cursor.Advance(close + 3);
                    auto node = CountNode(context, cdataStart, context.cursor.Offset());
                    if (!node)
                        return node;
                    auto child = CountChild(context, cdataStart, context.cursor.Offset());
                    if (!child)
                        return child;
                    auto delivered = Deliver(
                            context,
                            Event {
                                    .kind = EventKind::CData,
                                    .span = SourceSpan {
                                            context.sourceId,
                                            cdataStart,
                                            context.cursor.Offset(),
                                    },
                                    .value = text,
                            });
                    if (!delivered)
                        return delivered;
                    continue;
                }
                if (StartsWith(context, "<?"))
                {
                    auto instruction = ParseProcessingInstruction(context, true, false);
                    if (!instruction)
                        return instruction;
                    continue;
                }
                if (StartsWith(context, "<!"))
                {
                    return Failure<void>(MakeError(
                            context,
                            ParseErrorCode::UnsupportedConstruct,
                            "Unsupported XML declaration inside element"));
                }
                if (context.cursor.Peek() == '<')
                {
                    auto child = CountChild(
                            context, context.cursor.Offset(), context.cursor.Offset() + 1);
                    if (!child)
                        return child;
                    auto element = ParseElement(context);
                    if (!element)
                        return element;
                    continue;
                }

                const UIntSize textStart = context.cursor.Offset();
                while (!context.cursor.IsEof() && context.cursor.Peek() != '<')
                    context.cursor.Advance();
                const UIntSize textEnd = context.cursor.Offset();
                const auto     rawText = context.source.substr(textStart, textEnd - textStart);
                if (rawText.find("]]>") != std::string_view::npos)
                {
                    return Failure<void>(MakeErrorAt(
                            context,
                            ParseErrorCode::InvalidToken,
                            "']]>' is not allowed in XML character data",
                            textStart,
                            textEnd));
                }
                if (context.options.trivia == TriviaPolicy::Discard &&
                    IsWhitespaceOnly(rawText))
                    continue;

                auto text = DecodeText(context, rawText, textStart, false);
                if (!text)
                    return Failure<void>(std::move(text.Error()));
                auto node = CountNode(context, textStart, textEnd);
                if (!node)
                    return node;
                auto child = CountChild(context, textStart, textEnd);
                if (!child)
                    return child;
                auto delivered = Deliver(
                        context,
                        Event {
                                .kind  = EventKind::Text,
                                .span  = SourceSpan {context.sourceId, textStart, textEnd},
                                .value = text.Value(),
                        });
                if (!delivered)
                    return delivered;
            }
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
            return Failure<void>(MakeErrorAt(
                    context,
                    ParseErrorCode::LimitExceeded,
                    "XML input byte limit exceeded",
                    0,
                    source.size()));
        }
        if (!NGIN::Text::Unicode::IsValidUtf8(source))
        {
            return Failure<void>(MakeErrorAt(
                    context,
                    ParseErrorCode::InvalidEncoding,
                    "XML input is not valid UTF-8",
                    0,
                    source.size()));
        }
        if (!ContainsOnlyXmlCharacters(source))
        {
            return Failure<void>(MakeErrorAt(
                    context,
                    ParseErrorCode::InvalidEncoding,
                    "XML input contains a character forbidden by XML 1.0",
                    0,
                    source.size()));
        }

        scratch.Reset();

        if (StartsWith(context, "\xef\xbb\xbf"))
            context.cursor.Advance(3);
        if (IsXmlDeclaration(context))
        {
            auto declaration = ParseProcessingInstruction(context, false, true);
            if (!declaration)
                return declaration;
        }

        bool sawDoctype = false;
        while (true)
        {
            SkipWhitespace(context);
            if (StartsWith(context, "<!--"))
            {
                auto comment = ParseComment(context, false);
                if (!comment)
                    return comment;
            }
            else if (StartsWith(context, "<?"))
            {
                auto instruction = ParseProcessingInstruction(context, false, false);
                if (!instruction)
                    return instruction;
            }
            else if (StartsWith(context, "<!DOCTYPE"))
            {
                if (sawDoctype)
                {
                    return Failure<void>(MakeError(
                            context,
                            ParseErrorCode::InvalidDocumentStructure,
                            "XML document contains multiple DOCTYPE declarations"));
                }
                sawDoctype   = true;
                auto doctype = ParseDoctype(context);
                if (!doctype)
                    return doctype;
            }
            else
            {
                break;
            }
        }

        if (context.cursor.Peek() != '<' || StartsWith(context, "</"))
        {
            return Failure<void>(MakeError(
                    context,
                    ParseErrorCode::InvalidDocumentStructure,
                    "XML document requires one root element"));
        }
        auto root = ParseElement(context);
        if (!root)
            return root;

        while (true)
        {
            SkipWhitespace(context);
            if (StartsWith(context, "<!--"))
            {
                auto comment = ParseComment(context, false);
                if (!comment)
                    return comment;
            }
            else if (StartsWith(context, "<?"))
            {
                auto instruction = ParseProcessingInstruction(context, false, false);
                if (!instruction)
                    return instruction;
            }
            else
            {
                break;
            }
        }
        SkipWhitespace(context);
        if (!context.cursor.IsEof())
        {
            return Failure<void>(MakeError(
                    context,
                    ParseErrorCode::InvalidDocumentStructure,
                    "Content is not allowed after the XML root element"));
        }
        return {};
    }
}// namespace NGIN::Serialization::XML::detail
