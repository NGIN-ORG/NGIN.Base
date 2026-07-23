#include <NGIN/Serialization/XML/XmlParser.hpp>

#include "XmlDocumentInternal.hpp"

#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/SourceMap.hpp>
#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <concepts>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace NGIN::Serialization::XML
{
    namespace
    {
        struct ParseContext
        {
            InputCursor               cursor;
            ParseOptions              options;
            detail::DocumentState*    state {nullptr};
            std::vector<SyntaxToken>* syntaxTokens {nullptr};
            UIntSize                  depth {0};
            UIntSize                  decodedBytes {0};
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
            const auto location = SourceMap {context.state->source, context.state->sourceId}.Locate(begin);
            return ParseDiagnostic {
                    .code     = code,
                    .location = ParseLocation {
                            .offset = location.offset,
                            .line   = location.line,
                            .column = location.column,
                    },
                    .span    = SourceSpan {context.state->sourceId, begin, end},
                    .message = NGIN::Text::String {message},
            };
        }

        [[nodiscard]] ParseDiagnostic MakeError(ParseContext&    context,
                                                ParseErrorCode   code,
                                                std::string_view message)
        {
            const UIntSize begin = context.cursor.Offset();
            return MakeErrorAt(context,
                               code,
                               message,
                               begin,
                               (std::min) (begin + 1, context.state->source.size()));
        }

        [[nodiscard]] bool StartsWith(const ParseContext& context, std::string_view value) noexcept
        {
            const auto remaining = context.cursor.Remaining();
            return remaining.size() >= value.size() && remaining.substr(0, value.size()) == value;
        }

        [[nodiscard]] bool IsXmlDeclaration(const ParseContext& context) noexcept
        {
            if (!StartsWith(context, "<?xml"))
                return false;
            const char next = context.cursor.Peek(5);
            return next == ' ' || next == '\t' || next == '\r' || next == '\n';
        }

        void AddSyntax(ParseContext& context, SyntaxKind kind, UIntSize begin, UIntSize end)
        {
            if (context.syntaxTokens)
            {
                context.syntaxTokens->push_back(
                        SyntaxToken {.kind = kind, .span = SourceSpan {context.state->sourceId, begin, end}});
            }
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

        [[nodiscard]] NGIN::Utilities::Expected<detail::StringRef, ParseDiagnostic>
        ParseName(ParseContext& context)
        {
            const UIntSize begin = context.cursor.Offset();
            if (!IsNameStart(static_cast<unsigned char>(context.cursor.Peek())))
                return Failure<detail::StringRef>(
                        MakeError(context, ParseErrorCode::InvalidToken, "Expected an XML name"));
            context.cursor.Advance();
            while (IsNameContinue(static_cast<unsigned char>(context.cursor.Peek())))
                context.cursor.Advance();
            const auto value = context.state->source.substr(begin, context.cursor.Offset() - begin);
            return detail::StringRef {value.data(), value.size()};
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
                const auto decoded = NGIN::Text::Unicode::DecodeUtf8(value, offset);
                if (decoded.error != NGIN::Text::Unicode::EncodingError::None ||
                    !IsXmlCharacter(decoded.codePoint))
                    return false;
                offset += decoded.unitsConsumed;
            }
            return true;
        }

        [[nodiscard]] bool AppendUtf8(std::string& output, UInt32 value)
        {
            if (!IsXmlCharacter(value))
                return false;
            if (value <= 0x7f)
            {
                output.push_back(static_cast<char>(value));
            }
            else if (value <= 0x7ff)
            {
                output.push_back(static_cast<char>(0xc0 | (value >> 6)));
                output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            }
            else if (value <= 0xffff)
            {
                output.push_back(static_cast<char>(0xe0 | (value >> 12)));
                output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0 | (value >> 18)));
                output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            }
            return true;
        }

        [[nodiscard]] NGIN::Utilities::Expected<detail::StringRef, ParseDiagnostic>
        DecodeText(ParseContext&    context,
                   std::string_view raw,
                   UIntSize         sourceOffset,
                   bool             attribute)
        {
            bool needsDecode = raw.find('&') != std::string_view::npos ||
                               raw.find('\r') != std::string_view::npos;
            if (!needsDecode)
                return detail::StringRef {raw.data(), raw.size()};

            std::string decoded;
            try
            {
                decoded.reserve(raw.size());
                for (UIntSize index = 0; index < raw.size();)
                {
                    const char value = raw[index];
                    if (value == '\r')
                    {
                        decoded.push_back('\n');
                        ++index;
                        if (index < raw.size() && raw[index] == '\n')
                            ++index;
                        continue;
                    }
                    if (value != '&')
                    {
                        decoded.push_back(value);
                        ++index;
                        continue;
                    }

                    const UIntSize entityBegin = index;
                    const UIntSize semicolon   = raw.find(';', index + 1);
                    if (semicolon == std::string_view::npos)
                    {
                        return Failure<detail::StringRef>(
                                MakeErrorAt(context,
                                            ParseErrorCode::InvalidEntity,
                                            "Unterminated XML entity reference",
                                            sourceOffset + entityBegin,
                                            sourceOffset + raw.size()));
                    }
                    const auto entity = raw.substr(index + 1, semicolon - index - 1);
                    if (entity == "amp")
                        decoded.push_back('&');
                    else if (entity == "lt")
                        decoded.push_back('<');
                    else if (entity == "gt")
                        decoded.push_back('>');
                    else if (entity == "apos")
                        decoded.push_back('\'');
                    else if (entity == "quot")
                        decoded.push_back('"');
                    else if (!entity.empty() && entity.front() == '#')
                    {
                        UInt32     codePoint = 0;
                        const bool hex       = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                        const auto digits    = entity.substr(hex ? 2 : 1);
                        if (digits.empty())
                        {
                            return Failure<detail::StringRef>(
                                    MakeErrorAt(context,
                                                ParseErrorCode::InvalidEntity,
                                                "Empty XML numeric character reference",
                                                sourceOffset + entityBegin,
                                                sourceOffset + semicolon + 1));
                        }
                        const auto converted = std::from_chars(
                                digits.data(), digits.data() + digits.size(), codePoint, hex ? 16 : 10);
                        if (converted.ec != std::errc {} ||
                            converted.ptr != digits.data() + digits.size() ||
                            !AppendUtf8(decoded, codePoint))
                        {
                            return Failure<detail::StringRef>(
                                    MakeErrorAt(context,
                                                ParseErrorCode::InvalidEntity,
                                                "Invalid XML numeric character reference",
                                                sourceOffset + entityBegin,
                                                sourceOffset + semicolon + 1));
                        }
                    }
                    else
                    {
                        return Failure<detail::StringRef>(
                                MakeErrorAt(context,
                                            ParseErrorCode::InvalidEntity,
                                            "Unknown XML entity reference",
                                            sourceOffset + entityBegin,
                                            sourceOffset + semicolon + 1));
                    }
                    index = semicolon + 1;
                }
            } catch (const std::bad_alloc&)
            {
                return Failure<detail::StringRef>(
                        MakeErrorAt(context,
                                    ParseErrorCode::OutOfMemory,
                                    "XML entity decoding allocation failed",
                                    sourceOffset,
                                    sourceOffset + raw.size()));
            }

            if (context.decodedBytes > context.state->limits.maxDecodedStringBytes ||
                decoded.size() > context.state->limits.maxDecodedStringBytes - context.decodedBytes)
            {
                return Failure<detail::StringRef>(
                        MakeErrorAt(context,
                                    ParseErrorCode::LimitExceeded,
                                    "Decoded XML text limit exceeded",
                                    sourceOffset,
                                    sourceOffset + raw.size()));
            }
            const auto copy = context.state->arena.CopyString(decoded);
            if (!decoded.empty() && copy.data() == nullptr)
            {
                return Failure<detail::StringRef>(
                        MakeErrorAt(context,
                                    ParseErrorCode::OutOfMemory,
                                    attribute ? "XML attribute value allocation failed" : "XML text allocation failed",
                                    sourceOffset,
                                    sourceOffset + raw.size()));
            }
            context.decodedBytes += decoded.size();
            return detail::StringRef {copy.data(), copy.size()};
        }

        [[nodiscard]] bool IsWhitespaceOnly(std::string_view value) noexcept
        {
            return value.find_first_not_of(" \t\r\n") == std::string_view::npos;
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        AddNode(ParseContext& context, detail::NodeRecord node)
        {
            if (context.state->nodes.size() >= context.state->limits.maxNodes ||
                context.state->nodes.size() >= static_cast<UIntSize>((std::numeric_limits<UInt32>::max)()))
                return Failure<NodeId>(MakeErrorAt(context,
                                                   ParseErrorCode::LimitExceeded,
                                                   "XML node limit exceeded",
                                                   node.span.begin,
                                                   node.span.end));
            try
            {
                const NodeId id {static_cast<UInt32>(context.state->nodes.size())};
                context.state->nodes.push_back(node);
                if (!context.state->WithinMemoryLimit())
                {
                    context.state->nodes.pop_back();
                    return Failure<NodeId>(MakeErrorAt(context,
                                                       ParseErrorCode::LimitExceeded,
                                                       "XML memory limit exceeded",
                                                       node.span.begin,
                                                       node.span.end));
                }
                return id;
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(MakeErrorAt(context,
                                                   ParseErrorCode::OutOfMemory,
                                                   "XML node allocation failed",
                                                   node.span.begin,
                                                   node.span.end));
            }
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseComment(ParseContext& context, std::vector<NodeId>* children)
        {
            const UIntSize start = context.cursor.Offset();
            context.cursor.Advance(4);
            const UIntSize textStart = context.cursor.Offset();
            const auto     remaining = context.cursor.Remaining();
            const UIntSize close     = remaining.find("-->");
            if (close == std::string_view::npos)
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::UnexpectedEnd,
                                                 "Unterminated XML comment",
                                                 start,
                                                 context.state->source.size()));
            const auto text = remaining.substr(0, close);
            if (text.find("--") != std::string_view::npos || (!text.empty() && text.back() == '-'))
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::InvalidToken,
                                                 "XML comments cannot contain '--' or end with '-'",
                                                 textStart,
                                                 textStart + text.size()));
            context.cursor.Advance(close + 3);
            AddSyntax(context, SyntaxKind::Comment, start, context.cursor.Offset());

            if (children && context.options.trivia == TriviaPolicy::Preserve)
            {
                detail::NodeRecord node {
                        .kind = NodeKind::Comment,
                        .span = SourceSpan {context.state->sourceId, start, context.cursor.Offset()},
                        .text = detail::StringRef {text.data(), text.size()},
                };
                auto id = AddNode(context, node);
                if (!id)
                    return Failure<void>(std::move(id.Error()));
                children->push_back(id.Value());
            }
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseProcessingInstruction(ParseContext& context, std::vector<NodeId>* children, bool declaration)
        {
            const UIntSize start = context.cursor.Offset();
            context.cursor.Advance(2);
            auto target = ParseName(context);
            if (!target)
                return Failure<void>(std::move(target.Error()));
            const auto targetText = target.Value().View();
            const bool reservedXml =
                    targetText.size() == 3 &&
                    (targetText[0] == 'x' || targetText[0] == 'X') &&
                    (targetText[1] == 'm' || targetText[1] == 'M') &&
                    (targetText[2] == 'l' || targetText[2] == 'L');
            if ((declaration && targetText != "xml") || (!declaration && reservedXml))
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::InvalidToken,
                                                 "The XML processing-instruction target 'xml' is reserved",
                                                 start,
                                                 context.cursor.Offset()));
            const auto     remaining = context.cursor.Remaining();
            const UIntSize close     = remaining.find("?>");
            if (close == std::string_view::npos)
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::UnexpectedEnd,
                                                 "Unterminated XML processing instruction",
                                                 start,
                                                 context.state->source.size()));
            const UIntSize bodyStart = context.cursor.Offset();
            const auto     body      = remaining.substr(0, close);
            if (declaration &&
                (body.empty() ||
                 (body.front() != ' ' && body.front() != '\t' &&
                  body.front() != '\r' && body.front() != '\n') ||
                 body.find("version") == std::string_view::npos))
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::InvalidToken,
                                                 "XML declaration requires a version pseudo-attribute",
                                                 start,
                                                 context.cursor.Offset() + close + 2));
            context.cursor.Advance(close + 2);
            AddSyntax(context,
                      declaration ? SyntaxKind::XmlDeclaration : SyntaxKind::ProcessingInstruction,
                      start,
                      context.cursor.Offset());

            if (children && context.options.trivia == TriviaPolicy::Preserve)
            {
                detail::NodeRecord node {
                        .kind = NodeKind::ProcessingInstruction,
                        .span = SourceSpan {context.state->sourceId, start, context.cursor.Offset()},
                        .name = target.Value(),
                        .text = detail::StringRef {
                                context.state->source.data() + bodyStart,
                                body.size(),
                        },
                };
                auto id = AddNode(context, node);
                if (!id)
                    return Failure<void>(std::move(id.Error()));
                children->push_back(id.Value());
            }
            return {};
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseDoctype(ParseContext& context)
        {
            const UIntSize start = context.cursor.Offset();
            if (context.options.doctype == DoctypePolicy::Reject)
                return Failure<void>(MakeErrorAt(context,
                                                 ParseErrorCode::UnsupportedConstruct,
                                                 "DOCTYPE is disabled by the XML profile",
                                                 start,
                                                 start + 9));

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
                        const auto declaration = context.state->source.substr(start, context.cursor.Offset() - start);
                        if (declaration.find("SYSTEM") != std::string_view::npos ||
                            declaration.find("PUBLIC") != std::string_view::npos)
                            return Failure<void>(MakeErrorAt(context,
                                                             ParseErrorCode::UnsupportedConstruct,
                                                             "External XML identifiers are not supported",
                                                             start,
                                                             context.cursor.Offset()));
                        AddSyntax(context, SyntaxKind::Doctype, start, context.cursor.Offset());
                        return {};
                    }
                }
            }
            return Failure<void>(MakeErrorAt(context,
                                             ParseErrorCode::UnexpectedEnd,
                                             "Unterminated XML DOCTYPE",
                                             start,
                                             context.state->source.size()));
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, ParseDiagnostic>
        ParseElement(ParseContext& context)
        {
            const UIntSize start = context.cursor.Offset();
            if (context.depth >= context.state->limits.maxDepth)
                return Failure<NodeId>(
                        MakeError(context, ParseErrorCode::DepthExceeded, "XML depth limit exceeded"));
            ++context.depth;
            context.cursor.Advance();

            auto name = ParseName(context);
            if (!name)
            {
                --context.depth;
                return Failure<NodeId>(std::move(name.Error()));
            }

            std::vector<detail::AttributeRecord> attributes;
            bool                                 selfClosing = false;
            try
            {
                while (true)
                {
                    const UIntSize beforeWhitespace = context.cursor.Offset();
                    SkipWhitespace(context);
                    const bool separated = context.cursor.Offset() != beforeWhitespace;
                    if (StartsWith(context, "/>"))
                    {
                        context.cursor.Advance(2);
                        selfClosing = true;
                        break;
                    }
                    if (context.cursor.Peek() == '>')
                    {
                        context.cursor.Advance();
                        break;
                    }
                    if (!separated)
                    {
                        --context.depth;
                        return Failure<NodeId>(MakeError(context,
                                                         ParseErrorCode::InvalidToken,
                                                         "XML attributes must be separated by whitespace"));
                    }
                    if (context.cursor.IsEof())
                    {
                        --context.depth;
                        return Failure<NodeId>(MakeErrorAt(context,
                                                           ParseErrorCode::UnexpectedEnd,
                                                           "Unterminated XML start tag",
                                                           start,
                                                           context.state->source.size()));
                    }

                    const UIntSize attributeStart     = context.cursor.Offset();
                    const UIntSize attributeNameStart = attributeStart;
                    auto           attributeName      = ParseName(context);
                    if (!attributeName)
                    {
                        --context.depth;
                        return Failure<NodeId>(std::move(attributeName.Error()));
                    }
                    const UIntSize attributeNameEnd = context.cursor.Offset();
                    for (const auto& previous: attributes)
                    {
                        if (previous.name.View() == attributeName.Value().View())
                        {
                            --context.depth;
                            auto error    = MakeErrorAt(context,
                                                        ParseErrorCode::DuplicateName,
                                                        "Duplicate XML attribute",
                                                        attributeNameStart,
                                                        attributeNameEnd);
                            error.related = previous.nameSpan;
                            return Failure<NodeId>(std::move(error));
                        }
                    }
                    SkipWhitespace(context);
                    if (context.cursor.Peek() != '=')
                    {
                        --context.depth;
                        return Failure<NodeId>(
                                MakeError(context, ParseErrorCode::InvalidToken, "Expected '=' after XML attribute name"));
                    }
                    context.cursor.Advance();
                    SkipWhitespace(context);
                    const char quote = context.cursor.Peek();
                    if (quote != '\'' && quote != '"')
                    {
                        --context.depth;
                        return Failure<NodeId>(
                                MakeError(context, ParseErrorCode::InvalidToken, "XML attribute values must be quoted"));
                    }
                    context.cursor.Advance();
                    const UIntSize valueStart = context.cursor.Offset();
                    while (!context.cursor.IsEof() && context.cursor.Peek() != quote)
                    {
                        if (context.cursor.Peek() == '<')
                        {
                            --context.depth;
                            return Failure<NodeId>(
                                    MakeError(context, ParseErrorCode::InvalidToken, "'<' is not allowed in XML attributes"));
                        }
                        context.cursor.Advance();
                    }
                    if (context.cursor.IsEof())
                    {
                        --context.depth;
                        return Failure<NodeId>(MakeErrorAt(context,
                                                           ParseErrorCode::UnexpectedEnd,
                                                           "Unterminated XML attribute value",
                                                           valueStart,
                                                           context.state->source.size()));
                    }
                    const UIntSize valueEnd = context.cursor.Offset();
                    const auto     rawValue = context.state->source.substr(valueStart, valueEnd - valueStart);
                    auto           value    = DecodeText(context, rawValue, valueStart, true);
                    if (!value)
                    {
                        --context.depth;
                        return Failure<NodeId>(std::move(value.Error()));
                    }
                    context.cursor.Advance();
                    attributes.push_back(detail::AttributeRecord {
                            .name      = attributeName.Value(),
                            .value     = value.Value(),
                            .span      = SourceSpan {context.state->sourceId, attributeStart, context.cursor.Offset()},
                            .nameSpan  = SourceSpan {context.state->sourceId, attributeNameStart, attributeNameEnd},
                            .valueSpan = SourceSpan {context.state->sourceId, valueStart, valueEnd},
                    });
                }
            } catch (const std::bad_alloc&)
            {
                --context.depth;
                return Failure<NodeId>(MakeErrorAt(context,
                                                   ParseErrorCode::OutOfMemory,
                                                   "XML attribute allocation failed",
                                                   start,
                                                   context.cursor.Offset()));
            }

            AddSyntax(context, SyntaxKind::StartTag, start, context.cursor.Offset());
            std::vector<NodeId> children;
            if (!selfClosing)
            {
                try
                {
                    while (true)
                    {
                        if (context.cursor.IsEof())
                        {
                            --context.depth;
                            return Failure<NodeId>(MakeErrorAt(context,
                                                               ParseErrorCode::UnexpectedEnd,
                                                               "Unterminated XML element",
                                                               start,
                                                               context.state->source.size()));
                        }
                        if (StartsWith(context, "</"))
                        {
                            const UIntSize closeStart = context.cursor.Offset();
                            context.cursor.Advance(2);
                            auto closeName = ParseName(context);
                            if (!closeName)
                            {
                                --context.depth;
                                return Failure<NodeId>(std::move(closeName.Error()));
                            }
                            SkipWhitespace(context);
                            if (context.cursor.Peek() != '>')
                            {
                                --context.depth;
                                return Failure<NodeId>(MakeError(
                                        context, ParseErrorCode::InvalidToken, "Expected '>' after XML end tag"));
                            }
                            context.cursor.Advance();
                            if (closeName.Value().View() != name.Value().View())
                            {
                                --context.depth;
                                return Failure<NodeId>(MakeErrorAt(context,
                                                                   ParseErrorCode::MismatchedTag,
                                                                   "XML end tag does not match start tag",
                                                                   closeStart,
                                                                   context.cursor.Offset()));
                            }
                            AddSyntax(context, SyntaxKind::EndTag, closeStart, context.cursor.Offset());
                            break;
                        }
                        if (StartsWith(context, "<!--"))
                        {
                            auto result = ParseComment(context, &children);
                            if (!result)
                            {
                                --context.depth;
                                return Failure<NodeId>(std::move(result.Error()));
                            }
                            continue;
                        }
                        if (StartsWith(context, "<![CDATA["))
                        {
                            const UIntSize cdataStart = context.cursor.Offset();
                            context.cursor.Advance(9);
                            const UIntSize close = context.cursor.Remaining().find("]]>");
                            if (close == std::string_view::npos)
                            {
                                --context.depth;
                                return Failure<NodeId>(MakeErrorAt(context,
                                                                   ParseErrorCode::UnexpectedEnd,
                                                                   "Unterminated XML CDATA section",
                                                                   cdataStart,
                                                                   context.state->source.size()));
                            }
                            const auto text = context.cursor.Remaining().substr(0, close);
                            context.cursor.Advance(close + 3);
                            AddSyntax(context, SyntaxKind::CData, cdataStart, context.cursor.Offset());
                            auto id = AddNode(context, detail::NodeRecord {
                                                               .kind = NodeKind::CData,
                                                               .span = SourceSpan {
                                                                       context.state->sourceId,
                                                                       cdataStart,
                                                                       context.cursor.Offset(),
                                                               },
                                                               .text = detail::StringRef {text.data(), text.size()},
                                                       });
                            if (!id)
                            {
                                --context.depth;
                                return Failure<NodeId>(std::move(id.Error()));
                            }
                            children.push_back(id.Value());
                            continue;
                        }
                        if (StartsWith(context, "<?"))
                        {
                            auto result = ParseProcessingInstruction(context, &children, false);
                            if (!result)
                            {
                                --context.depth;
                                return Failure<NodeId>(std::move(result.Error()));
                            }
                            continue;
                        }
                        if (StartsWith(context, "<!"))
                        {
                            --context.depth;
                            return Failure<NodeId>(MakeError(context,
                                                             ParseErrorCode::UnsupportedConstruct,
                                                             "Unsupported XML declaration inside element"));
                        }
                        if (context.cursor.Peek() == '<')
                        {
                            auto child = ParseElement(context);
                            if (!child)
                            {
                                --context.depth;
                                return Failure<NodeId>(std::move(child.Error()));
                            }
                            children.push_back(child.Value());
                            continue;
                        }

                        const UIntSize textStart = context.cursor.Offset();
                        while (!context.cursor.IsEof() && context.cursor.Peek() != '<')
                            context.cursor.Advance();
                        const UIntSize textEnd = context.cursor.Offset();
                        const auto     rawText = context.state->source.substr(textStart, textEnd - textStart);
                        if (rawText.find("]]>") != std::string_view::npos)
                        {
                            --context.depth;
                            return Failure<NodeId>(MakeErrorAt(context,
                                                               ParseErrorCode::InvalidToken,
                                                               "']]>' is not allowed in XML character data",
                                                               textStart,
                                                               textEnd));
                        }
                        AddSyntax(context, SyntaxKind::Text, textStart, textEnd);
                        if (context.options.trivia == TriviaPolicy::Discard && IsWhitespaceOnly(rawText))
                            continue;
                        auto text = DecodeText(context, rawText, textStart, false);
                        if (!text)
                        {
                            --context.depth;
                            return Failure<NodeId>(std::move(text.Error()));
                        }
                        auto id = AddNode(context, detail::NodeRecord {
                                                           .kind = NodeKind::Text,
                                                           .span = SourceSpan {context.state->sourceId, textStart, textEnd},
                                                           .text = text.Value(),
                                                   });
                        if (!id)
                        {
                            --context.depth;
                            return Failure<NodeId>(std::move(id.Error()));
                        }
                        children.push_back(id.Value());
                    }
                } catch (const std::bad_alloc&)
                {
                    --context.depth;
                    return Failure<NodeId>(MakeErrorAt(context,
                                                       ParseErrorCode::OutOfMemory,
                                                       "XML child allocation failed",
                                                       start,
                                                       context.cursor.Offset()));
                }
            }

            --context.depth;
            if (context.state->attributes.size() > context.state->limits.maxMembers ||
                attributes.size() > context.state->limits.maxMembers - context.state->attributes.size() ||
                context.state->children.size() > context.state->limits.maxMembers ||
                children.size() > context.state->limits.maxMembers - context.state->children.size())
                return Failure<NodeId>(MakeErrorAt(context,
                                                   ParseErrorCode::LimitExceeded,
                                                   "XML member limit exceeded",
                                                   start,
                                                   context.cursor.Offset()));

            const UIntSize attributeBegin = context.state->attributes.size();
            const UIntSize childBegin     = context.state->children.size();
            const UIntSize elementIndex   = context.state->elements.size();
            try
            {
                context.state->attributes.insert(
                        context.state->attributes.end(), attributes.begin(), attributes.end());
                context.state->children.insert(
                        context.state->children.end(), children.begin(), children.end());
                context.state->elements.push_back(detail::ElementRecord {
                        .name       = name.Value(),
                        .attributes = detail::Range {attributeBegin, attributes.size()},
                        .children   = detail::Range {childBegin, children.size()},
                        .span       = SourceSpan {context.state->sourceId, start, context.cursor.Offset()},
                });
            } catch (const std::bad_alloc&)
            {
                context.state->attributes.resize(attributeBegin);
                context.state->children.resize(childBegin);
                return Failure<NodeId>(MakeErrorAt(context,
                                                   ParseErrorCode::OutOfMemory,
                                                   "XML element allocation failed",
                                                   start,
                                                   context.cursor.Offset()));
            }

            auto node = AddNode(context, detail::NodeRecord {
                                                 .kind = NodeKind::Element,
                                                 .span = SourceSpan {
                                                         context.state->sourceId,
                                                         start,
                                                         context.cursor.Offset(),
                                                 },
                                                 .element = elementIndex,
                                         });
            if (!node)
            {
                context.state->elements.pop_back();
                context.state->attributes.resize(attributeBegin);
                context.state->children.resize(childBegin);
            }
            return node;
        }

        template<class DocumentType>
        [[nodiscard]] NGIN::Utilities::Expected<DocumentType, ParseDiagnostic>
        ParseState(std::unique_ptr<detail::DocumentState> state,
                   const ParseOptions&                    options,
                   std::vector<SyntaxToken>*              syntaxTokens = nullptr)
        {
            ParseContext context {
                    .cursor       = InputCursor {state->source},
                    .options      = options,
                    .state        = state.get(),
                    .syntaxTokens = syntaxTokens,
            };
            if (state->source.size() > state->limits.maxInputBytes)
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::LimitExceeded,
                                                         "XML input byte limit exceeded",
                                                         0,
                                                         state->source.size()));
            if (!NGIN::Text::Unicode::IsValidUtf8(state->source))
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::InvalidEncoding,
                                                         "XML input is not valid UTF-8",
                                                         0,
                                                         state->source.size()));
            if (!ContainsOnlyXmlCharacters(state->source))
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::InvalidEncoding,
                                                         "XML input contains a character forbidden by XML 1.0",
                                                         0,
                                                         state->source.size()));

            if (StartsWith(context, "\xef\xbb\xbf"))
                context.cursor.Advance(3);
            if (IsXmlDeclaration(context))
            {
                auto declaration = ParseProcessingInstruction(context, nullptr, true);
                if (!declaration)
                    return Failure<DocumentType>(std::move(declaration.Error()));
            }

            bool sawDoctype = false;
            while (true)
            {
                SkipWhitespace(context);
                if (StartsWith(context, "<!--"))
                {
                    auto comment = ParseComment(context, nullptr);
                    if (!comment)
                        return Failure<DocumentType>(std::move(comment.Error()));
                }
                else if (StartsWith(context, "<?"))
                {
                    auto instruction = ParseProcessingInstruction(context, nullptr, false);
                    if (!instruction)
                        return Failure<DocumentType>(std::move(instruction.Error()));
                }
                else if (StartsWith(context, "<!DOCTYPE"))
                {
                    if (sawDoctype)
                        return Failure<DocumentType>(MakeError(context,
                                                               ParseErrorCode::InvalidDocumentStructure,
                                                               "XML document contains multiple DOCTYPE declarations"));
                    sawDoctype   = true;
                    auto doctype = ParseDoctype(context);
                    if (!doctype)
                        return Failure<DocumentType>(std::move(doctype.Error()));
                }
                else
                {
                    break;
                }
            }

            if (context.cursor.Peek() != '<' || StartsWith(context, "</"))
                return Failure<DocumentType>(
                        MakeError(context, ParseErrorCode::InvalidDocumentStructure, "XML document requires one root element"));
            auto root = ParseElement(context);
            if (!root)
                return Failure<DocumentType>(std::move(root.Error()));
            state->root = root.Value();

            while (true)
            {
                SkipWhitespace(context);
                if (StartsWith(context, "<!--"))
                {
                    auto comment = ParseComment(context, nullptr);
                    if (!comment)
                        return Failure<DocumentType>(std::move(comment.Error()));
                }
                else if (StartsWith(context, "<?"))
                {
                    auto instruction = ParseProcessingInstruction(context, nullptr, false);
                    if (!instruction)
                        return Failure<DocumentType>(std::move(instruction.Error()));
                }
                else
                {
                    break;
                }
            }
            SkipWhitespace(context);
            if (!context.cursor.IsEof())
                return Failure<DocumentType>(MakeError(context,
                                                       ParseErrorCode::InvalidDocumentStructure,
                                                       "Content is not allowed after the XML root element"));

            state->FinalizeViews();
            if (!state->WithinMemoryLimit())
            {
                return Failure<DocumentType>(MakeErrorAt(context,
                                                         ParseErrorCode::LimitExceeded,
                                                         "XML total memory limit exceeded",
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
            return ParseState<Document>(
                    std::make_unique<detail::DocumentState>(
                            std::move(input), limits, resources),
                    options);
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate XML document";
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
            return ParseState<BorrowedDocument>(
                    std::make_unique<detail::DocumentState>(input, limits, resources), options);
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate borrowed XML document";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }
    }

    NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
    Parser::ParseSyntax(OwnedTextBuffer       input,
                        const ParseOptions&   options,
                        const ParseLimits&    limits,
                        const ParseResources& resources)
    {
        try
        {
            auto syntax    = std::make_unique<detail::SyntaxState>();
            syntax->source = std::move(input);
            auto state     = std::make_unique<detail::DocumentState>(
                    syntax->source.Borrow(), limits, resources);
            auto syntaxOptions   = options;
            syntaxOptions.trivia = TriviaPolicy::Preserve;
            auto parsed          = ParseState<Document>(std::move(state), syntaxOptions, &syntax->tokens);
            if (!parsed)
                return Failure<SyntaxDocument>(std::move(parsed.Error()));
            syntax->valid = true;
            return SyntaxDocument {std::move(syntax)};
        } catch (const std::bad_alloc&)
        {
            ParseDiagnostic error;
            error.code    = ParseErrorCode::OutOfMemory;
            error.message = "Failed to allocate XML syntax document";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(error));
        }
    }
}// namespace NGIN::Serialization::XML
