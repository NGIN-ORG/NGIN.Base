#include <NGIN/Serialization/XML/XmlStreamWriter.hpp>

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <cctype>
#include <new>

namespace NGIN::Serialization::XML
{
    namespace
    {
        [[nodiscard]] bool IsName(std::string_view value) noexcept
        {
            if (value.empty() || !NGIN::Text::Unicode::IsValidUtf8(value))
                return false;
            const auto first = static_cast<unsigned char>(value.front());
            if (!(std::isalpha(first) || value.front() == '_' || value.front() == ':' || first >= 0x80))
                return false;
            for (UIntSize index = 1; index < value.size(); ++index)
            {
                const auto byte = static_cast<unsigned char>(value[index]);
                if (!(std::isalnum(byte) || value[index] == '_' || value[index] == ':' ||
                      value[index] == '-' || value[index] == '.' || byte >= 0x80))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HasInvalidXmlByte(std::string_view value) noexcept
        {
            UIntSize offset = 0;
            while (offset < value.size())
            {
                const auto decoded = NGIN::Text::Unicode::DecodeUtf8(value, offset);
                if (decoded.error != NGIN::Text::Unicode::EncodingError::None)
                    return true;
                const UInt32 codePoint = decoded.codePoint;
                if (!(codePoint == 0x09 || codePoint == 0x0a || codePoint == 0x0d ||
                      (codePoint >= 0x20 && codePoint <= 0xd7ff) ||
                      (codePoint >= 0xe000 && codePoint <= 0xfffd) ||
                      (codePoint >= 0x10000 && codePoint <= 0x10ffff)))
                    return true;
                offset += decoded.unitsConsumed;
            }
            return false;
        }
    }// namespace

    StreamWriter::StreamWriter(TextSink sink, const WriteOptions& options)
        : m_sink(sink), m_options(options)
    {
        m_stack.reserve(16);
    }

    void StreamWriter::Reset(TextSink sink) noexcept
    {
        m_sink         = sink;
        m_stack.clear();
        m_bytesWritten = 0;
        m_rootCount    = 0;
        m_failed       = false;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::Fail(WriteErrorCode code, std::string_view message) const
    {
        return NGIN::Utilities::Unexpected<WriteDiagnostic>(
                WriteDiagnostic {.code = code, .message = NGIN::Text::String {message}});
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Append(std::string_view value)
    {
        if (m_failed)
            return Fail(WriteErrorCode::InvalidDocument, "XML writer is in a failed state");
        if (m_bytesWritten > m_options.maxOutputBytes ||
            value.size() > m_options.maxOutputBytes - m_bytesWritten)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutputLimitExceeded, "XML output limit exceeded");
        }
        if (!m_sink.Write(value))
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "XML output sink rejected bytes");
        }
        m_bytesWritten += value.size();
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::Escape(std::string_view value, bool attribute)
    {
        if (HasInvalidXmlByte(value))
            return Fail(WriteErrorCode::InvalidNode, "XML content contains a forbidden character");
        UIntSize run = 0;
        for (UIntSize index = 0; index < value.size(); ++index)
        {
            std::string_view replacement;
            switch (value[index])
            {
                case '&': replacement = "&amp;"; break;
                case '<': replacement = "&lt;"; break;
                case '>': replacement = "&gt;"; break;
                case '"':
                    if (attribute)
                        replacement = "&quot;";
                    break;
                case '\'':
                    if (attribute)
                        replacement = "&apos;";
                    break;
                default: break;
            }
            if (!replacement.empty())
            {
                auto prefix = Append(value.substr(run, index - run));
                if (!prefix)
                    return prefix;
                auto escaped = Append(replacement);
                if (!escaped)
                    return escaped;
                run = index + 1;
            }
        }
        return Append(value.substr(run));
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Indent(UIntSize depth)
    {
        if (!m_options.pretty)
            return {};
        auto newline = Append("\n");
        if (!newline)
            return newline;
        if (m_options.indentWidth != 0 &&
            depth > m_options.maxOutputBytes / m_options.indentWidth)
            return Fail(WriteErrorCode::OutputLimitExceeded, "XML indentation limit exceeded");
        UIntSize spaces = depth * m_options.indentWidth;
        static constexpr std::string_view block = "                                ";
        while (spaces != 0)
        {
            const UIntSize count = (std::min)(spaces, block.size());
            auto result = Append(block.substr(0, count));
            if (!result)
                return result;
            spaces -= count;
        }
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::CloseStart()
    {
        if (m_stack.empty() || !m_stack.back().startOpen)
            return {};
        auto close = Append(">");
        if (close)
            m_stack.back().startOpen = false;
        return close;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::BeforeMarkup()
    {
        if (m_stack.empty())
            return {};
        auto close = CloseStart();
        if (!close)
            return close;
        auto& parent = m_stack.back();
        if (m_options.pretty && !parent.hasText)
        {
            auto indent = Indent(m_stack.size());
            if (!indent)
                return indent;
        }
        parent.hasContent = true;
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::BeginElement(std::string_view name)
    {
        if (!IsName(name))
            return Fail(WriteErrorCode::InvalidNode, "Invalid XML element name");
        if (m_stack.size() >= m_options.maxDepth)
            return Fail(WriteErrorCode::DepthExceeded, "XML writer depth limit exceeded");
        if (m_stack.empty())
        {
            if (m_rootCount != 0)
                return Fail(WriteErrorCode::InvalidDocument, "XML document may contain only one root element");
            if (m_options.includeDeclaration)
            {
                auto declaration = Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
                if (!declaration)
                    return declaration;
                if (m_options.pretty)
                {
                    auto newline = Append("\n");
                    if (!newline)
                        return newline;
                }
            }
            ++m_rootCount;
        }
        else
        {
            auto before = BeforeMarkup();
            if (!before)
                return before;
        }
        auto open = Append("<");
        if (!open)
            return open;
        auto writtenName = Append(name);
        if (!writtenName)
            return writtenName;
        try
        {
            m_stack.push_back(Frame {.name = std::string {name}});
        } catch (const std::bad_alloc&)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "XML writer stack allocation failed");
        }
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::Attribute(std::string_view name, std::string_view value)
    {
        if (m_stack.empty() || !m_stack.back().startOpen)
            return Fail(WriteErrorCode::InvalidNode, "XML attributes must immediately follow BeginElement");
        if (!IsName(name))
            return Fail(WriteErrorCode::InvalidNode, "Invalid XML attribute name");
        for (const auto& existing: m_stack.back().attributeNames)
        {
            if (existing == name)
                return Fail(WriteErrorCode::InvalidNode, "Duplicate XML attribute");
        }
        try
        {
            m_stack.back().attributeNames.emplace_back(name);
        } catch (const std::bad_alloc&)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "XML writer attribute allocation failed");
        }
        auto space = Append(" ");
        if (!space)
            return space;
        auto writtenName = Append(name);
        if (!writtenName)
            return writtenName;
        auto equals = Append("=\"");
        if (!equals)
            return equals;
        auto escaped = Escape(value, true);
        return escaped ? Append("\"") : escaped;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Text(std::string_view value)
    {
        if (m_stack.empty())
            return Fail(WriteErrorCode::InvalidDocument, "XML text requires an open element");
        auto close = CloseStart();
        if (!close)
            return close;
        auto escaped = Escape(value, false);
        if (escaped)
        {
            m_stack.back().hasContent = true;
            m_stack.back().hasText = true;
        }
        return escaped;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::CData(std::string_view value)
    {
        if (m_stack.empty() || HasInvalidXmlByte(value))
            return Fail(WriteErrorCode::InvalidNode, "Invalid XML CDATA content");
        auto close = CloseStart();
        if (!close)
            return close;
        auto open = Append("<![CDATA[");
        if (!open)
            return open;
        UIntSize begin = 0;
        while (true)
        {
            const UIntSize end = value.find("]]>", begin);
            if (end == std::string_view::npos)
                break;
            auto prefix = Append(value.substr(begin, end - begin));
            if (!prefix)
                return prefix;
            auto split = Append("]]]]><![CDATA[>");
            if (!split)
                return split;
            begin = end + 3;
        }
        auto tail = Append(value.substr(begin));
        if (!tail)
            return tail;
        auto result = Append("]]>");
        if (result)
        {
            m_stack.back().hasContent = true;
            m_stack.back().hasText = true;
        }
        return result;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Comment(std::string_view value)
    {
        if (value.find("--") != std::string_view::npos ||
            (!value.empty() && value.back() == '-'))
            return Fail(WriteErrorCode::InvalidComment, "XML comment content is invalid");
        auto before = BeforeMarkup();
        if (!before)
            return before;
        auto open = Append("<!--");
        if (!open)
            return open;
        auto body = Append(value);
        return body ? Append("-->") : body;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::ProcessingInstruction(std::string_view target, std::string_view value)
    {
        if (!IsName(target) || target == "xml" || target == "XML" ||
            value.find("?>") != std::string_view::npos)
            return Fail(WriteErrorCode::InvalidProcessingInstruction,
                        "XML processing instruction is invalid");
        auto before = BeforeMarkup();
        if (!before)
            return before;
        auto open = Append("<?");
        if (!open)
            return open;
        auto writtenTarget = Append(target);
        if (!writtenTarget)
            return writtenTarget;
        auto body = Append(value);
        return body ? Append("?>") : body;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::EndElement()
    {
        if (m_stack.empty())
            return Fail(WriteErrorCode::InvalidDocument, "No XML element is open");
        Frame frame = std::move(m_stack.back());
        m_stack.pop_back();
        if (frame.startOpen)
            return Append("/>");
        if (m_options.pretty && frame.hasContent && !frame.hasText)
        {
            auto indent = Indent(m_stack.size());
            if (!indent)
                return indent;
        }
        auto open = Append("</");
        if (!open)
            return open;
        auto name = Append(frame.name);
        return name ? Append(">") : name;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Finish()
    {
        if (m_failed)
            return Fail(WriteErrorCode::InvalidDocument, "XML writer is in a failed state");
        if (!m_stack.empty())
            return Fail(WriteErrorCode::InvalidDocument, "XML output contains an unclosed element");
        if (m_rootCount != 1)
            return Fail(WriteErrorCode::InvalidDocument, "XML output requires exactly one root element");
        return {};
    }
}// namespace NGIN::Serialization::XML
