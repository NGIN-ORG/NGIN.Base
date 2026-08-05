#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string>

namespace NGIN::Serialization::XML
{
    /// @brief Failure category reported while serializing XML.
    enum class WriteErrorCode : UInt8
    {
        InvalidDocument,
        InvalidNode,
        InvalidComment,
        InvalidProcessingInstruction,
        DepthExceeded,
        OutputLimitExceeded,
        OutOfMemory,
    };

    /// @brief Structured XML serialization failure.
    struct WriteDiagnostic
    {
        WriteErrorCode     code {WriteErrorCode::InvalidDocument};
        NGIN::Text::String message {};
    };

    /// @brief Formatting and resource policy for XML serialization.
    struct WriteOptions
    {
        bool     pretty {false};
        bool     includeDeclaration {false};
        UIntSize indentWidth {2};
        UIntSize maxDepth {256};
        UIntSize maxOutputBytes {64ULL * 1024ULL * 1024ULL};
    };

    /// @brief Serializes semantic or syntax-preserving XML documents.
    class NGIN_SERIALIZATION_API Writer
    {
    public:
        /// @brief Serializes a semantic root element using the requested formatting policy.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(ElementView root, const WriteOptions& options = {});

        /// @brief Serializes the semantic root of a document.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const Document& document, const WriteOptions& options = {})
        {
            return Write(document.Root(), options);
        }

        /// @brief Emits a syntax document byte-for-byte.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const SyntaxDocument& document);

        /// @brief Escapes decoded text for use inside a quoted XML attribute.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        EscapeAttribute(std::string_view value);
    };
}// namespace NGIN::Serialization::XML
