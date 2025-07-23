// HalfPointer.hpp
#pragma once

#include <cassert>
#include <cstdint>

#include <NGIN/Primitives.hpp>

namespace NGIN::Memory
{
    class HalfPointer
    {
    public:
        static constexpr UInt32 INVALID_OFFSET = 0xFFFFFFFF;

    public:
        inline HalfPointer()
            : offset(INVALID_OFFSET) {}// Invalid pointer

        inline HalfPointer(void* base, void* ptr)
        {
            assert(ptr >= base && "Pointer must be within the heap");
            offset = static_cast<UInt32>(reinterpret_cast<char*>(ptr) - reinterpret_cast<char*>(base));
        }

        template<typename T>
        inline T* ToAbsolute(T* base) const
        {
            if (offset == INVALID_OFFSET)
                return nullptr;
            return reinterpret_cast<char*>(base) + offset;
        }

        inline UInt32 GetOffset() const
        {
            return offset;
        }

    private:
        UInt32 offset;
    };
}// namespace NGIN::Memory
