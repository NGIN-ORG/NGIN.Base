#pragma once

#include <NGIN/Primitives.hpp>

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
    };
}// namespace NGIN::Serialization::XML
