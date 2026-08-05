/// @file HalfPointer.hpp
/// @brief Compact pointer representation using a 32-bit offset from a caller-provided base.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <NGIN/Primitives.hpp>

namespace NGIN::Memory
{
    /// @brief Stores a relocatable 32-bit byte offset instead of an absolute pointer.
    class HalfPointer
    {
    public:
        /// @brief Sentinel offset used by invalid pointers.
        static constexpr UInt32 INVALID_OFFSET = 0xFFFFFFFF;

    public:
        /// @brief Constructs an invalid pointer.
        inline HalfPointer()
            : offset(INVALID_OFFSET) {}// Invalid pointer

        /// @brief Constructs an offset from an allocation base to a pointer within that allocation.
        /// @param base Base address used later by `ToAbsolute`.
        /// @param ptr Address at or after `base` and within 32-bit offset range.
        inline HalfPointer(void* base, void* ptr)
        {
            assert(ptr >= base && "Pointer must be within the heap");
            const std::uintptr_t diff = static_cast<std::uintptr_t>(reinterpret_cast<const std::byte*>(ptr) -
                                                                    reinterpret_cast<const std::byte*>(base));
            assert(diff <= std::numeric_limits<UInt32>::max() && "HalfPointer offset overflow");
            offset = static_cast<UInt32>(diff);
        }

        /// @brief Resolves the stored offset against a base address.
        /// @tparam T Pointed-to type.
        /// @param base Base corresponding to the one used during construction.
        /// @return Resolved address, or `nullptr` when this pointer is invalid.
        template<typename T>
        inline T* ToAbsolute(T* base) const
        {
            if (offset == INVALID_OFFSET)
                return nullptr;
            std::byte* baseBytes = reinterpret_cast<std::byte*>(base);
            return reinterpret_cast<T*>(baseBytes + offset);
        }

        /// @brief Returns the stored byte offset or `INVALID_OFFSET`.
        inline UInt32 GetOffset() const
        {
            return offset;
        }

    private:
        UInt32 offset;
    };
}// namespace NGIN::Memory
