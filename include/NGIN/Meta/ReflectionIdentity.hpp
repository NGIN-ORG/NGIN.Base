#pragma once

#include <NGIN/Meta/SymbolId.hpp>
#include <NGIN/Hashing/FNV.hpp>

namespace NGIN::Meta
{
  namespace detail
  {
    template <class T>
    [[nodiscard]] inline UInt64 HashIdentityComponent(UInt64 seed, const T &value) noexcept
    {
      const auto bytes = reinterpret_cast<const UInt8 *>(&value);
      UInt64 hash = seed;
      for (UIntSize i = 0; i < sizeof(T); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
      return hash;
    }
  } // namespace detail

  struct ModuleIdentity
  {
    SymbolId moduleName{};
    UInt32 abiFamily{1};
    UInt32 abiVersion{1};
    UInt64 keyHash{0};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return moduleName.IsValid() && keyHash != 0;
    }

    [[nodiscard]] static ModuleIdentity Create(SymbolId moduleNameId,
                                               UInt32 family = 1,
                                               UInt32 version = 1) noexcept
    {
      UInt64 seed = NGIN::Hashing::FNV1a64("NGIN.ModuleIdentity", 19u);
      seed = detail::HashIdentityComponent(seed, moduleNameId.value);
      seed = detail::HashIdentityComponent(seed, family);
      seed = detail::HashIdentityComponent(seed, version);
      return ModuleIdentity{moduleNameId, family, version, seed};
    }

    constexpr auto operator<=>(const ModuleIdentity &) const noexcept = default;
  };

  struct TypeIdentity
  {
    ModuleIdentity module{};
    SymbolId qualifiedName{};
    UInt64 signatureHash{0};
    UInt64 keyHash{0};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return module.IsValid() && qualifiedName.IsValid() && signatureHash != 0 && keyHash != 0;
    }

    [[nodiscard]] static TypeIdentity Create(const ModuleIdentity &moduleIdentity,
                                             SymbolId qualifiedNameId,
                                             UInt64 signature) noexcept
    {
      UInt64 seed = NGIN::Hashing::FNV1a64("NGIN.TypeIdentity", 17u);
      seed = detail::HashIdentityComponent(seed, moduleIdentity.keyHash);
      seed = detail::HashIdentityComponent(seed, qualifiedNameId.value);
      seed = detail::HashIdentityComponent(seed, signature);
      return TypeIdentity{moduleIdentity, qualifiedNameId, signature, seed};
    }

    constexpr auto operator<=>(const TypeIdentity &) const noexcept = default;
  };
} // namespace NGIN::Meta
