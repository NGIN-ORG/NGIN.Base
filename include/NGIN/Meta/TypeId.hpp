/// @file TypeId.hpp
/// @brief Compile-time type identifiers derived from qualified type names.
#pragma once

#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Primitives.hpp>
namespace NGIN::Meta
{

    /// @brief Computes the FNV-1a identifier for a type's qualified name.
    /// @tparam T Type to identify.
    template<typename T>
    struct TypeId
    {
        /// @brief Returns the deterministic identifier for `T`.
        static constexpr UInt64 GetId() noexcept
        {
            constexpr std::string_view name = Meta::TypeName<T>::qualifiedName;
            return NGIN::Hashing::FNV1a64(name.data(), name.size());
        }
    };

    /// @brief Returns the deterministic qualified-name identifier for a type.
    /// @tparam T Type to identify.
    template<typename T>
    constexpr UInt64 GetTypeId() noexcept
    {
        return Meta::TypeId<T>::GetId();
    }
}// namespace NGIN::Meta
