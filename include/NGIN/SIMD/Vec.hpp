#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Scalar baseline implementation of the NGIN SIMD façade. Future backends hook
// into the same interface by specializing the underlying storage operations.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "NGIN/SIMD/Tags.hpp"
#include "NGIN/SIMD/detail/BackendTraits.hpp"

namespace NGIN::SIMD
{

    namespace detail
    {

        template<class T, class Backend>
        inline constexpr int ResolveLaneCount(int requested) noexcept
        {
            if (requested > 0)
            {
                return requested;
            }
            if constexpr (std::same_as<Backend, ScalarTag>)
            {
                return BackendTraits<Backend, T>::native_lanes;
            }
            else
            {
                return BackendTraits<Backend, T>::native_lanes;
            }
        }

    }// namespace detail

    template<int Lanes, class Backend>
    struct Mask
    {
        static_assert(Lanes > 0, "Mask must specify lane count explicitly.");

        using backend              = Backend;
        static constexpr int lanes = Lanes;
        using storage_type         = typename detail::BackendTraits<Backend, bool>::template MaskStorage<Lanes>;
        using operations           = typename detail::BackendTraits<Backend, bool>::template Ops<Lanes>;

        constexpr Mask() noexcept = default;
        constexpr explicit Mask(bool value) noexcept
            : storage(value) {}

        [[nodiscard]] constexpr auto GetLane(int index) const noexcept -> bool
        {
            return storage.Get(index);
        }

        constexpr void SetLane(int index, bool value) noexcept
        {
            storage.Set(index, value);
        }

        storage_type storage {};
    };

    template<class T, class Backend, int Lanes>
    struct Vec
    {
        static constexpr int lanes = detail::ResolveLaneCount<T, Backend>(Lanes);
        static_assert(lanes > 0, "Lane count must be resolvable at compile time.");

        using value_type   = T;
        using backend      = Backend;
        using mask_type    = Mask<lanes, Backend>;
        using storage_type = typename detail::BackendTraits<Backend, T>::template Storage<lanes>;
        using operations   = typename detail::BackendTraits<Backend, T>::template Ops<lanes>;

        constexpr Vec() noexcept = default;

        constexpr explicit Vec(T value) noexcept
            : storage(value) {}

        [[nodiscard]] static constexpr auto Iota(T start, T step) noexcept -> Vec
        {
            Vec result;
            T   current = start;
            for (int lane = 0; lane < lanes; ++lane)
            {
                result.storage.Set(lane, current);
                current = static_cast<T>(current + step);
            }
            return result;
        }

        [[nodiscard]] static constexpr auto Load(const T* pointer) noexcept -> Vec
        {
            Vec result;
            result.storage = operations::Load(pointer);
            return result;
        }

        [[nodiscard]] static constexpr auto LoadAligned(const T*             pointer,
                                                        [[maybe_unused]] int align = alignof(T) * lanes) noexcept -> Vec
        {
            Vec result;
            result.storage = operations::LoadAligned(pointer);
            return result;
        }

        [[nodiscard]] static constexpr auto Load(const T*         pointer,
                                                 const mask_type& mask,
                                                 T                fill = T {}) noexcept -> Vec
        {
            Vec result;
            result.storage = operations::LoadMasked(pointer, mask.storage, fill);
            return result;
        }

        constexpr void Store(T* pointer) const noexcept
        {
            operations::Store(storage, pointer);
        }

        constexpr void StoreAligned(T*                   pointer,
                                    [[maybe_unused]] int align = alignof(T) * lanes) const noexcept
        {
            operations::StoreAligned(storage, pointer);
        }

        constexpr void Store(T* pointer, const mask_type& mask) const noexcept
        {
            operations::StoreMasked(storage, pointer, mask.storage);
        }

        template<class IndexVec>
        [[nodiscard]] static constexpr auto Gather(const T* base, const IndexVec& indices) noexcept -> Vec
            requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
        {
            static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
            Vec result;
            result.storage = operations::Gather(base, indices.storage);
            return result;
        }

        template<class IndexVec>
        [[nodiscard]] static constexpr auto Gather(const T*         base,
                                                   const IndexVec&  indices,
                                                   const mask_type& mask,
                                                   T                fill = T {}) noexcept -> Vec
            requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
        {
            static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
            Vec result;
            result.storage = operations::GatherMasked(base, indices.storage, mask.storage, fill);
            return result;
        }

        template<class IndexVec>
        constexpr void Scatter(T* base, const IndexVec& indices) const noexcept
            requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
        {
            static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
            operations::Scatter(storage, base, indices.storage);
        }

        template<class IndexVec>
        constexpr void Scatter(T* base, const IndexVec& indices, const mask_type& mask) const noexcept
            requires SimdVecConcept<IndexVec> && std::is_integral_v<typename IndexVec::value_type>
        {
            static_assert(IndexVec::lanes == lanes, "Index vector lane count must match.");
            operations::ScatterMasked(storage, base, indices.storage, mask.storage);
        }

        [[nodiscard]] constexpr auto GetLane(int index) const noexcept -> T
        {
            return storage.Get(index);
        }

        constexpr void SetLane(int index, T value) noexcept
        {
            storage.Set(index, value);
        }

        storage_type storage {};
    };

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator+(Vec<T, Backend, Lanes>        lhs,
                                           const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes>
    {
        lhs.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Add(lhs.storage, rhs.storage);
        return lhs;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator-(Vec<T, Backend, Lanes>        lhs,
                                           const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes>
    {
        lhs.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Sub(lhs.storage, rhs.storage);
        return lhs;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator*(Vec<T, Backend, Lanes>        lhs,
                                           const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes>
    {
        lhs.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Mul(lhs.storage, rhs.storage);
        return lhs;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator/(Vec<T, Backend, Lanes>        lhs,
                                           const Vec<T, Backend, Lanes>& rhs) noexcept -> Vec<T, Backend, Lanes>
    {
        lhs.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Div(lhs.storage, rhs.storage);
        return lhs;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Fma(Vec<T, Backend, Lanes>        a,
                                     const Vec<T, Backend, Lanes>& b,
                                     const Vec<T, Backend, Lanes>& c) noexcept -> Vec<T, Backend, Lanes>
    {
        a.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Fma(a.storage, b.storage, c.storage);
        return a;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Min(const Vec<T, Backend, Lanes>& a,
                                     const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Min(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Max(const Vec<T, Backend, Lanes>& a,
                                     const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Max(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Abs(const Vec<T, Backend, Lanes>& a) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Abs(a.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator==(const Vec<T, Backend, Lanes>& a,
                                            const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        using MaskType = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
        MaskType mask;
        mask.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::CompareEq(a.storage, b.storage);
        return mask;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator!=(const Vec<T, Backend, Lanes>& a,
                                            const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        using MaskType = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
        MaskType mask;
        mask.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::CompareNe(a.storage, b.storage);
        return mask;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator<(const Vec<T, Backend, Lanes>& a,
                                           const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        using MaskType = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
        MaskType mask;
        mask.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::CompareLt(a.storage, b.storage);
        return mask;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator<=(const Vec<T, Backend, Lanes>& a,
                                            const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        using MaskType = Mask<Vec<T, Backend, Lanes>::lanes, Backend>;
        MaskType mask;
        mask.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::CompareLe(a.storage, b.storage);
        return mask;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator>(const Vec<T, Backend, Lanes>& a,
                                           const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        return b < a;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator>=(const Vec<T, Backend, Lanes>& a,
                                            const Vec<T, Backend, Lanes>& b) noexcept -> Mask<Vec<T, Backend, Lanes>::lanes, Backend>
    {
        return b <= a;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator&(const Vec<T, Backend, Lanes>& a,
                                           const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::BitwiseAnd(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator|(const Vec<T, Backend, Lanes>& a,
                                           const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::BitwiseOr(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto operator^(const Vec<T, Backend, Lanes>& a,
                                           const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::BitwiseXor(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto AndNot(const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::AndNot(a.storage, b.storage);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Shl(const Vec<T, Backend, Lanes>& value, int amount) noexcept -> Vec<T, Backend, Lanes>
    {
        static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Shl(value.storage, amount);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Shr(const Vec<T, Backend, Lanes>& value, int amount) noexcept -> Vec<T, Backend, Lanes>
    {
        static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
        Vec<T, Backend, Lanes> result;
        result.storage = detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::Shr(value.storage, amount);
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto ReduceAdd(const Vec<T, Backend, Lanes>& value) noexcept -> T
    {
        return detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::ReduceAdd(value.storage);
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto ReduceMin(const Vec<T, Backend, Lanes>& value) noexcept -> T
    {
        return detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::ReduceMin(value.storage);
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto ReduceMax(const Vec<T, Backend, Lanes>& value) noexcept -> T
    {
        return detail::BackendTraits<Backend, T>::template Ops<Vec<T, Backend, Lanes>::lanes>::ReduceMax(value.storage);
    }

    template<class To, class From>
    [[nodiscard]] constexpr auto BitCast(const From& from) noexcept -> To
    {
        static_assert(sizeof(To) == sizeof(From), "BitCast requires identical size.");
        return std::bit_cast<To>(from);
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto operator~(const Mask<Lanes, Backend>& mask) noexcept -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> result;
        result.storage = Mask<Lanes, Backend>::operations::MaskNot(mask.storage);
        return result;
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto operator&(const Mask<Lanes, Backend>& lhs,
                                           const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> result;
        result.storage = Mask<Lanes, Backend>::operations::MaskAnd(lhs.storage, rhs.storage);
        return result;
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto operator|(const Mask<Lanes, Backend>& lhs,
                                           const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> result;
        result.storage = Mask<Lanes, Backend>::operations::MaskOr(lhs.storage, rhs.storage);
        return result;
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto operator^(const Mask<Lanes, Backend>& lhs,
                                           const Mask<Lanes, Backend>& rhs) noexcept -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> result;
        result.storage = Mask<Lanes, Backend>::operations::MaskXor(lhs.storage, rhs.storage);
        return result;
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto Any(const Mask<Lanes, Backend>& mask) noexcept -> bool
    {
        return Mask<Lanes, Backend>::operations::MaskAny(mask.storage);
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto All(const Mask<Lanes, Backend>& mask) noexcept -> bool
    {
        return Mask<Lanes, Backend>::operations::MaskAll(mask.storage);
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto None(const Mask<Lanes, Backend>& mask) noexcept -> bool
    {
        return !Mask<Lanes, Backend>::operations::MaskAny(mask.storage);
    }

    template<int Lanes, class Backend, class T>
    [[nodiscard]] constexpr auto Select(const Mask<Lanes, Backend>&   mask,
                                        const Vec<T, Backend, Lanes>& a,
                                        const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        for (int lane = 0; lane < Lanes; ++lane)
        {
            result.storage.Set(lane, mask.GetLane(lane) ? a.storage.Get(lane) : b.storage.Get(lane));
        }
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto Reverse(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        Vec<T, Backend, Lanes> result;
        for (int lane = 0; lane < Lanes; ++lane)
        {
            result.storage.Set(lane, value.storage.Get(Lanes - 1 - lane));
        }
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto ZipLo(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        static_assert(Lanes % 2 == 0, "ZipLo requires even lane count.");
        Vec<T, Backend, Lanes> result;
        const int              half = Lanes / 2;
        for (int lane = 0; lane < half; ++lane)
        {
            result.storage.Set(2 * lane, a.storage.Get(lane));
            result.storage.Set(2 * lane + 1, b.storage.Get(lane));
        }
        return result;
    }

    template<class T, class Backend, int Lanes>
    [[nodiscard]] constexpr auto ZipHi(const Vec<T, Backend, Lanes>& a,
                                       const Vec<T, Backend, Lanes>& b) noexcept -> Vec<T, Backend, Lanes>
    {
        static_assert(Lanes % 2 == 0, "ZipHi requires even lane count.");
        Vec<T, Backend, Lanes> result;
        const int              half = Lanes / 2;
        for (int lane = 0; lane < half; ++lane)
        {
            result.storage.Set(2 * lane, a.storage.Get(lane + half));
            result.storage.Set(2 * lane + 1, b.storage.Get(lane + half));
        }
        return result;
    }

    template<int Lanes, class Backend>
    [[nodiscard]] constexpr auto FirstNMask(int count) noexcept -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> mask;
        const auto           clamped = std::clamp(count, 0, Lanes);
        for (int lane = 0; lane < clamped; ++lane)
        {
            mask.SetLane(lane, true);
        }
        return mask;
    }

    template<class T, class Backend, int Lanes, class Func>
    constexpr void ForEachSimd(T*          destination,
                               const T*    source,
                               std::size_t count,
                               Func&&      functor) noexcept
    {
        using Vector        = Vec<T, Backend, Lanes>;
        constexpr int width = Vector::lanes;
        std::size_t   index = 0;
        for (; index + width <= count; index += width)
        {
            auto loaded      = Vector::Load(source + index);
            auto transformed = std::forward<Func>(functor)(loaded);
            transformed.Store(destination + index);
        }
        const auto remainder = static_cast<int>(count - index);
        if (remainder > 0)
        {
            auto mask        = FirstNMask<width, Backend>(remainder);
            auto loaded      = Vector::Load(source + index, mask);
            auto transformed = std::forward<Func>(functor)(loaded);
            transformed.Store(destination + index, mask);
        }
    }

}// namespace NGIN::SIMD
