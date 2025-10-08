#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Scalar baseline implementation of the NGIN SIMD façade. Future backends hook
// into the same interface by specializing the underlying storage operations.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "NGIN/SIMD/Tags.hpp"

namespace NGIN::SIMD {

namespace detail {

template<class T, int Lanes>
struct ScalarStorage {
    static_assert(Lanes > 0, "Lane count must be positive.");

    using value_type = T;
    static constexpr int lanes = Lanes;

    constexpr ScalarStorage() noexcept = default;

    constexpr explicit ScalarStorage(T value) noexcept {
        data.fill(value);
    }

    [[nodiscard]] constexpr auto Get(int index) const noexcept -> T {
        return data[static_cast<std::size_t>(index)];
    }

    constexpr void Set(int index, T value) noexcept {
        data[static_cast<std::size_t>(index)] = value;
    }

    std::array<T, static_cast<std::size_t>(lanes)> data{};
};

template<class T>
inline constexpr int NativeLaneCountFor([[maybe_unused]] ScalarTag) noexcept {
    return 1;
}

template<class T, class Backend>
inline constexpr int ResolveLaneCount(int requested) noexcept {
    if (requested > 0) {
        return requested;
    }
    if constexpr (std::same_as<Backend, ScalarTag>) {
        return NativeLaneCountFor<T>(Backend{});
    } else {
        static_assert(requested > 0, "Backend lane resolution not implemented.");
        return requested;
    }
}

template<class T>
[[nodiscard]] constexpr auto BitwiseAnd(T lhs, T rhs) noexcept -> T {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(lhs & rhs);
    } else {
        using Bits = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                        std::uint32_t, std::uint64_t>;
        const auto bl = std::bit_cast<Bits>(lhs);
        const auto br = std::bit_cast<Bits>(rhs);
        return std::bit_cast<T>(bl & br);
    }
}

template<class T>
[[nodiscard]] constexpr auto BitwiseOr(T lhs, T rhs) noexcept -> T {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(lhs | rhs);
    } else {
        using Bits = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                        std::uint32_t, std::uint64_t>;
        const auto bl = std::bit_cast<Bits>(lhs);
        const auto br = std::bit_cast<Bits>(rhs);
        return std::bit_cast<T>(bl | br);
    }
}

template<class T>
[[nodiscard]] constexpr auto BitwiseXor(T lhs, T rhs) noexcept -> T {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(lhs ^ rhs);
    } else {
        using Bits = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                        std::uint32_t, std::uint64_t>;
        const auto bl = std::bit_cast<Bits>(lhs);
        const auto br = std::bit_cast<Bits>(rhs);
        return std::bit_cast<T>(bl ^ br);
    }
}

template<class T>
[[nodiscard]] constexpr auto BitwiseNot(T value) noexcept -> T {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(~value);
    } else {
        using Bits = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                        std::uint32_t, std::uint64_t>;
        const auto b = std::bit_cast<Bits>(value);
        return std::bit_cast<T>(~b);
    }
}

} // namespace detail

template<int Lanes, class Backend>
struct Mask {
    static_assert(Lanes > 0, "Mask must specify lane count explicitly.");

    using backend = Backend;
    static constexpr int lanes = Lanes;

    constexpr Mask() noexcept = default;
    constexpr explicit Mask(bool value) noexcept {
        bits.fill(value);
    }

    [[nodiscard]] constexpr auto GetLane(int index) const noexcept -> bool {
        return bits[static_cast<std::size_t>(index)];
    }

    constexpr void SetLane(int index, bool value) noexcept {
        bits[static_cast<std::size_t>(index)] = value;
    }

    std::array<bool, static_cast<std::size_t>(lanes)> bits{};
};

template<class T, class Backend, int Lanes>
struct Vec {
    static constexpr int lanes = detail::ResolveLaneCount<T, Backend>(Lanes);
    static_assert(lanes > 0, "Lane count must be resolvable at compile time.");

    using value_type = T;
    using backend = Backend;
    using mask_type = Mask<lanes, Backend>;

    static_assert(std::same_as<Backend, ScalarTag>,
                  "Only ScalarTag backend is implemented at this stage.");

    constexpr Vec() noexcept = default;

    constexpr explicit Vec(T value) noexcept
        : storage(value) {}

    [[nodiscard]] static constexpr auto Iota(T start, T step) noexcept -> Vec {
        Vec result;
        T current = start;
        for (int lane = 0; lane < lanes; ++lane) {
            result.storage.Set(lane, current);
            current = static_cast<T>(current + step);
        }
        return result;
    }

    [[nodiscard]] static constexpr auto Load(const T* pointer) noexcept -> Vec {
        Vec result;
        for (int lane = 0; lane < lanes; ++lane) {
            result.storage.Set(lane, pointer[lane]);
        }
        return result;
    }

    [[nodiscard]] static constexpr auto LoadAligned(const T* pointer,
                                                    [[maybe_unused]] int align = alignof(T) * lanes) noexcept -> Vec {
        return Load(pointer);
    }

    [[nodiscard]] static constexpr auto Load(const T* pointer,
                                             const mask_type& mask,
                                             T fill = T{}) noexcept -> Vec {
        Vec result(fill);
        for (int lane = 0; lane < lanes; ++lane) {
            if (mask.GetLane(lane)) {
                result.storage.Set(lane, pointer[lane]);
            }
        }
        return result;
    }

    constexpr void Store(T* pointer) const noexcept {
        for (int lane = 0; lane < lanes; ++lane) {
            pointer[lane] = storage.Get(lane);
        }
    }

    constexpr void StoreAligned(T* pointer,
                                [[maybe_unused]] int align = alignof(T) * lanes) const noexcept {
        Store(pointer);
    }

    constexpr void Store(T* pointer, const mask_type& mask) const noexcept {
        for (int lane = 0; lane < lanes; ++lane) {
            if (mask.GetLane(lane)) {
                pointer[lane] = storage.Get(lane);
            }
        }
    }

    template<class IndexVec>
    [[nodiscard]] static constexpr auto Gather(const T* base, const IndexVec& indices) noexcept -> Vec
        requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
    {
        static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
        Vec result;
        for (int lane = 0; lane < lanes; ++lane) {
            const auto offset = static_cast<std::size_t>(indices.GetLane(lane));
            result.storage.Set(lane, base[offset]);
        }
        return result;
    }

    template<class IndexVec>
    [[nodiscard]] static constexpr auto Gather(const T* base,
                                               const IndexVec& indices,
                                               const mask_type& mask,
                                               T fill = T{}) noexcept -> Vec
        requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
    {
        static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
        Vec result(fill);
        for (int lane = 0; lane < lanes; ++lane) {
            if (mask.GetLane(lane)) {
                const auto offset = static_cast<std::size_t>(indices.GetLane(lane));
                result.storage.Set(lane, base[offset]);
            }
        }
        return result;
    }

    template<class IndexVec>
    constexpr void Scatter(T* base, const IndexVec& indices) const noexcept
        requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
    {
        static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
        for (int lane = 0; lane < lanes; ++lane) {
            const auto offset = static_cast<std::size_t>(indices.GetLane(lane));
            base[offset] = storage.Get(lane);
        }
    }

    template<class IndexVec>
    constexpr void Scatter(T* base, const IndexVec& indices, const mask_type& mask) const noexcept
        requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
    {
        static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
        for (int lane = 0; lane < lanes; ++lane) {
            if (mask.GetLane(lane)) {
                const auto offset = static_cast<std::size_t>(indices.GetLane(lane));
                base[offset] = storage.Get(lane);
            }
        }
    }

    [[nodiscard]] constexpr auto GetLane(int index) const noexcept -> T {
        return storage.Get(index);
    }

    constexpr void SetLane(int index, T value) noexcept {
        storage.Set(index, value);
    }

    detail::ScalarStorage<T, lanes> storage{};
};

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator+(Vec<T, Backend, Lanes> lhs,
                                       const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes> {
    for (int lane = 0; lane < lhs.lanes; ++lane) {
        lhs.storage.Set(lane, static_cast<T>(lhs.storage.Get(lane) + rhs.storage.Get(lane)));
    }
    return lhs;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator-(Vec<T, Backend, Lanes> lhs,
                                       const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes> {
    for (int lane = 0; lane < lhs.lanes; ++lane) {
        lhs.storage.Set(lane, static_cast<T>(lhs.storage.Get(lane) - rhs.storage.Get(lane)));
    }
    return lhs;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator*(Vec<T, Backend, Lanes> lhs,
                                       const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes> {
    for (int lane = 0; lane < lhs.lanes; ++lane) {
        lhs.storage.Set(lane, static_cast<T>(lhs.storage.Get(lane) * rhs.storage.Get(lane)));
    }
    return lhs;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator/(Vec<T, Backend, Lanes> lhs,
                                       const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes> {
    for (int lane = 0; lane < lhs.lanes; ++lane) {
        lhs.storage.Set(lane, static_cast<T>(lhs.storage.Get(lane) / rhs.storage.Get(lane)));
    }
    return lhs;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Fma(Vec<T, Backend, Lanes> a,
                                 const Vec<T, Backend, Lanes>& b,
                                 const Vec<T, Backend, Lanes>& c) noexcept -> Vec<T, Backend, Lanes> {
    for (int lane = 0; lane < a.lanes; ++lane) {
        const auto ab = static_cast<T>(a.storage.Get(lane) * b.storage.Get(lane));
        a.storage.Set(lane, static_cast<T>(ab + c.storage.Get(lane)));
    }
    return a;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Min(const Vec<T, Backend, Lanes>& a,
                                 const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, std::min(a.storage.Get(lane), b.storage.Get(lane)));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Max(const Vec<T, Backend, Lanes>& a,
                                 const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, std::max(a.storage.Get(lane), b.storage.Get(lane)));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Abs(const Vec<T, Backend, Lanes>& a) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    if constexpr (std::is_signed_v<T>) {
        for (int lane = 0; lane < result.lanes; ++lane) {
            const auto value = a.storage.Get(lane);
            result.storage.Set(lane, value < T{} ? static_cast<T>(-value) : value);
        }
    } else {
        result = a;
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator==(const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    using Result = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
    Result mask;
    for (int lane = 0; lane < Result::lanes; ++lane) {
        mask.SetLane(lane, a.storage.Get(lane) == b.storage.Get(lane));
    }
    return mask;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator!=(const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    using Result = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
    Result mask;
    for (int lane = 0; lane < Result::lanes; ++lane) {
        mask.SetLane(lane, a.storage.Get(lane) != b.storage.Get(lane));
    }
    return mask;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator<(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    using Result = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
    Result mask;
    for (int lane = 0; lane < Result::lanes; ++lane) {
        mask.SetLane(lane, a.storage.Get(lane) < b.storage.Get(lane));
    }
    return mask;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator<=(const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    using Result = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
    Result mask;
    for (int lane = 0; lane < Result::lanes; ++lane) {
        mask.SetLane(lane, a.storage.Get(lane) <= b.storage.Get(lane));
    }
    return mask;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator>(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    return b < a;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator>=(const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend> {
    return b <= a;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator&(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, detail::BitwiseAnd(a.storage.Get(lane), b.storage.Get(lane)));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator|(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, detail::BitwiseOr(a.storage.Get(lane), b.storage.Get(lane)));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto operator^(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, detail::BitwiseXor(a.storage.Get(lane), b.storage.Get(lane)));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto AndNot(const Vec<T, Backend, Lanes>& a,
                                    const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        const auto notB = detail::BitwiseNot(b.storage.Get(lane));
        result.storage.Set(lane, detail::BitwiseAnd(a.storage.Get(lane), notB));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Shl(const Vec<T, Backend, Lanes>& value, int amount) noexcept -> Vec<T, Backend, Lanes> {
    static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        result.storage.Set(lane, static_cast<T>(value.storage.Get(lane) << amount));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Shr(const Vec<T, Backend, Lanes>& value, int amount) noexcept -> Vec<T, Backend, Lanes> {
    static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < result.lanes; ++lane) {
        if constexpr (std::is_signed_v<T>) {
            result.storage.Set(lane, static_cast<T>(value.storage.Get(lane) >> amount));
        } else {
            using Unsigned = std::make_unsigned_t<T>;
            const auto shifted = static_cast<Unsigned>(value.storage.Get(lane)) >> amount;
            result.storage.Set(lane, static_cast<T>(shifted));
        }
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto ReduceAdd(const Vec<T, Backend, Lanes>& value) noexcept -> T {
    T total{};
    for (int lane = 0; lane < value.lanes; ++lane) {
        total = static_cast<T>(total + value.storage.Get(lane));
    }
    return total;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto ReduceMin(const Vec<T, Backend, Lanes>& value) noexcept -> T {
    auto minimum = value.storage.Get(0);
    for (int lane = 1; lane < value.lanes; ++lane) {
        minimum = std::min(minimum, value.storage.Get(lane));
    }
    return minimum;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto ReduceMax(const Vec<T, Backend, Lanes>& value) noexcept -> T {
    auto maximum = value.storage.Get(0);
    for (int lane = 1; lane < value.lanes; ++lane) {
        maximum = std::max(maximum, value.storage.Get(lane));
    }
    return maximum;
}

template<class To, class From>
[[nodiscard]] constexpr auto BitCast(const From& from) noexcept -> To {
    static_assert(sizeof(To) == sizeof(From), "BitCast requires identical size.");
    return std::bit_cast<To>(from);
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto operator~(const Mask<Lanes, Backend>& mask) noexcept -> Mask<Lanes, Backend> {
    Mask<Lanes, Backend> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.SetLane(lane, !mask.GetLane(lane));
    }
    return result;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto operator&(const Mask<Lanes, Backend>& lhs,
                                       const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend> {
    Mask<Lanes, Backend> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.SetLane(lane, lhs.GetLane(lane) && rhs.GetLane(lane));
    }
    return result;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto operator|(const Mask<Lanes, Backend>& lhs,
                                       const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend> {
    Mask<Lanes, Backend> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.SetLane(lane, lhs.GetLane(lane) || rhs.GetLane(lane));
    }
    return result;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto operator^(const Mask<Lanes, Backend>& lhs,
                                       const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend> {
    Mask<Lanes, Backend> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.SetLane(lane, lhs.GetLane(lane) != rhs.GetLane(lane));
    }
    return result;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto Any(const Mask<Lanes, Backend>& mask) noexcept -> bool {
    for (int lane = 0; lane < Lanes; ++lane) {
        if (mask.GetLane(lane)) {
            return true;
        }
    }
    return false;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto All(const Mask<Lanes, Backend>& mask) noexcept -> bool {
    for (int lane = 0; lane < Lanes; ++lane) {
        if (!mask.GetLane(lane)) {
            return false;
        }
    }
    return true;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto None(const Mask<Lanes, Backend>& mask) noexcept -> bool {
    return !Any(mask);
}

template<int Lanes, class Backend, class T>
[[nodiscard]] constexpr auto Select(const Mask<Lanes, Backend>& mask,
                                    const Vec<T, Backend, Lanes>& a,
                                    const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.storage.Set(lane, mask.GetLane(lane) ? a.storage.Get(lane) : b.storage.Get(lane));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto Reverse(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes> {
    Vec<T, Backend, Lanes> result;
    for (int lane = 0; lane < Lanes; ++lane) {
        result.storage.Set(lane, value.storage.Get(Lanes - 1 - lane));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto ZipLo(const Vec<T, Backend, Lanes>& a,
                                   const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    static_assert(Lanes % 2 == 0, "ZipLo requires even lane count.");
    Vec<T, Backend, Lanes> result;
    const int half = Lanes / 2;
    for (int lane = 0; lane < half; ++lane) {
        result.storage.Set(2 * lane, a.storage.Get(lane));
        result.storage.Set(2 * lane + 1, b.storage.Get(lane));
    }
    return result;
}

template<class T, class Backend, int Lanes>
[[nodiscard]] constexpr auto ZipHi(const Vec<T, Backend, Lanes>& a,
                                   const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes> {
    static_assert(Lanes % 2 == 0, "ZipHi requires even lane count.");
    Vec<T, Backend, Lanes> result;
    const int half = Lanes / 2;
    for (int lane = 0; lane < half; ++lane) {
        result.storage.Set(2 * lane, a.storage.Get(lane + half));
        result.storage.Set(2 * lane + 1, b.storage.Get(lane + half));
    }
    return result;
}

template<int Lanes, class Backend>
[[nodiscard]] constexpr auto FirstNMask(int count) noexcept -> Mask<Lanes, Backend> {
    Mask<Lanes, Backend> mask;
    const auto clamped = std::clamp(count, 0, Lanes);
    for (int lane = 0; lane < clamped; ++lane) {
        mask.SetLane(lane, true);
    }
    return mask;
}

template<class T, class Backend, int Lanes, class Func>
constexpr void ForEachSimd(T* destination,
                           const T* source,
                           std::size_t count,
                           Func&& functor) noexcept {
    using Vector = Vec<T, Backend, Lanes>;
    constexpr int width = Vector::lanes;
    std::size_t index = 0;
    for (; index + width <= count; index += width) {
        auto loaded = Vector::Load(source + index);
        auto transformed = std::forward<Func>(functor)(loaded);
        transformed.Store(destination + index);
    }
    const auto remainder = static_cast<int>(count - index);
    if (remainder > 0) {
        auto mask = FirstNMask<width, Backend>(remainder);
        auto loaded = Vector::Load(source + index, mask);
        auto transformed = std::forward<Func>(functor)(loaded);
        transformed.Store(destination + index, mask);
    }
}

} // namespace NGIN::SIMD
