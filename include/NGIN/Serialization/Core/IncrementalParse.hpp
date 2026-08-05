#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>

#include <optional>

namespace NGIN::Serialization
{
    enum class IncrementalParseStatus : UInt8
    {
        NeedMoreInput,
        EventProduced,
        Complete,
        Error,
    };

    struct IncrementalParseResult
    {
        IncrementalParseStatus         status {IncrementalParseStatus::NeedMoreInput};
        UIntSize                       eventsProduced {0};
        std::optional<ParseDiagnostic> diagnostic {};

        [[nodiscard]] bool HasError() const noexcept { return status == IncrementalParseStatus::Error; }
        [[nodiscard]] bool IsComplete() const noexcept { return status == IncrementalParseStatus::Complete; }
    };
}// namespace NGIN::Serialization
