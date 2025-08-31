// TypeId.hpp
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Hashing/FNV.hpp>
namespace NGIN::Meta
{

    template<typename T>
    struct TypeId
    {
        static constexpr UInt64 GetId() noexcept
        {
            constexpr auto name = Meta::TypeName<T>::qualifiedName;
            return NGIN::Hashing::FNV1a64(name.data(), name.size());
        }
    };

    template<typename T>
    constexpr UInt64 GetTypeId() noexcept
    {
        return Meta::TypeId<T>::GetId();
    }
}// namespace NGIN::Meta
