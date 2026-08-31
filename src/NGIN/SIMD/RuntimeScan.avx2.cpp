// SPDX-License-Identifier: Apache-2.0

#include "RuntimeScanVariant.hpp"

namespace NGIN::SIMD::detail
{
    auto RuntimeFindEqByteAVX2(const std::uint8_t* data, std::size_t length, std::uint8_t value) noexcept -> std::size_t
    {
        return RuntimeFindEqByteVariant<AVX2Tag>(data, length, value);
    }

    auto RuntimeFindAny2ByteAVX2(const std::uint8_t* data,
                                 std::size_t         length,
                                 std::uint8_t        a,
                                 std::uint8_t        b) noexcept -> std::size_t
    {
        return RuntimeFindAny2ByteVariant<AVX2Tag>(data, length, a, b);
    }

    auto RuntimeFindAny3ByteAVX2(const std::uint8_t* data,
                                 std::size_t         length,
                                 std::uint8_t        a,
                                 std::uint8_t        b,
                                 std::uint8_t        c) noexcept -> std::size_t
    {
        return RuntimeFindAny3ByteVariant<AVX2Tag>(data, length, a, b, c);
    }

    auto RuntimeFindAny4ByteAVX2(const std::uint8_t* data,
                                 std::size_t         length,
                                 std::uint8_t        a,
                                 std::uint8_t        b,
                                 std::uint8_t        c,
                                 std::uint8_t        d) noexcept -> std::size_t
    {
        return RuntimeFindAny4ByteVariant<AVX2Tag>(data, length, a, b, c, d);
    }
}// namespace NGIN::SIMD::detail
