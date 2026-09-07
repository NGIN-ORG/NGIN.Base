#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>

namespace NGIN::Serialization::XML
{
    enum class TriviaPolicy : UInt8
    {
        Discard,
        Preserve,
    };

    enum class DoctypePolicy : UInt8
    {
        Reject,
        AllowWithoutExternalEntities,
    };

    struct ParseOptions
    {
        TriviaPolicy  trivia {TriviaPolicy::Discard};
        DoctypePolicy doctype {DoctypePolicy::Reject};
        /// @brief Source identity attached to spans and diagnostics.
        SourceId source {};
    };
}// namespace NGIN::Serialization::XML
