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
    /// @brief Controls acceptance of non-standard JSON comments.
    enum class CommentPolicy : UInt8
    {
        Reject,
        Allow,
    };

    /// @brief Controls acceptance of trailing commas in arrays and objects.
    enum class TrailingCommaPolicy : UInt8
    {
        Reject,
        Allow,
    };

    /// @brief Controls validation and normalization of duplicate object keys.
    enum class DuplicateKeyPolicy : UInt8
    {
        Reject,
        Preserve,
        KeepFirst,
        KeepLast,
    };

    /// @brief Controls whether parser input is validated as UTF-8.
    enum class Utf8Policy : UInt8
    {
        Validate,
        AssumeValid,
    };

    /// @brief JSON syntax and normalization policy.
    struct ParseOptions
    {
        CommentPolicy       comments {CommentPolicy::Reject};
        TrailingCommaPolicy trailingCommas {TrailingCommaPolicy::Reject};
        DuplicateKeyPolicy  duplicateKeys {DuplicateKeyPolicy::Reject};
        Utf8Policy          utf8 {Utf8Policy::Validate};
    };

    /// @brief JSON parser with explicit source-ownership entry points.
    class NGIN_SERIALIZATION_API Parser
    {
    public:
        /// @brief Parses owned UTF-8 input into a self-contained immutable document.
        [[nodiscard]] static NGIN::Utilities::Expected<Document, ParseDiagnostic>
        Parse(OwnedTextBuffer       input,
              const ParseOptions&   options   = {},
              const ParseLimits&    limits    = {},
              const ParseResources& resources = {});

        /// @brief Parses mutable owned input, permitting in-situ decoding optimizations.
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
    };

    /// @brief Parses owned UTF-8 input into a self-contained immutable document.
    [[nodiscard]] inline NGIN::Utilities::Expected<Document, ParseDiagnostic>
    Parse(OwnedTextBuffer       input,
          const ParseOptions&   options   = {},
          const ParseLimits&    limits    = {},
          const ParseResources& resources = {})
    {
        return Parser::Parse(std::move(input), options, limits, resources);
    }

    /// @brief Parses mutable owned input, permitting in-situ decoding optimizations.
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
}// namespace NGIN::Serialization::JSON
