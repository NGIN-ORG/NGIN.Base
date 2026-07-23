#pragma once

#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/XML/XmlProfile.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <utility>

namespace NGIN::Serialization::XML
{
    /// @brief Strict XML parser with explicit semantic, syntax, and ownership entry points.
    class NGIN_BASE_API Parser
    {
    public:
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        Parse(OwnedTextBuffer input,
              const ParseOptions& options = {},
              const ParseLimits& limits = {},
              const ParseResources& resources = {});

        [[nodiscard]] static NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
        ParseBorrowed(BorrowedTextView input,
                      ParseScratch& scratch,
                      const ParseOptions& options = {},
                      const ParseLimits& limits = {},
                      const ParseResources& resources = {});

        [[nodiscard]] static NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
        ParseSyntax(OwnedTextBuffer input,
                    const ParseOptions& options = {},
                    const ParseLimits& limits = {},
                    const ParseResources& resources = {});
    };

    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parse(OwnedTextBuffer input,
          const ParseOptions& options = {},
          const ParseLimits& limits = {},
          const ParseResources& resources = {})
    {
        return Parser::Parse(std::move(input), options, limits, resources);
    }

    [[nodiscard]] inline NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
    ParseBorrowed(BorrowedTextView input,
                  ParseScratch& scratch,
                  const ParseOptions& options = {},
                  const ParseLimits& limits = {},
                  const ParseResources& resources = {})
    {
        return Parser::ParseBorrowed(input, scratch, options, limits, resources);
    }

    [[nodiscard]] inline NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
    ParseSyntax(OwnedTextBuffer input,
                const ParseOptions& options = {},
                const ParseLimits& limits = {},
                const ParseResources& resources = {})
    {
        return Parser::ParseSyntax(std::move(input), options, limits, resources);
    }
}// namespace NGIN::Serialization::XML
