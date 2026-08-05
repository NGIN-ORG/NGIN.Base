/// @file SymbolId.hpp
/// @brief Compact identifier for an interned symbol.
#pragma once

#include <compare>

#include <NGIN/Primitives.hpp>

namespace NGIN::Meta
{
    /// @brief Stable numeric reference to a symbol-table entry.
    struct SymbolId
    {
        /// @brief Integer representation used by symbol identifiers.
        using ValueType = UInt32;

        /// @brief Reserved value representing an invalid identifier.
        static constexpr ValueType InvalidValue = 0;

        /// @brief Stored symbol-table identifier.
        ValueType value {InvalidValue};

        /// @brief Returns whether this identifier refers to a symbol-table entry.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != InvalidValue;
        }

        /// @brief Returns the invalid identifier sentinel.
        [[nodiscard]] static constexpr SymbolId Invalid() noexcept
        {
            return SymbolId {};
        }

        /// @brief Compares identifiers by their numeric value.
        constexpr auto operator<=>(const SymbolId&) const noexcept = default;
    };
}// namespace NGIN::Meta
