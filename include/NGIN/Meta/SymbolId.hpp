#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Meta
{
  struct SymbolId
  {
    using ValueType = UInt32;

    static constexpr ValueType InvalidValue = 0;

    ValueType value{InvalidValue};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return value != InvalidValue;
    }

    [[nodiscard]] static constexpr SymbolId Invalid() noexcept
    {
      return SymbolId{};
    }

    constexpr auto operator<=>(const SymbolId &) const noexcept = default;
  };
} // namespace NGIN::Meta
