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

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindEqByte(const Byte* data, std::size_t length, Byte value) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindEqByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const auto* bytes  = reinterpret_cast<const std::uint8_t*>(data);
        const auto  needle = detail::ToU8(value);

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
        const auto    needleVector = VecType(needle);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const auto chunk = VecType::Load(bytes + index);
            const auto mask  = (chunk == needleVector);
            const auto bits  = MaskToBits(mask);
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

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindEqByte(std::span<const Byte> data, Byte value) noexcept -> std::size_t
    {
        return FindEqByte<Backend>(data.data(), data.size(), value);
    }

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(const Byte* data, std::size_t length, Byte a, Byte b) noexcept -> std::size_t
    {
        static_assert(sizeof(Byte) == 1, "FindAnyByte requires a 1-byte element type.");
        if (!data || length == 0)
        {
            return length;
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const auto  va    = detail::ToU8(a);
        const auto  vb    = detail::ToU8(b);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const auto value = bytes[index];
                if (value == va || value == vb)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const auto    vaVec = VecType(va);
        const auto    vbVec = VecType(vb);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const auto chunk = VecType::Load(bytes + index);
            const auto mask  = (chunk == vaVec) | (chunk == vbVec);
            const auto bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const auto value = bytes[index];
            if (value == va || value == vb)
            {
                return index;
            }
        }

        return length;
    }

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b);
    }

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

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const auto  va    = detail::ToU8(a);
        const auto  vb    = detail::ToU8(b);
        const auto  vc    = detail::ToU8(c);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const auto value = bytes[index];
                if (value == va || value == vb || value == vc)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const auto    vaVec = VecType(va);
        const auto    vbVec = VecType(vb);
        const auto    vcVec = VecType(vc);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const auto chunk = VecType::Load(bytes + index);
            const auto mask  = (chunk == vaVec) | (chunk == vbVec) | (chunk == vcVec);
            const auto bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const auto value = bytes[index];
            if (value == va || value == vb || value == vc)
            {
                return index;
            }
        }

        return length;
    }

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b, Byte c) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b, c);
    }

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

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
        const auto  va    = detail::ToU8(a);
        const auto  vb    = detail::ToU8(b);
        const auto  vc    = detail::ToU8(c);
        const auto  vd    = detail::ToU8(d);

        constexpr std::size_t kSimdScanMinBytes = 128;
        if (length < kSimdScanMinBytes)
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                const auto value = bytes[index];
                if (value == va || value == vb || value == vc || value == vd)
                {
                    return index;
                }
            }
            return length;
        }

        using VecType       = Vec<std::uint8_t, Backend>;
        constexpr int lanes = VecType::lanes;
        const auto    vaVec = VecType(va);
        const auto    vbVec = VecType(vb);
        const auto    vcVec = VecType(vc);
        const auto    vdVec = VecType(vd);

        std::size_t index = 0;
        for (; index + static_cast<std::size_t>(lanes) <= length; index += static_cast<std::size_t>(lanes))
        {
            const auto chunk = VecType::Load(bytes + index);
            const auto mask  = (chunk == vaVec) | (chunk == vbVec) | (chunk == vcVec) | (chunk == vdVec);
            const auto bits  = MaskToBits(mask);
            if (bits != 0)
            {
                return index + static_cast<std::size_t>(std::countr_zero(bits));
            }
        }

        for (; index < length; ++index)
        {
            const auto value = bytes[index];
            if (value == va || value == vb || value == vc || value == vd)
            {
                return index;
            }
        }

        return length;
    }

    template<class Backend = DefaultBackend, class Byte>
    [[nodiscard]] inline auto FindAnyByte(std::span<const Byte> data, Byte a, Byte b, Byte c, Byte d) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data.data(), data.size(), a, b, c, d);
    }
}// namespace NGIN::SIMD
