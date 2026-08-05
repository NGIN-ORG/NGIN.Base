#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string>

namespace NGIN::Serialization::JSON
{
    enum class WriteErrorCode : UInt8
    {
        InvalidValue,
        NonFiniteNumber,
        DepthExceeded,
        OutputLimitExceeded,
        OutOfMemory,
    };

    struct WriteDiagnostic
    {
        WriteErrorCode     code {WriteErrorCode::InvalidValue};
        NGIN::Text::String message {};
    };

    struct WriteOptions
    {
        bool     pretty {false};
        bool     sortObjectKeys {false};
        UIntSize indentWidth {2};
        UIntSize maxDepth {256};
        UIntSize maxOutputBytes {64ULL * 1024ULL * 1024ULL};
    };

    /// @brief Serializes immutable JSON views with deterministic escaping.
    class NGIN_SERIALIZATION_API Writer
    {
    public:
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(ValueView value, const WriteOptions& options = {});

        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const Document& document, const WriteOptions& options = {})
        {
            return Write(document.Root(), options);
        }

        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        WriteCanonical(ValueView value);

        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        EscapeString(std::string_view value);
    };
}// namespace NGIN::Serialization::JSON
