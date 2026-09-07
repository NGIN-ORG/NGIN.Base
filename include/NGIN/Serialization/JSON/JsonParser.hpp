#pragma once

#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string_view>

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
        /// @brief Source identity attached to spans and diagnostics.
        SourceId source {};
    };

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
}// namespace NGIN::Serialization::JSON
