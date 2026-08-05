#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>

#include <optional>

namespace NGIN::Serialization
{
    /// @brief State returned by one incremental parser feed operation.
    enum class IncrementalParseStatus : UInt8
    {
        NeedMoreInput,
        EventProduced,
        Complete,
        Error,
    };

    /// @brief Status, event count, and optional diagnostic from one parser feed.
    struct IncrementalParseResult
    {
        IncrementalParseStatus         status {IncrementalParseStatus::NeedMoreInput};
        UIntSize                       eventsProduced {0};
        std::optional<ParseDiagnostic> diagnostic {};

        /// @brief Returns whether parsing stopped with a diagnostic.
        [[nodiscard]] bool HasError() const noexcept { return status == IncrementalParseStatus::Error; }
        /// @brief Returns whether a complete document has been consumed.
        [[nodiscard]] bool IsComplete() const noexcept { return status == IncrementalParseStatus::Complete; }
    };
}// namespace NGIN::Serialization
