#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Byte-oriented scan helpers built on top of the SIMD facade.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "NGIN/SIMD/Vec.hpp"

namespace NGIN::SIMD
{
    namespace detail
    {
        template<class Byte>
        [[nodiscard]] constexpr auto ToU8(Byte value) noexcept -> std::uint8_t
        {
            if constexpr (std::is_same_v<Byte, std::byte>)
            {
                return std::to_integer<std::uint8_t>(value);
            }
            else
            {
                return static_cast<std::uint8_t>(value);
            }
        }
    }// namespace detail

    /// @brief Returns the first index equal to @p value, or @p length when not found.
    /// @pre `Byte` is a one-byte type; @p data may be null only when @p length is zero.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindEqByte(const Byte* data, std::size_t length, Byte value) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindEqByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const std::uint8_t* bytes  = reinterpret_cast<const std::uint8_t*>(data);
        const std::uint8_t  needle = detail::ToU8(value);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                if (bytes[index] == needle)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType              = Vec<std::uint8_t, Backend>;
        constexpr int lanes        = VecType::lanes;
        const VecType needleVector = VecType(needle);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const VecType                     chunk = VecType::Load(bytes + index);
            const typename VecType::mask_type mask  = (chunk == needleVector);
            const std::uint64_t               bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            if (bytes[index] == needle)
            {
                return index;
            }
        }

        return length;
    }

    /// @brief Returns the first span index equal to @p value, or the span size when not found.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindEqByte(std::span<const Byte> data, Byte value) noexcept -> std::size_t
    {
        return FindEqByte<Backend>(data.data(), data.size(), value);
    }

    /// @brief Returns the first index equal to either candidate, or @p length when not found.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(const Byte* data, std::size_t length, Byte a, Byte b) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindAnyByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const std::uint8_t  va    = detail::ToU8(a);
        const std::uint8_t  vb    = detail::ToU8(b);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const std::uint8_t value = bytes[index];
                if (value == va || value == vb)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const VecType vaVec = VecType(va);
        const VecType vbVec = VecType(vb);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const VecType                     chunk = VecType::Load(bytes + index);
            const typename VecType::mask_type mask  = (chunk == vaVec) | (chunk == vbVec);
            const std::uint64_t               bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const std::uint8_t value = bytes[index];
            if (value == va || value == vb)
            {
                return index;
            }
        }

        return length;
    }

    /// @brief Returns the first span index equal to either candidate, or the span size.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b);
    }

    /// @brief Returns the first index equal to any of three candidates, or @p length.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(const Byte* data,
                                          std::size_t length,
                                          Byte        a,
                                          Byte        b,
                                          Byte        c) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindAnyByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const std::uint8_t  va    = detail::ToU8(a);
        const std::uint8_t  vb    = detail::ToU8(b);
        const std::uint8_t  vc    = detail::ToU8(c);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const std::uint8_t value = bytes[index];
                if (value == va || value == vb || value == vc)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const VecType vaVec = VecType(va);
        const VecType vbVec = VecType(vb);
        const VecType vcVec = VecType(vc);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const VecType                     chunk = VecType::Load(bytes + index);
            const typename VecType::mask_type mask  = (chunk == vaVec) | (chunk == vbVec) | (chunk == vcVec);
            const std::uint64_t               bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const std::uint8_t value = bytes[index];
            if (value == va || value == vb || value == vc)
            {
                return index;
            }
        }

        return length;
    }

    /// @brief Returns the first span index equal to any of three candidates, or the span size.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b, Byte c) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b, c);
    }

    /// @brief Returns the first index equal to any of four candidates, or @p length.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(const Byte* data,
                                          std::size_t length,
                                          Byte        a,
                                          Byte        b,
                                          Byte        c,
                                          Byte        d) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindAnyByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const std::uint8_t  va    = detail::ToU8(a);
        const std::uint8_t  vb    = detail::ToU8(b);
        const std::uint8_t  vc    = detail::ToU8(c);
        const std::uint8_t  vd    = detail::ToU8(d);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const std::uint8_t value = bytes[index];
                if (value == va || value == vb || value == vc || value == vd)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const VecType vaVec = VecType(va);
        const VecType vbVec = VecType(vb);
        const VecType vcVec = VecType(vc);
        const VecType vdVec = VecType(vd);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const VecType                     chunk = VecType::Load(bytes + index);
            const typename VecType::mask_type mask =
                    (chunk == vaVec) | (chunk == vbVec) | (chunk == vcVec) | (chunk == vdVec);
            const std::uint64_t bits = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const std::uint8_t value = bytes[index];
            if (value == va || value == vb || value == vc || value == vd)
            {
                return index;
            }
        }

        return length;
    }

    /// @brief Returns the first span index equal to any of four candidates, or the span size.
    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b, Byte c, Byte d) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b, c, d);
    }
}// namespace NGIN::SIMD
