#include <NGIN/Serialization/JSON/JsonWriter.hpp>

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>
#include <vector>

namespace NGIN::Serialization::JSON
{
    namespace
    {
        struct WriteContext
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

        [[nodiscard]] bool Append(WriteContext& context, std::string_view value)
        {
            if (context.output.size() > context.options.maxOutputBytes ||
                value.size() > context.options.maxOutputBytes - context.output.size())
                return false;
            context.output.append(value);
            return true;
        }

        [[nodiscard]] bool AppendIndent(WriteContext& context, UIntSize depth)
        {
            if (!context.options.pretty)
                return true;
            if (!Append(context, "\n"))
                return false;
            if (context.options.indentWidth != 0 &&
                depth > context.options.maxOutputBytes / context.options.indentWidth)
                return false;
            const UIntSize count = depth * context.options.indentWidth;
            if (count > context.options.maxOutputBytes - context.output.size())
                return false;
            context.output.append(count, ' ');
            return true;
        }

        [[nodiscard]] bool AppendEscaped(WriteContext& context, std::string_view value)
        {
            if (!NGIN::Text::Unicode::IsValidUtf8(value))
                return false;
            static constexpr char HEX[] = "0123456789abcdef";
            if (!Append(context, "\""))
                return false;
            UIntSize runStart = 0;
            for (UIntSize index = 0; index < value.size(); ++index)
            {
                const auto       byte = static_cast<unsigned char>(value[index]);
                std::string_view escape;
                switch (value[index])
                {
                    case '"':
                        escape = "\\\"";
                        break;
                    case '\\':
                        escape = "\\\\";
                        break;
                    case '\b':
                        escape = "\\b";
                        break;
                    case '\f':
                        escape = "\\f";
                        break;
                    case '\n':
                        escape = "\\n";
                        break;
                    case '\r':
                        escape = "\\r";
                        break;
                    case '\t':
                        escape = "\\t";
                        break;
                    default:
                        break;
                }
                if (!escape.empty() || byte < 0x20)
                {
                    if (!Append(context, value.substr(runStart, index - runStart)))
                        return false;
                    if (!escape.empty())
                    {
                        if (!Append(context, escape))
                            return false;
                    }
                    else
                    {
                        char encoded[] = {'\\', 'u', '0', '0', HEX[byte >> 4], HEX[byte & 0x0f]};
                        if (!Append(context, std::string_view {encoded, sizeof(encoded)}))
                            return false;
                    }
                    runStart = index + 1;
                }
            }
            return Append(context, value.substr(runStart)) && Append(context, "\"");
        }

        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        WriteValue(WriteContext& context, ValueView value, UIntSize depth)
        {
            if (!value.IsValid())
                return Failure<void>(WriteErrorCode::InvalidValue, "Cannot write an invalid JSON value");
            if (depth > context.options.maxDepth)
                return Failure<void>(WriteErrorCode::DepthExceeded, "JSON writer depth limit exceeded");

            char number[64] {};
            switch (value.Kind())
            {
                case ValueKind::Null:
                    if (!Append(context, "null"))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                case ValueKind::Bool:
                    if (!Append(context, *value.TryBool() ? "true" : "false"))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                case ValueKind::Int64: {
                    const auto result = std::to_chars(number, number + sizeof(number), *value.TryInt64());
                    if (!Append(context, {number, static_cast<UIntSize>(result.ptr - number)}))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                }
                case ValueKind::UInt64: {
                    const auto result = std::to_chars(number, number + sizeof(number), *value.TryUInt64());
                    if (!Append(context, {number, static_cast<UIntSize>(result.ptr - number)}))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                }
                case ValueKind::Double: {
                    const F64 numberValue = *value.TryDouble();
                    if (!std::isfinite(numberValue))
                        return Failure<void>(WriteErrorCode::NonFiniteNumber, "JSON cannot represent a non-finite number");
                    const auto result = std::to_chars(
                            number, number + sizeof(number), numberValue, std::chars_format::general);
                    if (result.ec != std::errc {} ||
                        !Append(context, {number, static_cast<UIntSize>(result.ptr - number)}))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                }
                case ValueKind::String:
                    if (!AppendEscaped(context, *value.TryString()))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                case ValueKind::Array: {
                    const auto array = *value.TryArray();
                    if (!Append(context, "["))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    for (UIntSize index = 0; index < array.Size(); ++index)
                    {
                        if (index != 0 && !Append(context, ","))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                        if (!AppendIndent(context, depth + 1))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                        auto result = WriteValue(context, array[index], depth + 1);
                        if (!result)
                            return result;
                    }
                    if (!array.Empty() && !AppendIndent(context, depth))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    if (!Append(context, "]"))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                }
                case ValueKind::Object: {
                    const auto              object = *value.TryObject();
                    std::vector<MemberView> members;
                    try
                    {
                        members.reserve(object.Size());
                        for (const MemberView member: object)
                            members.push_back(member);
                        if (context.options.sortObjectKeys)
                        {
                            std::stable_sort(members.begin(), members.end(), [](MemberView left, MemberView right) {
                                return left.Key() < right.Key();
                            });
                        }
                    } catch (const std::bad_alloc&)
                    {
                        return Failure<void>(WriteErrorCode::OutOfMemory, "JSON writer allocation failed");
                    }

                    if (!Append(context, "{"))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    for (UIntSize index = 0; index < members.size(); ++index)
                    {
                        if (index != 0 && !Append(context, ","))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                        if (!AppendIndent(context, depth + 1) ||
                            !AppendEscaped(context, members[index].Key()) ||
                            !Append(context, context.options.pretty ? ": " : ":"))
                            return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                        auto result = WriteValue(context, members[index].Value(), depth + 1);
                        if (!result)
                            return result;
                    }
                    if (!members.empty() && !AppendIndent(context, depth))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    if (!Append(context, "}"))
                        return Failure<void>(WriteErrorCode::OutputLimitExceeded, "JSON output limit exceeded");
                    return {};
                }
            }
            return Failure<void>(WriteErrorCode::InvalidValue, "Unknown JSON value kind");
        }
    }// namespace

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::Write(ValueView value, const WriteOptions& options)
    {
        try
        {
            WriteContext context {.output = {}, .options = options};
            auto         result = WriteValue(context, value, 0);
            if (!result)
                return NGIN::Utilities::Unexpected<WriteDiagnostic>(std::move(result.Error()));
            return std::move(context.output);
        } catch (const std::bad_alloc&)
        {
            return Failure<std::string>(WriteErrorCode::OutOfMemory, "JSON writer allocation failed");
        }
    }

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::WriteCanonical(ValueView value)
    {
        WriteOptions options;
        options.sortObjectKeys = true;
        return Write(value, options);
    }

    NGIN::Utilities::Expected<std::string, WriteDiagnostic>
    Writer::EscapeString(std::string_view value)
    {
        try
        {
            WriteContext context;
            if (!AppendEscaped(context, value))
                return Failure<std::string>(
                        NGIN::Text::Unicode::IsValidUtf8(value)
                                ? WriteErrorCode::OutputLimitExceeded
                                : WriteErrorCode::InvalidValue,
                        "Failed to encode JSON string");
            return std::move(context.output);
        } catch (const std::bad_alloc&)
        {
            return Failure<std::string>(WriteErrorCode::OutOfMemory,
                                        "JSON string allocation failed");
        }
    }
}// namespace NGIN::Serialization::JSON
