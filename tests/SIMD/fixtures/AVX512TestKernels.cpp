// SPDX-License-Identifier: Apache-2.0

#include "NGIN/SIMD.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

using namespace NGIN::SIMD;

auto RunAVX512KernelTests() noexcept -> bool
{
    using VecFloat  = Vec<float, AVX512Tag>;
    using VecDouble = Vec<double, AVX512Tag>;
    using VecInt    = Vec<std::int32_t, AVX512Tag>;
    using VecByte   = Vec<std::uint8_t, AVX512Tag>;

    static_assert(VecFloat::lanes == 16);
    static_assert(VecDouble::lanes == 8);
    static_assert(VecInt::lanes == 16);
    static_assert(VecByte::lanes == 64);
    static_assert(detail::BackendTraits<AVX512Tag, float>::Ops<16>::has_native_overrides);
    static_assert(detail::BackendTraits<AVX512Tag, double>::Ops<8>::has_native_overrides);
    static_assert(detail::BackendTraits<AVX512Tag, std::int32_t>::Ops<16>::has_native_overrides);
    static_assert(detail::BackendTraits<AVX512Tag, std::uint8_t>::Ops<64>::has_native_overrides);
    static_assert(detail::BackendTraits<AVX512Tag, std::int8_t>::Ops<64>::has_native_overrides);

    alignas(64) std::array<float, VecFloat::lanes> floatValues {};
    for (int lane = 0; lane < VecFloat::lanes; ++lane)
    {
        floatValues[static_cast<std::size_t>(lane)] = static_cast<float>(lane + 1);
    }
    const VecFloat floats = VecFloat::LoadAligned(floatValues.data(), 64);
    const VecFloat sums   = Fma(floats, VecFloat {2.0F}, VecFloat {1.0F});
    if (sums.GetLane(0) != 3.0F || sums.GetLane(15) != 33.0F || !All(sums > floats))
    {
        return false;
    }

    VecFloat::mask_type alternating {};
    for (int lane = 0; lane < VecFloat::lanes; ++lane)
    {
        alternating.SetLane(lane, lane % 2 == 0);
    }
    const VecFloat masked = VecFloat::Load(floatValues.data(), alternating, -1.0F);
    if (masked.GetLane(0) != 1.0F || masked.GetLane(1) != -1.0F)
    {
        return false;
    }

    const VecInt                                   indices  = VecInt::Iota(15, -1);
    const VecFloat                                 gathered = VecFloat::Gather(floatValues.data(), indices);
    alignas(64) std::array<float, VecFloat::lanes> scattered {};
    gathered.Scatter(scattered.data(), indices);
    if (scattered != floatValues)
    {
        return false;
    }

    alignas(64) std::array<double, VecDouble::lanes> doubleValues {};
    for (int lane = 0; lane < VecDouble::lanes; ++lane)
    {
        doubleValues[static_cast<std::size_t>(lane)] = static_cast<double>(lane + 1);
    }
    const VecDouble doubles = VecDouble::LoadAligned(doubleValues.data(), 64);
    const VecDouble scaled  = Fma(doubles, VecDouble {0.5}, VecDouble {1.0});
    if (scaled.GetLane(0) != 1.5 || scaled.GetLane(7) != 5.0 || !All(scaled > VecDouble {1.0}))
    {
        return false;
    }

    using VecDoubleIndex                                             = Vec<std::int32_t, AVX512Tag, VecDouble::lanes>;
    const VecDoubleIndex                             doubleIndex     = VecDoubleIndex::Iota(VecDouble::lanes - 1, -1);
    const VecDouble                                  gatheredDoubles = VecDouble::Gather(doubleValues.data(), doubleIndex);
    alignas(64) std::array<double, VecDouble::lanes> scatteredDoubles {};
    gatheredDoubles.Scatter(scatteredDoubles.data(), doubleIndex);
    if (scatteredDoubles != doubleValues)
    {
        return false;
    }

    const VecInt integers = VecInt::Iota(-8, 1);
    const VecInt squared  = integers * integers;
    if (squared.GetLane(0) != 64 || squared.GetLane(8) != 0 || Abs(integers).GetLane(0) != 8)
    {
        return false;
    }

    alignas(64) std::array<std::uint8_t, VecByte::lanes> bytes {};
    std::fill(bytes.begin(), bytes.end(), static_cast<std::uint8_t>(3));
    bytes[47]                = 9;
    const VecByte byteVector = VecByte::LoadAligned(bytes.data(), 64);
    if (MaskToBits(byteVector == VecByte {9}) != (std::uint64_t {1} << 47))
    {
        return false;
    }

    return true;
}
