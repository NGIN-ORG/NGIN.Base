#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "NGIN/SIMD/Scan.hpp"

namespace NGIN::SIMD::detail
{
    template<class Backend>
    [[nodiscard]] inline auto RuntimeFindEqByteVariant(const std::uint8_t* data,
                                                       std::size_t         length,
                                                       std::uint8_t        value) noexcept -> std::size_t
    {
        return FindEqByte<Backend>(data, length, value);
    }

    template<class Backend>
    [[nodiscard]] inline auto RuntimeFindAny2ByteVariant(const std::uint8_t* data,
                                                         std::size_t         length,
                                                         std::uint8_t        a,
                                                         std::uint8_t        b) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data, length, a, b);
    }

    template<class Backend>
    [[nodiscard]] inline auto RuntimeFindAny3ByteVariant(const std::uint8_t* data,
                                                         std::size_t         length,
                                                         std::uint8_t        a,
                                                         std::uint8_t        b,
                                                         std::uint8_t        c) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data, length, a, b, c);
    }

    template<class Backend>
    [[nodiscard]] inline auto RuntimeFindAny4ByteVariant(const std::uint8_t* data,
                                                         std::size_t         length,
                                                         std::uint8_t        a,
                                                         std::uint8_t        b,
                                                         std::uint8_t        c,
                                                         std::uint8_t        d) noexcept -> std::size_t
    {
        return FindAnyByte<Backend>(data, length, a, b, c, d);
    }
}// namespace NGIN::SIMD::detail
