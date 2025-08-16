// TypeId.hpp
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <Hashing/FNV.hpp>
namespace NGIN::Meta
{
    // FNV-1a 64-bit constexpr hash
    constexpr UInt64 FNV1a(const char* str, UIntSize len) noexcept
    {
        UInt64 hash = 14695981039346656037Ui64;
        for (UIntSize i = 0; i < len; ++i)
        {
            hash ^= static_cast<UInt64>(str[i]);
            hash *= 1099511628211Ui64;
        }
        return hash;
    }

    template<typename T>
    struct TypeId
    {
        static constexpr UInt64 GetId() noexcept
        {
            constexpr auto name = Meta::TypeTraits<T>::qualifiedName;
            return NGIN::Hashing::FNV1a64(name.data(), name.size());
        }
    };

    template<typename T>
    constexpr UInt64 GetTypeId() noexcept
    {
        return Meta::TypeId<T>::GetId();
    }
}// namespace NGIN::Meta
