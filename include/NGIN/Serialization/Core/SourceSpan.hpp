#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Serialization
{
    /// @brief Stable identifier for a parser input registered by its caller.
    struct SourceId
    {
        UInt32 value {0};

        /// @brief Compares registered source identifiers.
        [[nodiscard]] friend constexpr bool operator==(SourceId, SourceId) noexcept = default;
    };

    /// @brief Half-open byte range within one source.
    struct SourceSpan
    {
        SourceId source {};
        UIntSize begin {0};
        UIntSize end {0};

        /// @brief Returns the range length, or zero for an inverted range.
        [[nodiscard]] constexpr UIntSize Length() const noexcept
        {
            return end >= begin ? end - begin : 0;
        }

        /// @brief Returns whether the range contains no bytes.
        [[nodiscard]] constexpr bool Empty() const noexcept
        {
            return begin == end;
        }

        /// @brief Returns the sentinel span used when no source location is available.
        [[nodiscard]] static constexpr SourceSpan Unknown() noexcept
        {
            return {};
        }

        /// @brief Compares source identity and range boundaries.
        [[nodiscard]] friend constexpr bool operator==(const SourceSpan&, const SourceSpan&) noexcept = default;
    };

    /// @brief Human-readable position derived from a source span.
    struct SourceLocation
    {
        SourceId source {};
        UIntSize offset {0};
        UIntSize line {0};
        UIntSize column {0};
    };
}// namespace NGIN::Serialization
