#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace NGIN::Memory
{
    /// @brief Calculates the adjustment needed to align the given address forward to the specified alignment.
    /// @param address The address to align.
    /// @param alignment The alignment in bytes (must be a power of two).
    /// @return The number of bytes required to align the address.
    [[nodiscard]] inline std::size_t CalculateAlignmentAdjustment(std::uintptr_t address, std::size_t alignment)
    {
        assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");
        std::size_t misalignment = address & (alignment - 1);
        return misalignment ? (alignment - misalignment) : 0;
    }

    /// @brief Calculates the adjustment needed to align the given pointer forward to the specified alignment.
    /// @param ptr The pointer to align.
    /// @param alignment The alignment in bytes (must be a power of two).
    /// @return The number of bytes required to align the pointer.
    [[nodiscard]] inline std::size_t CalculateAlignmentAdjustment(const void* ptr, std::size_t alignment)
    {
        return CalculateAlignmentAdjustment(reinterpret_cast<std::uintptr_t>(ptr), alignment);
    }

    /// @brief Aligns the given pointer forward to the specified alignment.
    /// @param ptr The pointer to align.
    /// @param alignment The alignment in bytes (must be a power of two).
    /// @return The aligned pointer.
    [[nodiscard]] inline void* AlignPointerForward(void* ptr, std::size_t alignment)
    {
        assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");
        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(ptr);
        std::size_t adjustment = CalculateAlignmentAdjustment(address, alignment);
        return reinterpret_cast<void*>(address + adjustment);
    }

    /// @brief Aligns the given address forward to the specified alignment.
    /// @param address The address to align.
    /// @param alignment The alignment in bytes (must be a power of two).
    /// @return The aligned address.
    [[nodiscard]] inline std::uintptr_t AlignAddressForward(std::uintptr_t address, std::size_t alignment)
    {
        std::size_t adjustment = CalculateAlignmentAdjustment(address, alignment);
        return address + adjustment;
    }
}// namespace NGIN::Memory
