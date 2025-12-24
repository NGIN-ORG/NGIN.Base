// HalfPointer.hpp
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

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
            const auto diff = static_cast<std::uintptr_t>(reinterpret_cast<const std::byte*>(ptr) -
                                                          reinterpret_cast<const std::byte*>(base));
            assert(diff <= std::numeric_limits<UInt32>::max() && "HalfPointer offset overflow");
            offset = static_cast<UInt32>(diff);
        }

        template<typename T>
        inline T* ToAbsolute(T* base) const
        {
            if (offset == INVALID_OFFSET)
                return nullptr;
            auto* baseBytes = reinterpret_cast<std::byte*>(base);
            return reinterpret_cast<T*>(baseBytes + offset);
        }

        inline UInt32 GetOffset() const
        {
            return offset;
        }

    private:
        UInt32 offset;
    };
}// namespace NGIN::Memory
