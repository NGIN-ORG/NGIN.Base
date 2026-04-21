#pragma once

#include <NGIN/Meta/SymbolId.hpp>
#include <NGIN/Utilities/StringInterner.hpp>

#include <limits>
#include <string_view>

namespace NGIN::Utilities
{
  template <class Allocator = NGIN::Memory::SystemAllocator,
            class ThreadPolicy = detail::NullMutex>
  class SymbolTable
  {
  public:
    using InternerType = StringInterner<Allocator, ThreadPolicy>;

    [[nodiscard]] NGIN::Meta::SymbolId Intern(std::string_view value)
    {
      const auto id = m_interner.InsertOrGet(value);
      if (id == InternerType::INVALID_ID)
        return {};

      if (id >= std::numeric_limits<NGIN::Meta::SymbolId::ValueType>::max() - 1u)
        return {};

      return NGIN::Meta::SymbolId{
          static_cast<NGIN::Meta::SymbolId::ValueType>(id + 1u)};
    }

    [[nodiscard]] bool TryGet(std::string_view value, NGIN::Meta::SymbolId &out) const noexcept
    {
      typename InternerType::IdType id = InternerType::INVALID_ID;
      if (!m_interner.TryGetId(value, id))
        return false;

      out = NGIN::Meta::SymbolId{
          static_cast<NGIN::Meta::SymbolId::ValueType>(id + 1u)};
      return true;
    }

    [[nodiscard]] std::string_view View(NGIN::Meta::SymbolId id) const noexcept
    {
      if (!id.IsValid())
        return {};

      return m_interner.View(static_cast<typename InternerType::IdType>(id.value - 1u));
    }

    [[nodiscard]] UIntSize Size() const noexcept
    {
      return m_interner.Size();
    }

    void Clear() noexcept
    {
      m_interner.Clear();
    }

  private:
    InternerType m_interner{};
  };
} // namespace NGIN::Utilities
