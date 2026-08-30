/// @file CheckedArithmetic.hpp
/// @brief Overflow-safe arithmetic helpers for allocator and storage internals.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>

namespace NGIN::Memory::detail
{
    /// @brief Adds two byte counts without wrapping.
    /// @param left First operand.
    /// @param right Second operand.
    /// @param result Receives the sum when it is representable.
    /// @return `true` when the addition succeeded; otherwise `false` and `result` is unchanged.
    [[nodiscard]] constexpr bool CheckedAdd(
            const std::size_t left,
            const std::size_t right,
            std::size_t&      result) noexcept
    {
        if (left > (std::numeric_limits<std::size_t>::max)() - right)
        {
            return false;
        }

        result = left + right;
        return true;
    }

    /// @brief Multiplies two byte counts without wrapping.
    /// @param left First operand.
    /// @param right Second operand.
    /// @param result Receives the product when it is representable.
    /// @return `true` when the multiplication succeeded; otherwise `false` and `result` is unchanged.
    [[nodiscard]] constexpr bool CheckedMultiply(
            const std::size_t left,
            const std::size_t right,
            std::size_t&      result) noexcept
    {
        if (left != 0 && right > (std::numeric_limits<std::size_t>::max)() / left)
        {
            return false;
        }

        result = left * right;
        return true;
    }

    /// @brief Normalizes an alignment to a representable power of two.
    /// @param requested Requested alignment. Zero is treated as one.
    /// @param minimum Smallest permitted alignment.
    /// @param result Receives the normalized alignment on success.
    /// @return `false` when the next power of two cannot be represented.
    [[nodiscard]] constexpr bool TryNormalizeAlignment(
            const std::size_t requested,
            const std::size_t minimum,
            std::size_t&      result) noexcept
    {
        const std::size_t value = (std::max) (requested == 0 ? std::size_t {1} : requested, minimum);
        if (std::has_single_bit(value))
        {
            result = value;
            return true;
        }

        constexpr int sizeBits = std::numeric_limits<std::size_t>::digits;
        if (std::bit_width(value) >= sizeBits)
        {
            return false;
        }

        result = std::size_t {1} << std::bit_width(value);
        return true;
    }
}// namespace NGIN::Memory::detail
