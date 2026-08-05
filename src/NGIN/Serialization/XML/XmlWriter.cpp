#include <NGIN/Serialization/XML/XmlWriter.hpp>

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <new>

namespace NGIN::Serialization::XML
{
    namespace
    {
        struct Context
        {
            std::string  output;
            WriteOptions options;
        };

        template<class T>
        [[nodiscard]] NGIN::Utilities::Expected<T, WriteDiagnostic>
        Failure(WriteErrorCode code, std::string_view message)
        {
            return NGIN::Utilities::Unexpected<WriteDiagnostic>(
                    WriteDiagnostic {.code = code, .message = NGIN::Text::String {message}});
        }

        [[nodiscard]] bool Append(Context& context, std::string_view value)
        {
            if (context.output.size() > context.options.maxOutputBytes ||
                value.size() > context.options.maxOutputBytes - context.output.size())
                return false;
            context.output.append(value);
            return true;
        }

        [[nodiscard]] bool Indent(Context& context, UIntSize depth)
        {
            if (!context.options.pretty)
                return true;
            if (!Append(context, "\n") ||
                (context.options.indentWidth != 0 &&
                 depth > context.options.maxOutputBytes / context.options.indentWidth))
                return false;
            const UIntSize count = depth * context.options.indentWidth;
            if (count > context.options.maxOutputBytes - context.output.size())
                return false;
            context.output.append(count, ' ');
            return true;
        }

        [[nodiscard]] bool Escape(Context& context, std::string_view value, bool attribute)
        {
            if (!NGIN::Text::Unicode::IsValidUtf8(value))
                return false;
            UIntSize run = 0;
            for (UIntSize index = 0; index < value.size(); ++index)
            {
                std::string_view replacement;
                switch (value[index])
                {
                    case '&':
                        replacement = "&amp;";
                        break;
                    case '<':
                        replacement = "&lt;";
                        break;
                    case '>':
                        replacement = "&gt;";
                        break;
                    case '"':
                        if (attribute)
                            replacement = "&quot;";
                        break;
                    case '\'':
                        if (attribute)
                            replacement = "&apos;";
                        break;
                    default:
                        break;
                }
                if (!replacement.empty())
                {
                    if (!Append(context, value.substr(run, index - run)) || !Append(context, replacement))
                        return false;
                    run = index + 1;
                }
            }
            return Append(context, value.substr(run));
        }

        [[nodiscard]] bool HasTextChildren(ElementView element)
        {
            for (const auto child: element.Children())
            {
                if (child.Kind() == NodeKind::Text || child.Kind() == NodeKind::CData)
                    return true;
            }
            return false;
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        WriteElement(Context& context, ElementView element, UIntSize depth)
        {
            if (!element.IsValid())
                return Failure<void>(WriteErrorCode::InvalidNode, "Cannot write an invalid XML element");
            if (depth > context.options.maxDepth)
                return Failure<void>(WriteErrorCode::DepthExceeded, "XML writer depth limit exceeded");

            if (!Append(context, "<") || !Append(context, element.Name()))
                return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
            for (const auto attribute: element.Attributes())
            {
                if (!Append(context, " ") || !Append(context, attribute.Name()) ||
                    !Append(context, "=\"") || !Escape(context, attribute.Value(), true) ||
                    !Append(context, "\""))
                    return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
            }

            const auto children = element.Children();
            if (children.Empty())
            {
                if (!Append(context, "/>"))
                    return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                return {};
            }
            if (!Append(context, ">"))
                return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");

            const bool mixed = HasTextChildren(element);
            for (const auto child: children)
            {
                if (context.options.pretty && !mixed && !Indent(context, depth + 1))
                    return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                switch (child.Kind())
                {
                    case NodeKind::Element: {
                        auto result = WriteElement(context, *child.TryElement(), depth + 1);
                        if (!result)
                            return result;
                        break;
                    }
                    case NodeKind::Text:
                        if (!Escape(context, *child.TryText(), false))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                        break;
                    case NodeKind::CData: {
                        if (!Append(context, "<![CDATA["))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                        auto     value = *child.TryText();
                        UIntSize begin = 0;
                        while (true)
                        {
                            const UIntSize close = value.find("]]>", begin);
                            if (close == std::string_view::npos)
                                break;
                            if (!Append(context, value.substr(begin, close - begin)) ||
                                !Append(context, "]]]]><![CDATA[>"))
                                return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                            begin = close + 3;
                        }
                        if (!Append(context, value.substr(begin)) || !Append(context, "]]>"))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                        break;
                    }
                    case NodeKind::Comment: {
                        const auto value = *child.TryText();
                        if (value.find("--") != std::string_view::npos ||
                            (!value.empty() && value.back() == '-'))
                            return Failure<void>(WriteErrorCode::InvalidComment, "XML comment content is invalid");
                        if (!Append(context, "<!--") || !Append(context, value) || !Append(context, "-->"))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                        break;
                    }
                    case NodeKind::ProcessingInstruction: {
                        const auto value = *child.TryText();
                        if (child.Name().empty() || value.find("?>") != std::string_view::npos)
                            return Failure<void>(WriteErrorCode::InvalidProcessingInstruction,
                                                 "XML processing instruction is invalid");
                        if (!Append(context, "<?") || !Append(context, child.Name()) ||
                            !Append(context, value) || !Append(context, "?>"))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
                        break;
                    }
                }
            }
            if (context.options.pretty && !mixed && !Indent(context, depth))
                return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
            if (!Append(context, "</") || !Append(context, element.Name()) || !Append(context, ">"))
                return Failure<void>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
            return {};
        }
    }// namespace

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::Write(ElementView root, const WriteOptions& options)
    {
        try
        {
            Context context {.output = {}, .options = options};
            if (options.includeDeclaration && !Append(context, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"))
                return Failure<std::string>(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
            auto result = WriteElement(context, root, 0);
            if (!result)
                return NGIN::Utilities::Unexpected<WriteDiagnostic>(std::move(result.Error()));
            return std::move(context.output);
        } catch (const std::bad_alloc&)
        {
            return Failure<std::string>(WriteErrorCode::OutOfMemory, "XML writer allocation failed");
        }
    }

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::Write(const SyntaxDocument& document)
    {
        if (!document.IsValid())
            return Failure<std::string>(WriteErrorCode::InvalidDocument, "Cannot write an invalid XML syntax document");
        try
        {
            return std::string {document.SourceText()};
        } catch (const std::bad_alloc&)
        {
            return Failure<std::string>(WriteErrorCode::OutOfMemory, "XML writer allocation failed");
        }
    }

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::EscapeAttribute(std::string_view value)
    {
        try
        {
            Context context;
            if (!Escape(context, value, true))
                return Failure<std::string>(
                        NGIN::Text::Unicode::IsValidUtf8(value)
                                ? WriteErrorCode::OutputLimitExceeded
                                : WriteErrorCode::InvalidNode,
                        "Failed to encode XML attribute");
            return std::move(context.output);
        } catch (const std::bad_alloc&)
        {
            return Failure<std::string>(WriteErrorCode::OutOfMemory,
                                        "XML attribute allocation failed");
        }
    }
}// namespace NGIN::Serialization::XML
