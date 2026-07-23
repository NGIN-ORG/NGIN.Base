#include <NGIN/Serialization/JSON/JsonStreamWriter.hpp>

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>

namespace NGIN::Serialization::JSON
{
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
        m_hasRoot      = false;
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
            return Fail(WriteErrorCode::InvalidValue, "JSON writer is in a failed state");
        if (m_bytesWritten > m_options.maxOutputBytes ||
            value.size() > m_options.maxOutputBytes - m_bytesWritten)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
        }
        if (!m_sink.Write(value))
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "JSON output sink rejected bytes");
        }
        m_bytesWritten += value.size();
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::AppendIndent(UIntSize depth)
    {
        if (!m_options.pretty)
            return {};
        auto newline = Append("\n");
        if (!newline)
            return newline;
        if (m_options.indentWidth != 0 &&
            depth > m_options.maxOutputBytes / m_options.indentWidth)
            return Fail(WriteErrorCode::OutputLimitExceeded, "JSON indentation limit exceeded");
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

    NGIN::Utilities::Expected<void, WriteDiagnostic>
    StreamWriter::AppendEscaped(std::string_view value)
    {
        if (!NGIN::Text::Unicode::IsValidUtf8(value))
            return Fail(WriteErrorCode::InvalidValue, "JSON string is not valid UTF-8");
        static constexpr char hex[] = "0123456789abcdef";
        auto result = Append("\"");
        if (!result)
            return result;
        UIntSize runStart = 0;
        for (UIntSize index = 0; index < value.size(); ++index)
        {
            const auto byte = static_cast<unsigned char>(value[index]);
            std::string_view escape;
            switch (value[index])
            {
                case '"': escape = "\\\""; break;
                case '\\': escape = "\\\\"; break;
                case '\b': escape = "\\b"; break;
                case '\f': escape = "\\f"; break;
                case '\n': escape = "\\n"; break;
                case '\r': escape = "\\r"; break;
                case '\t': escape = "\\t"; break;
                default: break;
            }
            if (!escape.empty() || byte < 0x20)
            {
                result = Append(value.substr(runStart, index - runStart));
                if (!result)
                    return result;
                if (!escape.empty())
                    result = Append(escape);
                else
                {
                    const char encoded[] = {'\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 0x0f]};
                    result = Append({encoded, sizeof(encoded)});
                }
                if (!result)
                    return result;
                runStart = index + 1;
            }
        }
        result = Append(value.substr(runStart));
        return result ? Append("\"") : result;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::BeforeValue()
    {
        if (m_stack.empty())
        {
            if (m_hasRoot)
                return Fail(WriteErrorCode::InvalidValue, "JSON output may contain only one root value");
            m_hasRoot = true;
            return {};
        }

        auto& frame = m_stack.back();
        if (frame.kind == Container::Object)
        {
            if (!frame.awaitingValue)
                return Fail(WriteErrorCode::InvalidValue, "JSON object value requires a preceding key");
            frame.awaitingValue = false;
            ++frame.count;
            return {};
        }

        if (frame.count != 0)
        {
            auto comma = Append(",");
            if (!comma)
                return comma;
        }
        auto indent = AppendIndent(m_stack.size());
        if (!indent)
            return indent;
        ++frame.count;
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::CompleteScalar()
    {
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::BeginObject()
    {
        auto before = BeforeValue();
        if (!before)
            return before;
        if (m_stack.size() >= m_options.maxDepth)
            return Fail(WriteErrorCode::DepthExceeded, "JSON writer depth limit exceeded");
        auto open = Append("{");
        if (!open)
            return open;
        try
        {
            m_stack.push_back(Frame {.kind = Container::Object});
        } catch (const std::bad_alloc&)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "JSON writer stack allocation failed");
        }
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::EndObject()
    {
        if (m_stack.empty() || m_stack.back().kind != Container::Object)
            return Fail(WriteErrorCode::InvalidValue, "Mismatched JSON object end");
        const auto frame = m_stack.back();
        if (frame.awaitingValue)
            return Fail(WriteErrorCode::InvalidValue, "JSON object key has no value");
        if (frame.count != 0)
        {
            auto indent = AppendIndent(m_stack.size() - 1);
            if (!indent)
                return indent;
        }
        m_stack.pop_back();
        return Append("}");
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::BeginArray()
    {
        auto before = BeforeValue();
        if (!before)
            return before;
        if (m_stack.size() >= m_options.maxDepth)
            return Fail(WriteErrorCode::DepthExceeded, "JSON writer depth limit exceeded");
        auto open = Append("[");
        if (!open)
            return open;
        try
        {
            m_stack.push_back(Frame {.kind = Container::Array});
        } catch (const std::bad_alloc&)
        {
            m_failed = true;
            return Fail(WriteErrorCode::OutOfMemory, "JSON writer stack allocation failed");
        }
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::EndArray()
    {
        if (m_stack.empty() || m_stack.back().kind != Container::Array)
            return Fail(WriteErrorCode::InvalidValue, "Mismatched JSON array end");
        const auto frame = m_stack.back();
        if (frame.count != 0)
        {
            auto indent = AppendIndent(m_stack.size() - 1);
            if (!indent)
                return indent;
        }
        m_stack.pop_back();
        return Append("]");
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Key(std::string_view value)
    {
        if (m_stack.empty() || m_stack.back().kind != Container::Object ||
            m_stack.back().awaitingValue)
            return Fail(WriteErrorCode::InvalidValue, "JSON key is only valid while expecting an object key");
        auto& frame = m_stack.back();
        if (frame.count != 0)
        {
            auto comma = Append(",");
            if (!comma)
                return comma;
        }
        auto indent = AppendIndent(m_stack.size());
        if (!indent)
            return indent;
        auto key = AppendEscaped(value);
        if (!key)
            return key;
        auto colon = Append(m_options.pretty ? ": " : ":");
        if (!colon)
            return colon;
        frame.awaitingValue = true;
        return {};
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Null()
    {
        auto result = BeforeValue();
        return result ? Append("null") : result;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Bool(bool value)
    {
        auto result = BeforeValue();
        return result ? Append(value ? "true" : "false") : result;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Int64(NGIN::Int64 value)
    {
        auto result = BeforeValue();
        if (!result)
            return result;
        char buffer[32];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return Append({buffer, static_cast<UIntSize>(converted.ptr - buffer)});
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::UInt64(NGIN::UInt64 value)
    {
        auto result = BeforeValue();
        if (!result)
            return result;
        char buffer[32];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return Append({buffer, static_cast<UIntSize>(converted.ptr - buffer)});
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Double(F64 value)
    {
        if (!std::isfinite(value))
            return Fail(WriteErrorCode::NonFiniteNumber, "JSON cannot represent a non-finite number");
        auto result = BeforeValue();
        if (!result)
            return result;
        char buffer[64];
        const auto converted = std::to_chars(
                buffer, buffer + sizeof(buffer), value, std::chars_format::general);
        if (converted.ec != std::errc {})
            return Fail(WriteErrorCode::InvalidValue, "JSON number formatting failed");
        return Append({buffer, static_cast<UIntSize>(converted.ptr - buffer)});
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::String(std::string_view value)
    {
        auto result = BeforeValue();
        return result ? AppendEscaped(value) : result;
    }

    NGIN::Utilities::Expected<void, WriteDiagnostic> StreamWriter::Finish()
    {
        if (m_failed)
            return Fail(WriteErrorCode::InvalidValue, "JSON writer is in a failed state");
        if (!m_hasRoot)
            return Fail(WriteErrorCode::InvalidValue, "JSON output has no root value");
        if (!m_stack.empty())
            return Fail(WriteErrorCode::InvalidValue, "JSON output contains an unclosed container");
        return {};
    }
}// namespace NGIN::Serialization::JSON
