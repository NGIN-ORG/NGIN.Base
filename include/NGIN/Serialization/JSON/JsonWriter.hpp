#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string>

namespace NGIN::Serialization::JSON
{
    /// @brief Failure category reported while serializing JSON.
    enum class WriteErrorCode : UInt8
    {
        InvalidValue,
        NonFiniteNumber,
        DepthExceeded,
        OutputLimitExceeded,
        OutOfMemory,
    };

    /// @brief Structured JSON serialization failure.
    struct WriteDiagnostic
    {
        WriteErrorCode     code {WriteErrorCode::InvalidValue};
        NGIN::Text::String message {};
    };

    /// @brief Formatting and resource policy for JSON serialization.
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
        /// @brief Serializes a JSON value using the requested formatting policy.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(ValueView value, const WriteOptions& options = {});

        /// @brief Serializes the root value of a document.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const Document& document, const WriteOptions& options = {})
        {
            return Write(document.Root(), options);
        }

        /// @brief Serializes a value in deterministic canonical form.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        WriteCanonical(ValueView value);

        /// @brief Escapes decoded UTF-8 text as JSON string contents without surrounding quotes.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        EscapeString(std::string_view value);
    };
}// namespace NGIN::Serialization::JSON
