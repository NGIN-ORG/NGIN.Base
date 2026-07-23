#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string>

namespace NGIN::Serialization::XML
{
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

    struct WriteDiagnostic
    {
        WriteErrorCode    code {WriteErrorCode::InvalidDocument};
        NGIN::Text::String message {};
    };

    struct WriteOptions
    {
        bool     pretty {false};
        bool     includeDeclaration {false};
        UIntSize indentWidth {2};
        UIntSize maxDepth {256};
        UIntSize maxOutputBytes {64ULL * 1024ULL * 1024ULL};
    };

    class NGIN_BASE_API Writer
    {
    public:
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(ElementView root, const WriteOptions& options = {});

        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const Document& document, const WriteOptions& options = {})
        {
            return Write(document.Root(), options);
        }

        /// @brief Emits a syntax document byte-for-byte.
        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        Write(const SyntaxDocument& document);

        [[nodiscard]] static NGIN::Utilities::Expected<std::string, WriteDiagnostic>
        EscapeAttribute(std::string_view value);
    };
}// namespace NGIN::Serialization::XML
