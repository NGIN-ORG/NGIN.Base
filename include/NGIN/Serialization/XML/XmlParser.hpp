#pragma once

#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/XML/XmlProfile.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string_view>

namespace NGIN::Serialization::XML
{
    /// @brief Parses UTF-8 text into self-contained documents.
    class NGIN_SERIALIZATION_API Parser
    {
    public:
        /// @brief Parses text into an owning immutable document.
        /// @note Input is needed only during this call; returned views refer to document-owned storage.
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        Parse(std::string_view      input,
              const ParseOptions&   options   = {},
              const ParseLimits&    limits    = {},
              const ParseResources& resources = {});

        /// @brief Parses text into an owning source-preserving syntax document.
        /// @note Input is needed only during this call; returned views refer to document-owned storage.
        [[nodiscard]] static NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
        ParseSyntax(std::string_view      input,
                    const ParseOptions&   options   = {},
                    const ParseLimits&    limits    = {},
                    const ParseResources& resources = {});
    };

    /// @brief Parses text into an owning immutable document.
    /// @note Input may be modified or destroyed after this call; views remain tied to the returned document.
    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parse(std::string_view      input,
          const ParseOptions&   options   = {},
          const ParseLimits&    limits    = {},
          const ParseResources& resources = {})
    {
        return Parser::Parse(input, options, limits, resources);
    }

    /// @brief Parses text into an owning source-preserving syntax document.
    /// @note Input may be modified or destroyed after this call; views remain tied to the returned document.
    [[nodiscard]] inline NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
    ParseSyntax(std::string_view      input,
                const ParseOptions&   options   = {},
                const ParseLimits&    limits    = {},
                const ParseResources& resources = {})
    {
        return Parser::ParseSyntax(input, options, limits, resources);
    }
}// namespace NGIN::Serialization::XML
