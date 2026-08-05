/// @file ReflectionIdentity.hpp
/// @brief Stable module and type identities for reflection metadata.
#pragma once

#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Meta/SymbolId.hpp>

namespace NGIN::Meta
{
    namespace detail
    {
        template<class T>
        [[nodiscard]] inline UInt64 HashIdentityComponent(UInt64 seed, const T& value) noexcept
        {
            const UInt8* bytes = reinterpret_cast<const UInt8*>(&value);
            UInt64       hash  = seed;
            for (UIntSize i = 0; i < sizeof(T); ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            return hash;
        }
    }// namespace detail

    /// @brief Identifies a module by name and ABI family/version.
    struct ModuleIdentity
    {
        SymbolId moduleName {};
        UInt32   abiFamily {1};
        UInt32   abiVersion {1};
        UInt64   keyHash {0};

        /// @brief Returns whether all required module identity fields are populated.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return moduleName.IsValid() && keyHash != 0;
        }

        /// @brief Creates a module identity and derives its deterministic key hash.
        /// @param moduleNameId Interned module name.
        /// @param family ABI compatibility family.
        /// @param version ABI version within the family.
        [[nodiscard]] static ModuleIdentity Create(SymbolId moduleNameId,
                                                   UInt32   family  = 1,
                                                   UInt32   version = 1) noexcept
        {
            UInt64 seed = NGIN::Hashing::FNV1a64("NGIN.ModuleIdentity", 19u);
            seed        = detail::HashIdentityComponent(seed, moduleNameId.value);
            seed        = detail::HashIdentityComponent(seed, family);
            seed        = detail::HashIdentityComponent(seed, version);
            return ModuleIdentity {moduleNameId, family, version, seed};
        }

        /// @brief Compares every identity component lexicographically.
        constexpr auto operator<=>(const ModuleIdentity&) const noexcept = default;
    };

    /// @brief Identifies a reflected type within a module and ABI signature.
    struct TypeIdentity
    {
        ModuleIdentity module {};
        SymbolId       qualifiedName {};
        UInt64         signatureHash {0};
        UInt64         keyHash {0};

        /// @brief Returns whether all required type identity fields are populated.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return module.IsValid() && qualifiedName.IsValid() && signatureHash != 0 && keyHash != 0;
        }

        /// @brief Creates a type identity and derives its deterministic key hash.
        /// @param moduleIdentity Owning module identity.
        /// @param qualifiedNameId Interned qualified type name.
        /// @param signature Hash of the reflected ABI signature.
        [[nodiscard]] static TypeIdentity Create(const ModuleIdentity& moduleIdentity,
                                                 SymbolId              qualifiedNameId,
                                                 UInt64                signature) noexcept
        {
            UInt64 seed = NGIN::Hashing::FNV1a64("NGIN.TypeIdentity", 17u);
            seed        = detail::HashIdentityComponent(seed, moduleIdentity.keyHash);
            seed        = detail::HashIdentityComponent(seed, qualifiedNameId.value);
            seed        = detail::HashIdentityComponent(seed, signature);
            return TypeIdentity {moduleIdentity, qualifiedNameId, signature, seed};
        }

        /// @brief Compares every identity component lexicographically.
        constexpr auto operator<=>(const TypeIdentity&) const noexcept = default;
    };
}// namespace NGIN::Meta
