#pragma once

#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <utility>

namespace NGIN::Serialization::JSON
{
    enum class CommentPolicy : UInt8
    {
        Reject,
        Allow,
    };

    enum class TrailingCommaPolicy : UInt8
    {
        Reject,
        Allow,
    };

    enum class DuplicateKeyPolicy : UInt8
    {
        Reject,
        Preserve,
        KeepFirst,
        KeepLast,
    };

    enum class Utf8Policy : UInt8
    {
        Validate,
        AssumeValid,
    };

    struct ParseOptions
    {
        CommentPolicy       comments {CommentPolicy::Reject};
        TrailingCommaPolicy trailingCommas {TrailingCommaPolicy::Reject};
        DuplicateKeyPolicy  duplicateKeys {DuplicateKeyPolicy::Reject};
        Utf8Policy           utf8 {Utf8Policy::Validate};
    };

    /// @brief JSON parser with explicit source-ownership entry points.
    class NGIN_BASE_API Parser
    {
    public:
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        Parse(OwnedTextBuffer input,
              const ParseOptions& options = {},
              const ParseLimits& limits = {},
              const ParseResources& resources = {});

        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        ParseInSitu(MutableTextBuffer input,
                    const ParseOptions& options = {},
                    const ParseLimits& limits = {},
                    const ParseResources& resources = {});

        [[nodiscard]] static NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic>
        ParseBorrowed(BorrowedTextView input,
                      ParseScratch& scratch,
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

    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    ParseInSitu(MutableTextBuffer input,
                const ParseOptions& options = {},
                const ParseLimits& limits = {},
                const ParseResources& resources = {})
    {
        return Parser::ParseInSitu(std::move(input), options, limits, resources);
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
}// namespace NGIN::Serialization::JSON
