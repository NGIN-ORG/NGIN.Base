#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/Core/TextSink.hpp>
#include <NGIN/Serialization/XML/XmlWriter.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Serialization::XML
{
    /// @brief Stateful XML-profile writer for directly-authored documents.
    class NGIN_SERIALIZATION_API StreamWriter
    {
    public:
        /// @brief Binds a non-owning output sink and formatting policy.
        explicit StreamWriter(TextSink sink, const WriteOptions& options = {});

        /// @brief Starts a new document with a replacement sink while retaining stack capacity.
        void Reset(TextSink sink) noexcept;

        /// @brief Begins an element with a validated XML name.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        BeginElement(std::string_view name);
        /// @brief Adds an attribute to the current still-open start tag.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        Attribute(std::string_view name, std::string_view value);
        /// @brief Writes escaped character data inside the current element.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        Text(std::string_view value);
        /// @brief Writes a validated CDATA section inside the current element.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        CData(std::string_view value);
        /// @brief Writes a validated XML comment.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        Comment(std::string_view value);
        /// @brief Writes a validated processing instruction.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic>
        ProcessingInstruction(std::string_view target, std::string_view value);
        /// @brief Ends the current element, using an empty-element form when possible.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> EndElement();
        /// @brief Validates that exactly one complete root element was written.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Finish();

        /// @brief Returns the number of bytes accepted by the sink for the current document.
        [[nodiscard]] UIntSize BytesWritten() const noexcept { return m_bytesWritten; }

    private:
        struct Frame
        {
            std::string              name {};
            std::vector<std::string> attributeNames {};
            bool                     startOpen {true};
            bool                     hasContent {false};
            bool                     hasText {false};
        };

        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Append(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Escape(
                std::string_view value, bool attribute);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Indent(UIntSize depth);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> CloseStart();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeforeMarkup();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Fail(
                WriteErrorCode code, std::string_view message) const;

        TextSink           m_sink {};
        WriteOptions       m_options {};
        std::vector<Frame> m_stack {};
        UIntSize           m_bytesWritten {0};
        UIntSize           m_rootCount {0};
        bool               m_failed {false};
    };
}// namespace NGIN::Serialization::XML
