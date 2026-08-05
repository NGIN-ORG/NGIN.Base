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
    class NGIN_SERIALIZATION_API Parser
    {
    public:
        /// @brief Parses owned UTF-8 input into a self-contained semantic document.
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        Parse(OwnedTextBuffer       input,
              const ParseOptions&   options   = {},
              const ParseLimits&    limits    = {},
              const ParseResources& resources = {});

        /// @brief Parses while decoding entities and line endings into the owned input buffer.
        ///
        /// The returned document owns the mutated buffer. Source spans continue to
        /// address the original byte positions, while decoded string views may be
        /// shorter than their source spans.
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        ParseInSitu(MutableTextBuffer     input,
                    const ParseOptions&   options   = {},
                    const ParseLimits&    limits    = {},
                    const ParseResources& resources = {});

        /// @brief Parses caller-owned input using reusable scratch storage.
        /// @note The input and scratch storage must outlive the returned document and its views.
        [[nodiscard]] static NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
        ParseBorrowed(BorrowedTextView      input,
                      ParseScratch&         scratch,
                      const ParseOptions&   options   = {},
                      const ParseLimits&    limits    = {},
                      const ParseResources& resources = {});

        /// @brief Parses owned input into a source-preserving syntax document.
        [[nodiscard]] static NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
        ParseSyntax(OwnedTextBuffer       input,
                    const ParseOptions&   options   = {},
                    const ParseLimits&    limits    = {},
                    const ParseResources& resources = {});
    };

    /// @brief Parses owned UTF-8 input into a self-contained semantic document.
    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parse(OwnedTextBuffer       input,
          const ParseOptions&   options   = {},
          const ParseLimits&    limits    = {},
          const ParseResources& resources = {})
    {
        return Parser::Parse(std::move(input), options, limits, resources);
    }

    /// @brief Parses mutable owned input using in-situ decoding.
    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    ParseInSitu(MutableTextBuffer     input,
                const ParseOptions&   options   = {},
                const ParseLimits&    limits    = {},
                const ParseResources& resources = {})
    {
        return Parser::ParseInSitu(std::move(input), options, limits, resources);
    }

    /// @brief Parses caller-owned input using reusable scratch storage.
    [[nodiscard]] inline NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
    ParseBorrowed(BorrowedTextView      input,
                  ParseScratch&         scratch,
                  const ParseOptions&   options   = {},
                  const ParseLimits&    limits    = {},
                  const ParseResources& resources = {})
    {
        return Parser::ParseBorrowed(input, scratch, options, limits, resources);
    }

    /// @brief Parses owned input into a source-preserving syntax document.
    [[nodiscard]] inline NGIN::Utilities::Expected<SyntaxDocument, ParseDiagnostic>
    ParseSyntax(OwnedTextBuffer       input,
                const ParseOptions&   options   = {},
                const ParseLimits&    limits    = {},
                const ParseResources& resources = {})
    {
        return Parser::ParseSyntax(std::move(input), options, limits, resources);
    }
}// namespace NGIN::Serialization::XML
