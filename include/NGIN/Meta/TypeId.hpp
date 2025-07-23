// TypeId.hpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace NGIN::Meta
{
    template<typename T>
    struct TypeId
    {
        static std::uintptr_t GetId()
        {
            static const char id {};
            return reinterpret_cast<std::uintptr_t>(&id);
        }
    };

    template<typename T>
    constexpr std::uintptr_t GetTypeId()
    {
        return Meta::TypeId<T>::GetId();
    }
}// namespace NGIN::Meta
