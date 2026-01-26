#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Backend trait scaffolding used by the SIMD façade to abstract over native
// register storage, load/store paths, and arithmetic helpers. Scalar backends
// provide element-wise fallbacks; vector backends override selected operations
// with intrinsic implementations.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "NGIN/SIMD/Tags.hpp"

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#include <xmmintrin.h>
#endif

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace NGIN::SIMD::detail
{

    namespace backend_detail
    {
        template<class T, int Lanes>
        struct ArrayStorage
        {
            static_assert(Lanes > 0, "Lane count must be positive.");

            using value_type           = T;
            static constexpr int lanes = Lanes;

            constexpr ArrayStorage() noexcept = default;
            constexpr explicit ArrayStorage(T value) noexcept
            {
                data.fill(value);
            }

            [[nodiscard]] constexpr auto Get(int index) const noexcept -> T
            {
                return data[static_cast<std::size_t>(index)];
            }

            constexpr void Set(int index, T value) noexcept
            {
                data[static_cast<std::size_t>(index)] = value;
            }

            [[nodiscard]] constexpr auto Data() noexcept -> T*
            {
                return data.data();
            }

            [[nodiscard]] constexpr auto Data() const noexcept -> const T*
            {
                return data.data();
            }

            std::array<T, static_cast<std::size_t>(lanes)> data {};
        };

        template<int Lanes>
        struct ArrayMaskStorage
        {
            static_assert(Lanes > 0, "Mask lane count must be positive.");

            constexpr ArrayMaskStorage() noexcept = default;
            constexpr explicit ArrayMaskStorage(bool value) noexcept
            {
                bits.fill(value);
            }

            [[nodiscard]] constexpr auto Get(int index) const noexcept -> bool
            {
                return bits[static_cast<std::size_t>(index)];
            }

            constexpr void Set(int index, bool value) noexcept
            {
                bits[static_cast<std::size_t>(index)] = value;
            }

            [[nodiscard]] constexpr auto Data() noexcept -> bool*
            {
                return bits.data();
            }

            [[nodiscard]] constexpr auto Data() const noexcept -> const bool*
            {
                return bits.data();
            }

            std::array<bool, static_cast<std::size_t>(Lanes)> bits {};
        };

        template<class T>
        [[nodiscard]] constexpr auto BitwiseAnd(T lhs, T rhs) noexcept -> T
        {
            if constexpr (std::is_integral_v<T>)
            {
                return static_cast<T>(lhs & rhs);
            }
            else
            {
                using Bits           = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                                          std::uint32_t, std::uint64_t>;
                const auto leftBits  = std::bit_cast<Bits>(lhs);
                const auto rightBits = std::bit_cast<Bits>(rhs);
                return std::bit_cast<T>(leftBits & rightBits);
            }
        }

        template<class T>
        [[nodiscard]] constexpr auto BitwiseOr(T lhs, T rhs) noexcept -> T
        {
            if constexpr (std::is_integral_v<T>)
            {
                return static_cast<T>(lhs | rhs);
            }
            else
            {
                using Bits           = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                                          std::uint32_t, std::uint64_t>;
                const auto leftBits  = std::bit_cast<Bits>(lhs);
                const auto rightBits = std::bit_cast<Bits>(rhs);
                return std::bit_cast<T>(leftBits | rightBits);
            }
        }

        template<class T>
        [[nodiscard]] constexpr auto BitwiseXor(T lhs, T rhs) noexcept -> T
        {
            if constexpr (std::is_integral_v<T>)
            {
                return static_cast<T>(lhs ^ rhs);
            }
            else
            {
                using Bits           = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                                          std::uint32_t, std::uint64_t>;
                const auto leftBits  = std::bit_cast<Bits>(lhs);
                const auto rightBits = std::bit_cast<Bits>(rhs);
                return std::bit_cast<T>(leftBits ^ rightBits);
            }
        }

        template<class T>
        [[nodiscard]] constexpr auto BitwiseNot(T value) noexcept -> T
        {
            if constexpr (std::is_integral_v<T>)
            {
                return static_cast<T>(~value);
            }
            else
            {
                using Bits             = std::conditional_t<sizeof(T) == sizeof(std::uint32_t),
                                                            std::uint32_t, std::uint64_t>;
                const auto currentBits = std::bit_cast<Bits>(value);
                return std::bit_cast<T>(~currentBits);
            }
        }
    }// namespace backend_detail

    template<class Backend, class T>
    struct BackendTraits;

    template<class T>
    struct BackendTraits<ScalarTag, T>
    {
        static constexpr int native_lanes = 1;

        template<int Lanes>
        using Storage = backend_detail::ArrayStorage<T, Lanes>;

        template<int Lanes>
        using MaskStorage = backend_detail::ArrayMaskStorage<Lanes>;

        template<int Lanes>
        struct Ops
        {
            using StorageType = Storage<Lanes>;
            using MaskType    = MaskStorage<Lanes>;

            static constexpr auto Load(const T* pointer) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, pointer[lane]);
                }
                return result;
            }

            static constexpr auto LoadAligned(const T* pointer) noexcept -> StorageType
            {
                return Load(pointer);
            }

            static constexpr auto LoadMasked(const T*        pointer,
                                             const MaskType& mask,
                                             T               fill) noexcept -> StorageType
            {
                StorageType result(fill);
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (mask.Get(lane))
                    {
                        result.Set(lane, pointer[lane]);
                    }
                }
                return result;
            }

            static constexpr void Store(const StorageType& storage, T* pointer) noexcept
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    pointer[lane] = storage.Get(lane);
                }
            }

            static constexpr void StoreAligned(const StorageType& storage, T* pointer) noexcept
            {
                Store(storage, pointer);
            }

            static constexpr void StoreMasked(const StorageType& storage,
                                              T*                 pointer,
                                              const MaskType&    mask) noexcept
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (mask.Get(lane))
                    {
                        pointer[lane] = storage.Get(lane);
                    }
                }
            }

            static constexpr auto Add(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, static_cast<T>(lhs.Get(lane) + rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Sub(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, static_cast<T>(lhs.Get(lane) - rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Mul(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, static_cast<T>(lhs.Get(lane) * rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Div(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, static_cast<T>(lhs.Get(lane) / rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Fma(const StorageType& a,
                                      const StorageType& b,
                                      const StorageType& c) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    const auto product = static_cast<T>(a.Get(lane) * b.Get(lane));
                    result.Set(lane, static_cast<T>(product + c.Get(lane)));
                }
                return result;
            }

            static constexpr auto Min(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, std::min(lhs.Get(lane), rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Max(const StorageType& lhs,
                                      const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, std::max(lhs.Get(lane), rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto Abs(const StorageType& value) noexcept -> StorageType
            {
                if constexpr (std::is_signed_v<T>)
                {
                    StorageType result;
                    for (int lane = 0; lane < Lanes; ++lane)
                    {
                        const auto current = value.Get(lane);
                        result.Set(lane, current < T {} ? static_cast<T>(-current) : current);
                    }
                    return result;
                }
                else
                {
                    return value;
                }
            }

            static constexpr auto BitwiseAnd(const StorageType& lhs,
                                             const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, backend_detail::BitwiseAnd(lhs.Get(lane), rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto BitwiseOr(const StorageType& lhs,
                                            const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, backend_detail::BitwiseOr(lhs.Get(lane), rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto BitwiseXor(const StorageType& lhs,
                                             const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, backend_detail::BitwiseXor(lhs.Get(lane), rhs.Get(lane)));
                }
                return result;
            }

            static constexpr auto AndNot(const StorageType& lhs,
                                         const StorageType& rhs) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    const auto notRhs = backend_detail::BitwiseNot(rhs.Get(lane));
                    result.Set(lane, backend_detail::BitwiseAnd(lhs.Get(lane), notRhs));
                }
                return result;
            }

            static constexpr auto Shl(const StorageType& value, int amount) noexcept -> StorageType
            {
                static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, static_cast<T>(value.Get(lane) << amount));
                }
                return result;
            }

            static constexpr auto Shr(const StorageType& value, int amount) noexcept -> StorageType
            {
                static_assert(std::is_integral_v<T>, "Shift operations require integral element type.");
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if constexpr (std::is_signed_v<T>)
                    {
                        result.Set(lane, static_cast<T>(value.Get(lane) >> amount));
                    }
                    else
                    {
                        using Unsigned     = std::make_unsigned_t<T>;
                        const auto shifted = static_cast<Unsigned>(value.Get(lane)) >> amount;
                        result.Set(lane, static_cast<T>(shifted));
                    }
                }
                return result;
            }

            static constexpr auto ReduceAdd(const StorageType& value) noexcept -> T
            {
                T total {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    total = static_cast<T>(total + value.Get(lane));
                }
                return total;
            }

            static constexpr auto ReduceMin(const StorageType& value) noexcept -> T
            {
                T minimum = value.Get(0);
                for (int lane = 1; lane < Lanes; ++lane)
                {
                    minimum = std::min(minimum, value.Get(lane));
                }
                return minimum;
            }

            static constexpr auto ReduceMax(const StorageType& value) noexcept -> T
            {
                T maximum = value.Get(0);
                for (int lane = 1; lane < Lanes; ++lane)
                {
                    maximum = std::max(maximum, value.Get(lane));
                }
                return maximum;
            }

            template<class IndexStorage>
            static constexpr auto Gather(const T* base, const IndexStorage& indices) noexcept -> StorageType
            {
                StorageType result;
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    const auto offset = static_cast<std::size_t>(indices.Get(lane));
                    result.Set(lane, base[offset]);
                }
                return result;
            }

            template<class IndexStorage>
            static constexpr auto GatherMasked(const T*            base,
                                               const IndexStorage& indices,
                                               const MaskType&     mask,
                                               T                   fill) noexcept -> StorageType
            {
                StorageType result(fill);
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (mask.Get(lane))
                    {
                        const auto offset = static_cast<std::size_t>(indices.Get(lane));
                        result.Set(lane, base[offset]);
                    }
                }
                return result;
            }

            template<class IndexStorage>
            static constexpr void Scatter(const StorageType&  values,
                                          T*                  base,
                                          const IndexStorage& indices) noexcept
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    const auto offset = static_cast<std::size_t>(indices.Get(lane));
                    base[offset]      = values.Get(lane);
                }
            }

            template<class IndexStorage>
            static constexpr void ScatterMasked(const StorageType&  values,
                                                T*                  base,
                                                const IndexStorage& indices,
                                                const MaskType&     mask) noexcept
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (mask.Get(lane))
                    {
                        const auto offset = static_cast<std::size_t>(indices.Get(lane));
                        base[offset]      = values.Get(lane);
                    }
                }
            }

            static constexpr auto CompareEq(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) == rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto CompareNe(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) != rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto CompareLt(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) < rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto CompareLe(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) <= rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto CompareGt(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) > rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto CompareGe(const StorageType& lhs, const StorageType& rhs) noexcept -> MaskType
            {
                MaskType mask {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    mask.Set(lane, lhs.Get(lane) >= rhs.Get(lane));
                }
                return mask;
            }

            static constexpr auto MaskNot(const MaskType& mask) noexcept -> MaskType
            {
                MaskType result {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, !mask.Get(lane));
                }
                return result;
            }

            static constexpr auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
            {
                MaskType result {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, lhs.Get(lane) && rhs.Get(lane));
                }
                return result;
            }

            static constexpr auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
            {
                MaskType result {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, lhs.Get(lane) || rhs.Get(lane));
                }
                return result;
            }

            static constexpr auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
            {
                MaskType result {};
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    result.Set(lane, lhs.Get(lane) != rhs.Get(lane));
                }
                return result;
            }

            static constexpr auto MaskAny(const MaskType& mask) noexcept -> bool
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (mask.Get(lane))
                    {
                        return true;
                    }
                }
                return false;
            }

            static constexpr auto MaskAll(const MaskType& mask) noexcept -> bool
            {
                for (int lane = 0; lane < Lanes; ++lane)
                {
                    if (!mask.Get(lane))
                    {
                        return false;
                    }
                }
                return true;
            }
        };
    };

    template<class Backend, class T>
    struct BackendTraits : BackendTraits<ScalarTag, T>
    {
        static constexpr int native_lanes = BackendTraits<ScalarTag, T>::native_lanes;
    };

#if defined(__SSE2__)
    template<>
    struct BackendTraits<SSE2Tag, float> : BackendTraits<ScalarTag, float>
    {
        using Base = BackendTraits<ScalarTag, float>;

        static constexpr int native_lanes = 4;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<SSE2Tag, bool> : BackendTraits<SSE2Tag, float>
    {
    };

    template<>
    struct BackendTraits<SSE2Tag, std::int32_t> : BackendTraits<ScalarTag, std::int32_t>
    {
        using Base = BackendTraits<ScalarTag, std::int32_t>;

        static constexpr int native_lanes = 4;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<SSE2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<SSE2Tag, std::int32_t>::Ops<BackendTraits<SSE2Tag, std::int32_t>::native_lanes>
        : BackendTraits<SSE2Tag, std::int32_t>::Base::template Ops<BackendTraits<SSE2Tag, std::int32_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<SSE2Tag, std::int32_t>::Base::template Ops<BackendTraits<SSE2Tag, std::int32_t>::native_lanes>;
        using Storage  = BackendTraits<SSE2Tag, std::int32_t>::template Storage<BackendTraits<SSE2Tag, std::int32_t>::native_lanes>;
        using MaskType = BackendTraits<SSE2Tag, bool>::template MaskStorage<BackendTraits<SSE2Tag, std::int32_t>::native_lanes>;

        static inline auto MaskFromRegister(__m128i reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm_movemask_ps(_mm_castsi128_ps(reg));
            for (int lane = 0; lane < BackendTraits<SSE2Tag, std::int32_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m128i
        {
            alignas(16) std::int32_t bits[BackendTraits<SSE2Tag, std::int32_t>::native_lanes];
            for (int lane = 0; lane < BackendTraits<SSE2Tag, std::int32_t>::native_lanes; ++lane)
            {
                bits[lane] = mask.Get(lane) ? -1 : 0;
            }
            return _mm_load_si128(reinterpret_cast<const __m128i*>(bits));
        }

        static constexpr auto Load(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_store_si128(reinterpret_cast<__m128i*>(result.Data()),
                            _mm_load_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(pointer),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm_store_si128(reinterpret_cast<__m128i*>(pointer),
                            _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i sum = _mm_add_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                              _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i diff = _mm_sub_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            return BaseOps::Mul(lhs, rhs);
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            return BaseOps::Div(lhs, rhs);
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                 _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_andnot_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())),
                                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmplt_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i lt = _mm_cmplt_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i eq = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromRegister(_mm_or_si128(lt, eq));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmpgt_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i gt = _mm_cmpgt_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i eq = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromRegister(_mm_or_si128(gt, eq));
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_xor_si128(MakeMask(mask), _mm_set1_epi32(-1)));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_and_si128(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_or_si128(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_xor_si128(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_ps(_mm_castsi128_ps(MakeMask(mask))) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_ps(_mm_castsi128_ps(MakeMask(mask))) == 0xF;
        }
    };

    template<>
    struct BackendTraits<SSE2Tag, std::uint8_t> : BackendTraits<ScalarTag, std::uint8_t>
    {
        using Base = BackendTraits<ScalarTag, std::uint8_t>;

        static constexpr int native_lanes = 16;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<SSE2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<SSE2Tag, std::int8_t> : BackendTraits<ScalarTag, std::int8_t>
    {
        using Base = BackendTraits<ScalarTag, std::int8_t>;

        static constexpr int native_lanes = 16;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<SSE2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<SSE2Tag, std::uint8_t>::Ops<BackendTraits<SSE2Tag, std::uint8_t>::native_lanes>
        : BackendTraits<SSE2Tag, std::uint8_t>::Base::template Ops<BackendTraits<SSE2Tag, std::uint8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<SSE2Tag, std::uint8_t>::Base::template Ops<BackendTraits<SSE2Tag, std::uint8_t>::native_lanes>;
        using Storage  = BackendTraits<SSE2Tag, std::uint8_t>::template Storage<BackendTraits<SSE2Tag, std::uint8_t>::native_lanes>;
        using MaskType = BackendTraits<SSE2Tag, bool>::template MaskStorage<BackendTraits<SSE2Tag, std::uint8_t>::native_lanes>;

        static inline auto MaskFromBitmask(int bitmask) noexcept -> MaskType
        {
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<SSE2Tag, std::uint8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_store_si128(reinterpret_cast<__m128i*>(result.Data()),
                            _mm_load_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(pointer),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            _mm_store_si128(reinterpret_cast<__m128i*>(pointer),
                            _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                 _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_andnot_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())),
                                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(cmp));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i bias    = _mm_set1_epi8(static_cast<char>(0x80));
            const __m128i lhsAdj  = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())), bias);
            const __m128i rhsAdj  = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())), bias);
            const __m128i cmpMask = _mm_cmplt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm_movemask_epi8(cmpMask));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i eqMask = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i bias   = _mm_set1_epi8(static_cast<char>(0x80));
            const __m128i lhsAdj = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())), bias);
            const __m128i rhsAdj = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())), bias);
            const __m128i ltMask = _mm_cmplt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm_movemask_epi8(_mm_or_si128(eqMask, ltMask)));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i bias    = _mm_set1_epi8(static_cast<char>(0x80));
            const __m128i lhsAdj  = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())), bias);
            const __m128i rhsAdj  = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())), bias);
            const __m128i cmpMask = _mm_cmpgt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm_movemask_epi8(cmpMask));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i eqMask = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i bias   = _mm_set1_epi8(static_cast<char>(0x80));
            const __m128i lhsAdj = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())), bias);
            const __m128i rhsAdj = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())), bias);
            const __m128i gtMask = _mm_cmpgt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm_movemask_epi8(_mm_or_si128(eqMask, gtMask)));
        }
    };

    template<>
    struct BackendTraits<SSE2Tag, std::int8_t>::Ops<BackendTraits<SSE2Tag, std::int8_t>::native_lanes>
        : BackendTraits<SSE2Tag, std::int8_t>::Base::template Ops<BackendTraits<SSE2Tag, std::int8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<SSE2Tag, std::int8_t>::Base::template Ops<BackendTraits<SSE2Tag, std::int8_t>::native_lanes>;
        using Storage  = BackendTraits<SSE2Tag, std::int8_t>::template Storage<BackendTraits<SSE2Tag, std::int8_t>::native_lanes>;
        using MaskType = BackendTraits<SSE2Tag, bool>::template MaskStorage<BackendTraits<SSE2Tag, std::int8_t>::native_lanes>;

        static inline auto MaskFromBitmask(int bitmask) noexcept -> MaskType
        {
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<SSE2Tag, std::int8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_store_si128(reinterpret_cast<__m128i*>(result.Data()),
                            _mm_load_si128(reinterpret_cast<const __m128i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int8_t* pointer) noexcept
        {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(pointer),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int8_t* pointer) noexcept
        {
            _mm_store_si128(reinterpret_cast<__m128i*>(pointer),
                            _mm_loadu_si128(reinterpret_cast<const __m128i*>(storage.Data())));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                 _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128i blended = _mm_andnot_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())),
                                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(cmp));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmplt_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(cmp));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i eqMask = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i ltMask = _mm_cmplt_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(_mm_or_si128(eqMask, ltMask)));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i cmp = _mm_cmpgt_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(cmp));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128i eqMask = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            const __m128i gtMask = _mm_cmpgt_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs.Data())),
                                                  _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs.Data())));
            return MaskFromBitmask(_mm_movemask_epi8(_mm_or_si128(eqMask, gtMask)));
        }
    };

    template<>
    struct BackendTraits<SSE2Tag, float>::Ops<BackendTraits<SSE2Tag, float>::native_lanes>
        : BackendTraits<SSE2Tag, float>::Base::template Ops<BackendTraits<SSE2Tag, float>::native_lanes>
    {
        using BaseOps  = BackendTraits<SSE2Tag, float>::Base::template Ops<BackendTraits<SSE2Tag, float>::native_lanes>;
        using Storage  = BackendTraits<SSE2Tag, float>::template Storage<BackendTraits<SSE2Tag, float>::native_lanes>;
        using MaskType = BackendTraits<SSE2Tag, float>::template MaskStorage<BackendTraits<SSE2Tag, float>::native_lanes>;

        static inline auto MaskFromRegister(__m128 reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm_movemask_ps(reg);
            for (int lane = 0; lane < BackendTraits<SSE2Tag, float>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m128
        {
            return _mm_castsi128_ps(
                    _mm_set_epi32(mask.Get(3) ? -1 : 0,
                                  mask.Get(2) ? -1 : 0,
                                  mask.Get(1) ? -1 : 0,
                                  mask.Get(0) ? -1 : 0));
        }

        static constexpr auto Load(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_storeu_ps(result.Data(), _mm_loadu_ps(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_store_ps(result.Data(), _mm_load_ps(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, float* pointer) noexcept
        {
            const __m128 reg = _mm_loadu_ps(storage.Data());
            _mm_storeu_ps(pointer, reg);
        }

        static constexpr void StoreAligned(const Storage& storage, float* pointer) noexcept
        {
            const __m128 reg = _mm_loadu_ps(storage.Data());
            _mm_store_ps(pointer, reg);
        }

        static auto LoadMasked(const float* pointer, const MaskType& mask, float fill) noexcept -> Storage
        {
            const __m128 maskVec = MakeMask(mask);
            const __m128 loadVec = _mm_loadu_ps(pointer);
            const __m128 fillVec = _mm_set1_ps(fill);
            const __m128 blended = _mm_or_ps(_mm_and_ps(maskVec, loadVec), _mm_andnot_ps(maskVec, fillVec));
            Storage      result;
            _mm_storeu_ps(result.Data(), blended);
            return result;
        }

        static void StoreMasked(const Storage& storage, float* pointer, const MaskType& mask) noexcept
        {
            const __m128 maskVec = MakeMask(mask);
            const __m128 srcVec  = _mm_loadu_ps(storage.Data());
            const __m128 destVec = _mm_loadu_ps(pointer);
            const __m128 blended = _mm_or_ps(_mm_and_ps(maskVec, srcVec), _mm_andnot_ps(maskVec, destVec));
            _mm_storeu_ps(pointer, blended);
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 sum = _mm_add_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 diff = _mm_sub_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 product = _mm_mul_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), product);
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 quotient = _mm_div_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), quotient);
            return result;
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
            Storage      result;
            const __m128 mul = _mm_mul_ps(_mm_loadu_ps(a.Data()), _mm_loadu_ps(b.Data()));
            const __m128 sum = _mm_add_ps(mul, _mm_loadu_ps(c.Data()));
            _mm_storeu_ps(result.Data(), sum);
            return result;
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 minimum = _mm_min_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), minimum);
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 maximum = _mm_max_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), maximum);
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage      result;
            const __m128 mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
            const __m128 magn = _mm_and_ps(_mm_loadu_ps(value.Data()), mask);
            _mm_storeu_ps(result.Data(), magn);
            return result;
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 combined = _mm_and_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 combined = _mm_or_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 combined = _mm_xor_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            _mm_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m128 combined = _mm_andnot_ps(_mm_loadu_ps(rhs.Data()), _mm_loadu_ps(lhs.Data()));
            _mm_storeu_ps(result.Data(), combined);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128 cmp = _mm_cmpeq_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128 cmp = _mm_cmplt_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128 cmp = _mm_cmple_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128 cmp = _mm_cmpgt_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128 cmp = _mm_cmpge_ps(_mm_loadu_ps(lhs.Data()), _mm_loadu_ps(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            const __m128 allOnes = _mm_castsi128_ps(_mm_set1_epi32(-1));
            return MaskFromRegister(_mm_xor_ps(MakeMask(mask), allOnes));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_and_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_or_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_xor_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_ps(MakeMask(mask)) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_ps(MakeMask(mask)) == 0xF;
        }
    };

    template<>
    struct BackendTraits<SSE2Tag, double> : BackendTraits<ScalarTag, double>
    {
        using Base = BackendTraits<ScalarTag, double>;

        static constexpr int native_lanes = 2;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<SSE2Tag, double>::Ops<BackendTraits<SSE2Tag, double>::native_lanes>
        : BackendTraits<SSE2Tag, double>::Base::template Ops<BackendTraits<SSE2Tag, double>::native_lanes>
    {
        using Storage  = BackendTraits<SSE2Tag, double>::template Storage<BackendTraits<SSE2Tag, double>::native_lanes>;
        using MaskType = BackendTraits<SSE2Tag, double>::template MaskStorage<BackendTraits<SSE2Tag, double>::native_lanes>;

        static inline auto MaskFromRegister(__m128d reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm_movemask_pd(reg);
            for (int lane = 0; lane < BackendTraits<SSE2Tag, double>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m128d
        {
            return _mm_castsi128_pd(
                    _mm_set_epi64x(mask.Get(1) ? -1LL : 0LL,
                                   mask.Get(0) ? -1LL : 0LL));
        }

        static constexpr auto Load(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_storeu_pd(result.Data(), _mm_loadu_pd(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm_store_pd(result.Data(), _mm_load_pd(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, double* pointer) noexcept
        {
            const __m128d reg = _mm_loadu_pd(storage.Data());
            _mm_storeu_pd(pointer, reg);
        }

        static constexpr void StoreAligned(const Storage& storage, double* pointer) noexcept
        {
            const __m128d reg = _mm_loadu_pd(storage.Data());
            _mm_store_pd(pointer, reg);
        }

        static auto LoadMasked(const double* pointer, const MaskType& mask, double fill) noexcept -> Storage
        {
            const __m128d maskVec = MakeMask(mask);
            const __m128d loadVec = _mm_loadu_pd(pointer);
            const __m128d fillVec = _mm_set1_pd(fill);
            const __m128d blended = _mm_or_pd(_mm_and_pd(maskVec, loadVec), _mm_andnot_pd(maskVec, fillVec));
            Storage       result;
            _mm_storeu_pd(result.Data(), blended);
            return result;
        }

        static void StoreMasked(const Storage& storage, double* pointer, const MaskType& mask) noexcept
        {
            const __m128d maskVec = MakeMask(mask);
            const __m128d srcVec  = _mm_loadu_pd(storage.Data());
            const __m128d destVec = _mm_loadu_pd(pointer);
            const __m128d blended = _mm_or_pd(_mm_and_pd(maskVec, srcVec), _mm_andnot_pd(maskVec, destVec));
            _mm_storeu_pd(pointer, blended);
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d sum = _mm_add_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d diff = _mm_sub_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d prod = _mm_mul_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), prod);
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d quot = _mm_div_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), quot);
            return result;
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
            Storage       result;
            const __m128d mul = _mm_mul_pd(_mm_loadu_pd(a.Data()), _mm_loadu_pd(b.Data()));
            const __m128d sum = _mm_add_pd(mul, _mm_loadu_pd(c.Data()));
            _mm_storeu_pd(result.Data(), sum);
            return result;
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d minimum = _mm_min_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), minimum);
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m128d maximum = _mm_max_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            _mm_storeu_pd(result.Data(), maximum);
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage       result;
            const __m128d mask = _mm_castsi128_pd(_mm_set1_epi64x(0x7FFF'FFFF'FFFF'FFFFLL));
            const __m128d magn = _mm_and_pd(_mm_loadu_pd(value.Data()), mask);
            _mm_storeu_pd(result.Data(), magn);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128d cmp = _mm_cmpeq_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128d cmp = _mm_cmplt_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128d cmp = _mm_cmple_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128d cmp = _mm_cmpgt_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m128d cmp = _mm_cmpge_pd(_mm_loadu_pd(lhs.Data()), _mm_loadu_pd(rhs.Data()));
            return MaskFromRegister(cmp);
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            const __m128d allOnes = _mm_castsi128_pd(_mm_set1_epi64x(-1LL));
            return MaskFromRegister(_mm_xor_pd(MakeMask(mask), allOnes));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_and_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_or_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm_xor_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_pd(MakeMask(mask)) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm_movemask_pd(MakeMask(mask)) == 0x3;
        }
    };
#endif// defined(__SSE2__)

#if defined(__AVX2__)
    template<>
    struct BackendTraits<AVX2Tag, float> : BackendTraits<SSE2Tag, float>
    {
        using Base = BackendTraits<SSE2Tag, float>;

        static constexpr int native_lanes = 8;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, bool> : BackendTraits<AVX2Tag, float>
    {
    };

    template<>
    struct BackendTraits<AVX2Tag, std::uint8_t> : BackendTraits<SSE2Tag, std::uint8_t>
    {
        using Base = BackendTraits<SSE2Tag, std::uint8_t>;

        static constexpr int native_lanes = 32;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<AVX2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, std::int8_t> : BackendTraits<SSE2Tag, std::int8_t>
    {
        using Base = BackendTraits<SSE2Tag, std::int8_t>;

        static constexpr int native_lanes = 32;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<AVX2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, std::uint8_t>::Ops<BackendTraits<AVX2Tag, std::uint8_t>::native_lanes>
        : BackendTraits<AVX2Tag, std::uint8_t>::Base::template Ops<BackendTraits<AVX2Tag, std::uint8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<AVX2Tag, std::uint8_t>::Base::template Ops<BackendTraits<AVX2Tag, std::uint8_t>::native_lanes>;
        using Storage  = BackendTraits<AVX2Tag, std::uint8_t>::template Storage<BackendTraits<AVX2Tag, std::uint8_t>::native_lanes>;
        using MaskType = BackendTraits<AVX2Tag, bool>::template MaskStorage<BackendTraits<AVX2Tag, std::uint8_t>::native_lanes>;

        static inline auto MaskFromBitmask(int bitmask) noexcept -> MaskType
        {
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<AVX2Tag, std::uint8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_load_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(pointer),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            _mm256_store_si256(reinterpret_cast<__m256i*>(pointer),
                               _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_or_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_andnot_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(cmp));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i bias    = _mm256_set1_epi8(static_cast<char>(0x80));
            const __m256i lhsAdj  = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())), bias);
            const __m256i rhsAdj  = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())), bias);
            const __m256i cmpMask = _mm256_cmpgt_epi8(rhsAdj, lhsAdj);
            return MaskFromBitmask(_mm256_movemask_epi8(cmpMask));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i eqMask = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            const __m256i bias   = _mm256_set1_epi8(static_cast<char>(0x80));
            const __m256i lhsAdj = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())), bias);
            const __m256i rhsAdj = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())), bias);
            const __m256i ltMask = _mm256_cmpgt_epi8(rhsAdj, lhsAdj);
            return MaskFromBitmask(_mm256_movemask_epi8(_mm256_or_si256(eqMask, ltMask)));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i bias    = _mm256_set1_epi8(static_cast<char>(0x80));
            const __m256i lhsAdj  = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())), bias);
            const __m256i rhsAdj  = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())), bias);
            const __m256i cmpMask = _mm256_cmpgt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm256_movemask_epi8(cmpMask));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i eqMask = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            const __m256i bias   = _mm256_set1_epi8(static_cast<char>(0x80));
            const __m256i lhsAdj = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())), bias);
            const __m256i rhsAdj = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())), bias);
            const __m256i gtMask = _mm256_cmpgt_epi8(lhsAdj, rhsAdj);
            return MaskFromBitmask(_mm256_movemask_epi8(_mm256_or_si256(eqMask, gtMask)));
        }
    };

    template<>
    struct BackendTraits<AVX2Tag, std::int8_t>::Ops<BackendTraits<AVX2Tag, std::int8_t>::native_lanes>
        : BackendTraits<AVX2Tag, std::int8_t>::Base::template Ops<BackendTraits<AVX2Tag, std::int8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<AVX2Tag, std::int8_t>::Base::template Ops<BackendTraits<AVX2Tag, std::int8_t>::native_lanes>;
        using Storage  = BackendTraits<AVX2Tag, std::int8_t>::template Storage<BackendTraits<AVX2Tag, std::int8_t>::native_lanes>;
        using MaskType = BackendTraits<AVX2Tag, bool>::template MaskStorage<BackendTraits<AVX2Tag, std::int8_t>::native_lanes>;

        static inline auto MaskFromBitmask(int bitmask) noexcept -> MaskType
        {
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<AVX2Tag, std::int8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_load_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int8_t* pointer) noexcept
        {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(pointer),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int8_t* pointer) noexcept
        {
            _mm256_store_si256(reinterpret_cast<__m256i*>(pointer),
                               _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_or_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_andnot_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(cmp));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpgt_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(cmp));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i eqMask = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            const __m256i ltMask = _mm256_cmpgt_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(_mm256_or_si256(eqMask, ltMask)));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpgt_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(cmp));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i eqMask = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            const __m256i gtMask = _mm256_cmpgt_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromBitmask(_mm256_movemask_epi8(_mm256_or_si256(eqMask, gtMask)));
        }
    };
#endif// defined(__AVX2__)

#if defined(__ARM_NEON)

    template<>
    struct BackendTraits<AVX2Tag, float>::Ops<BackendTraits<AVX2Tag, float>::native_lanes>
        : BackendTraits<AVX2Tag, float>::Base::template Ops<BackendTraits<AVX2Tag, float>::native_lanes>
    {
        using Storage  = BackendTraits<AVX2Tag, float>::template Storage<BackendTraits<AVX2Tag, float>::native_lanes>;
        using MaskType = BackendTraits<AVX2Tag, float>::template MaskStorage<BackendTraits<AVX2Tag, float>::native_lanes>;

        static inline auto MaskFromRegister(__m256 reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm256_movemask_ps(reg);
            for (int lane = 0; lane < BackendTraits<AVX2Tag, float>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m256
        {
            return _mm256_castsi256_ps(
                    _mm256_set_epi32(mask.Get(7) ? -1 : 0,
                                     mask.Get(6) ? -1 : 0,
                                     mask.Get(5) ? -1 : 0,
                                     mask.Get(4) ? -1 : 0,
                                     mask.Get(3) ? -1 : 0,
                                     mask.Get(2) ? -1 : 0,
                                     mask.Get(1) ? -1 : 0,
                                     mask.Get(0) ? -1 : 0));
        }

        static constexpr auto Load(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_ps(result.Data(), _mm256_loadu_ps(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_store_ps(result.Data(), _mm256_load_ps(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, float* pointer) noexcept
        {
            const __m256 reg = _mm256_loadu_ps(storage.Data());
            _mm256_storeu_ps(pointer, reg);
        }

        static constexpr void StoreAligned(const Storage& storage, float* pointer) noexcept
        {
            const __m256 reg = _mm256_loadu_ps(storage.Data());
            _mm256_store_ps(pointer, reg);
        }

        static auto LoadMasked(const float* pointer, const MaskType& mask, float fill) noexcept -> Storage
        {
            const __m256 maskVec = MakeMask(mask);
            const __m256 loadVec = _mm256_loadu_ps(pointer);
            const __m256 fillVec = _mm256_set1_ps(fill);
            const __m256 blended = _mm256_or_ps(_mm256_and_ps(maskVec, loadVec), _mm256_andnot_ps(maskVec, fillVec));
            Storage      result;
            _mm256_storeu_ps(result.Data(), blended);
            return result;
        }

        static void StoreMasked(const Storage& storage, float* pointer, const MaskType& mask) noexcept
        {
            const __m256 maskVec = MakeMask(mask);
            const __m256 srcVec  = _mm256_loadu_ps(storage.Data());
            const __m256 destVec = _mm256_loadu_ps(pointer);
            const __m256 blended = _mm256_or_ps(_mm256_and_ps(maskVec, srcVec), _mm256_andnot_ps(maskVec, destVec));
            _mm256_storeu_ps(pointer, blended);
        }

        template<class IndexStorage>
        static auto Gather(const float* base, const IndexStorage& indices) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4,
                          "AVX2 float gather requires 32-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            Storage      result;
            const __m256 gathered = _mm256_i32gather_ps(base, indexVec, sizeof(float));
            _mm256_storeu_ps(result.Data(), gathered);
            return result;
        }

        template<class IndexStorage>
        static auto GatherMasked(const float*        base,
                                 const IndexStorage& indices,
                                 const MaskType&     mask,
                                 float               fill) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4,
                          "AVX2 float gather requires 32-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            const __m256 maskVec  = MakeMask(mask);
            const __m256 fillVec  = _mm256_set1_ps(fill);
            const __m256 gathered = _mm256_mask_i32gather_ps(fillVec, base, indexVec, maskVec, sizeof(float));
            Storage      result;
            _mm256_storeu_ps(result.Data(), gathered);
            return result;
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 sum = _mm256_add_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 diff = _mm256_sub_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 prod = _mm256_mul_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), prod);
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 quot = _mm256_div_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), quot);
            return result;
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
            Storage result;
#if defined(__FMA__)
            const __m256 sum = _mm256_fmadd_ps(_mm256_loadu_ps(a.Data()), _mm256_loadu_ps(b.Data()), _mm256_loadu_ps(c.Data()));
#else
            const __m256 mul = _mm256_mul_ps(_mm256_loadu_ps(a.Data()), _mm256_loadu_ps(b.Data()));
            const __m256 sum = _mm256_add_ps(mul, _mm256_loadu_ps(c.Data()));
#endif
            _mm256_storeu_ps(result.Data(), sum);
            return result;
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 minimum = _mm256_min_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), minimum);
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 maximum = _mm256_max_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), maximum);
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage      result;
            const __m256 mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
            const __m256 magn = _mm256_and_ps(_mm256_loadu_ps(value.Data()), mask);
            _mm256_storeu_ps(result.Data(), magn);
            return result;
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 combined = _mm256_and_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 combined = _mm256_or_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 combined = _mm256_xor_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()));
            _mm256_storeu_ps(result.Data(), combined);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage      result;
            const __m256 combined = _mm256_andnot_ps(_mm256_loadu_ps(rhs.Data()), _mm256_loadu_ps(lhs.Data()));
            _mm256_storeu_ps(result.Data(), combined);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256 cmp = _mm256_cmp_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()), _CMP_EQ_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256 cmp = _mm256_cmp_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()), _CMP_LT_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256 cmp = _mm256_cmp_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()), _CMP_LE_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256 cmp = _mm256_cmp_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()), _CMP_GT_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256 cmp = _mm256_cmp_ps(_mm256_loadu_ps(lhs.Data()), _mm256_loadu_ps(rhs.Data()), _CMP_GE_OQ);
            return MaskFromRegister(cmp);
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            const __m256 allOnes = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
            return MaskFromRegister(_mm256_xor_ps(MakeMask(mask), allOnes));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_and_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_or_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_xor_ps(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_ps(MakeMask(mask)) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_ps(MakeMask(mask)) == 0xFF;
        }
    };

    template<>
    struct BackendTraits<AVX2Tag, double> : BackendTraits<SSE2Tag, double>
    {
        using Base = BackendTraits<SSE2Tag, double>;

        static constexpr int native_lanes = 4;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, double>::Ops<BackendTraits<AVX2Tag, double>::native_lanes>
        : BackendTraits<AVX2Tag, double>::Base::template Ops<BackendTraits<AVX2Tag, double>::native_lanes>
    {
        using Storage  = BackendTraits<AVX2Tag, double>::template Storage<BackendTraits<AVX2Tag, double>::native_lanes>;
        using MaskType = BackendTraits<AVX2Tag, double>::template MaskStorage<BackendTraits<AVX2Tag, double>::native_lanes>;

        static inline auto MaskFromRegister(__m256d reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm256_movemask_pd(reg);
            for (int lane = 0; lane < BackendTraits<AVX2Tag, double>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m256d
        {
            return _mm256_castsi256_pd(
                    _mm256_set_epi64x(mask.Get(3) ? -1LL : 0LL,
                                      mask.Get(2) ? -1LL : 0LL,
                                      mask.Get(1) ? -1LL : 0LL,
                                      mask.Get(0) ? -1LL : 0LL));
        }

        static constexpr auto Load(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_pd(result.Data(), _mm256_loadu_pd(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_store_pd(result.Data(), _mm256_load_pd(pointer));
            return result;
        }

        static auto LoadMasked(const double* pointer, const MaskType& mask, double fill) noexcept -> Storage
        {
            const __m256d maskVec = MakeMask(mask);
            const __m256d loadVec = _mm256_loadu_pd(pointer);
            const __m256d fillVec = _mm256_set1_pd(fill);
            const __m256d blended = _mm256_or_pd(_mm256_and_pd(maskVec, loadVec), _mm256_andnot_pd(maskVec, fillVec));
            Storage       result;
            _mm256_storeu_pd(result.Data(), blended);
            return result;
        }

        static void StoreMasked(const Storage& storage, double* pointer, const MaskType& mask) noexcept
        {
            const __m256d maskVec = MakeMask(mask);
            const __m256d srcVec  = _mm256_loadu_pd(storage.Data());
            const __m256d destVec = _mm256_loadu_pd(pointer);
            const __m256d blended = _mm256_or_pd(_mm256_and_pd(maskVec, srcVec), _mm256_andnot_pd(maskVec, destVec));
            _mm256_storeu_pd(pointer, blended);
        }

        template<class IndexStorage>
        static auto Gather(const double* base, const IndexStorage& indices) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 8,
                          "AVX2 double gather requires 64-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            Storage       result;
            const __m256d gathered = _mm256_i64gather_pd(base, indexVec, sizeof(double));
            _mm256_storeu_pd(result.Data(), gathered);
            return result;
        }

        template<class IndexStorage>
        static auto GatherMasked(const double*       base,
                                 const IndexStorage& indices,
                                 const MaskType&     mask,
                                 double              fill) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 8,
                          "AVX2 double gather requires 64-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            const __m256d maskVec  = MakeMask(mask);
            const __m256d fillVec  = _mm256_set1_pd(fill);
            const __m256d gathered = _mm256_mask_i64gather_pd(fillVec, base, indexVec, maskVec, sizeof(double));
            Storage       result;
            _mm256_storeu_pd(result.Data(), gathered);
            return result;
        }

        static constexpr void Store(const Storage& storage, double* pointer) noexcept
        {
            const __m256d reg = _mm256_loadu_pd(storage.Data());
            _mm256_storeu_pd(pointer, reg);
        }

        static constexpr void StoreAligned(const Storage& storage, double* pointer) noexcept
        {
            const __m256d reg = _mm256_loadu_pd(storage.Data());
            _mm256_store_pd(pointer, reg);
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d sum = _mm256_add_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d diff = _mm256_sub_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d prod = _mm256_mul_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), prod);
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d quot = _mm256_div_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), quot);
            return result;
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
            Storage result;
#if defined(__FMA__)
            const __m256d sum = _mm256_fmadd_pd(_mm256_loadu_pd(a.Data()), _mm256_loadu_pd(b.Data()), _mm256_loadu_pd(c.Data()));
#else
            const __m256d mul = _mm256_mul_pd(_mm256_loadu_pd(a.Data()), _mm256_loadu_pd(b.Data()));
            const __m256d sum = _mm256_add_pd(mul, _mm256_loadu_pd(c.Data()));
#endif
            _mm256_storeu_pd(result.Data(), sum);
            return result;
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d minimum = _mm256_min_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), minimum);
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256d maximum = _mm256_max_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()));
            _mm256_storeu_pd(result.Data(), maximum);
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage       result;
            const __m256d mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7FFF'FFFF'FFFF'FFFFLL));
            const __m256d magn = _mm256_and_pd(_mm256_loadu_pd(value.Data()), mask);
            _mm256_storeu_pd(result.Data(), magn);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256d cmp = _mm256_cmp_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()), _CMP_EQ_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256d cmp = _mm256_cmp_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()), _CMP_LT_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256d cmp = _mm256_cmp_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()), _CMP_LE_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256d cmp = _mm256_cmp_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()), _CMP_GT_OQ);
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256d cmp = _mm256_cmp_pd(_mm256_loadu_pd(lhs.Data()), _mm256_loadu_pd(rhs.Data()), _CMP_GE_OQ);
            return MaskFromRegister(cmp);
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            const __m256d allOnes = _mm256_castsi256_pd(_mm256_set1_epi64x(-1LL));
            return MaskFromRegister(_mm256_xor_pd(MakeMask(mask), allOnes));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_and_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_or_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_xor_pd(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_pd(MakeMask(mask)) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_pd(MakeMask(mask)) == 0xF;
        }
    };
#endif// defined(__AVX2__)

#if defined(__ARM_NEON)
    template<>
    struct BackendTraits<NeonTag, float> : BackendTraits<ScalarTag, float>
    {
        using Base = BackendTraits<ScalarTag, float>;

        static constexpr int native_lanes = 4;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, std::int32_t> : BackendTraits<SSE2Tag, std::int32_t>
    {
        using Base = BackendTraits<SSE2Tag, std::int32_t>;

        static constexpr int native_lanes = 8;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<AVX2Tag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX2Tag, std::int32_t>::Ops<BackendTraits<AVX2Tag, std::int32_t>::native_lanes>
        : BackendTraits<AVX2Tag, std::int32_t>::Base::template Ops<BackendTraits<AVX2Tag, std::int32_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<AVX2Tag, std::int32_t>::Base::template Ops<BackendTraits<AVX2Tag, std::int32_t>::native_lanes>;
        using Storage  = BackendTraits<AVX2Tag, std::int32_t>::template Storage<BackendTraits<AVX2Tag, std::int32_t>::native_lanes>;
        using MaskType = BackendTraits<AVX2Tag, bool>::template MaskStorage<BackendTraits<AVX2Tag, std::int32_t>::native_lanes>;

        static inline auto MaskFromRegister(__m256i reg) noexcept -> MaskType
        {
            MaskType  mask {};
            const int bitmask = _mm256_movemask_ps(_mm256_castsi256_ps(reg));
            for (int lane = 0; lane < BackendTraits<AVX2Tag, std::int32_t>::native_lanes; ++lane)
            {
                mask.Set(lane, ((bitmask >> lane) & 0x1) != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> __m256i
        {
            alignas(32) std::int32_t bits[BackendTraits<AVX2Tag, std::int32_t>::native_lanes];
            for (int lane = 0; lane < BackendTraits<AVX2Tag, std::int32_t>::native_lanes; ++lane)
            {
                bits[lane] = mask.Get(lane) ? -1 : 0;
            }
            return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bits));
        }

        static constexpr auto Load(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr auto LoadAligned(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()),
                                _mm256_load_si256(reinterpret_cast<const __m256i*>(pointer)));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(pointer),
                                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm256_store_si256(reinterpret_cast<__m256i*>(pointer),
                               _mm256_loadu_si256(reinterpret_cast<const __m256i*>(storage.Data())));
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i sum = _mm256_add_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                 _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), sum);
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i diff = _mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), diff);
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i prod = _mm256_mullo_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), prod);
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            return BaseOps::Div(lhs, rhs);
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_or_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage       result;
            const __m256i blended = _mm256_andnot_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), blended);
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpgt_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_or_si256(_mm256_cmpgt_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())),
                                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data()))),
                                                _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data()))));
            return MaskFromRegister(cmp);
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_cmpgt_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data())));
            return MaskFromRegister(cmp);
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m256i cmp = _mm256_or_si256(_mm256_cmpgt_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data()))),
                                                _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.Data())),
                                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.Data()))));
            return MaskFromRegister(cmp);
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_xor_si256(MakeMask(mask), _mm256_set1_epi32(-1)));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_and_si256(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_or_si256(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm256_xor_si256(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_ps(_mm256_castsi256_ps(MakeMask(mask))) != 0;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return _mm256_movemask_ps(_mm256_castsi256_ps(MakeMask(mask))) == 0xFF;
        }

        template<class IndexStorage>
        static auto Gather(const std::int32_t* base, const IndexStorage& indices) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4,
                          "AVX2 int gather requires 32-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            Storage       result;
            const __m256i gathered = _mm256_i32gather_epi32(base, indexVec, sizeof(std::int32_t));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), gathered);
            return result;
        }

        template<class IndexStorage>
        static auto GatherMasked(const std::int32_t* base,
                                 const IndexStorage& indices,
                                 const MaskType&     mask,
                                 std::int32_t        fill) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4,
                          "AVX2 int gather requires 32-bit indices.");
            const __m256i indexVec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(indices.Data()));
            const __m256i maskVec  = MakeMask(mask);
            const __m256i fillVec  = _mm256_set1_epi32(fill);
            const __m256i gathered = _mm256_mask_i32gather_epi32(fillVec, base, indexVec, maskVec, sizeof(std::int32_t));
            Storage       result;
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.Data()), gathered);
            return result;
        }
    };
    struct BackendTraits<NeonTag, float>::Ops<BackendTraits<NeonTag, float>::native_lanes>
        : BackendTraits<NeonTag, float>::Base::template Ops<BackendTraits<NeonTag, float>::native_lanes>
    {
        using BaseOps  = BackendTraits<NeonTag, float>::Base::template Ops<BackendTraits<NeonTag, float>::native_lanes>;
        using Storage  = BackendTraits<NeonTag, float>::template Storage<BackendTraits<NeonTag, float>::native_lanes>;
        using MaskType = BackendTraits<NeonTag, float>::template MaskStorage<BackendTraits<NeonTag, float>::native_lanes>;

        static inline auto MaskFromRegister(uint32x4_t reg) noexcept -> MaskType
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, float>::native_lanes];
            vst1q_u32(bits, reg);
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<NeonTag, float>::native_lanes; ++lane)
            {
                mask.Set(lane, bits[lane] != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> uint32x4_t
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, float>::native_lanes];
            for (int lane = 0; lane < BackendTraits<NeonTag, float>::native_lanes; ++lane)
            {
                bits[lane] = mask.Get(lane) ? 0xFFFFFFFFu : 0u;
            }
            return vld1q_u32(bits);
        }

        static constexpr auto Load(const float* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vld1q_f32(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const float* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vld1q_f32(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, float* pointer) noexcept
        {
            vst1q_f32(pointer, vld1q_f32(storage.Data()));
        }

        static constexpr void StoreAligned(const Storage& storage, float* pointer) noexcept
        {
            vst1q_f32(pointer, vld1q_f32(storage.Data()));
        }

        static auto LoadMasked(const float* pointer, const MaskType& mask, float fill) noexcept -> Storage
        {
            const uint32x4_t  maskVec = MakeMask(mask);
            const float32x4_t loadVec = vld1q_f32(pointer);
            const float32x4_t fillVec = vdupq_n_f32(fill);
            const uint32x4_t  blended = vorrq_u32(vandq_u32(maskVec, vreinterpretq_u32_f32(loadVec)),
                                                  vandq_u32(vmvnq_u32(maskVec), vreinterpretq_u32_f32(fillVec)));
            Storage           result;
            vst1q_f32(result.Data(), vreinterpretq_f32_u32(blended));
            return result;
        }

        static void StoreMasked(const Storage& storage, float* pointer, const MaskType& mask) noexcept
        {
            const uint32x4_t maskVec  = MakeMask(mask);
            const uint32x4_t srcBits  = vreinterpretq_u32_f32(vld1q_f32(storage.Data()));
            const uint32x4_t destBits = vreinterpretq_u32_f32(vld1q_f32(pointer));
            const uint32x4_t blended  = vorrq_u32(vandq_u32(maskVec, srcBits), vandq_u32(vmvnq_u32(maskVec), destBits));
            vst1q_f32(pointer, vreinterpretq_f32_u32(blended));
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vaddq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vsubq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vmulq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
#if defined(__aarch64__)
            Storage result;
            vst1q_f32(result.Data(), vdivq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
#else
            return BaseOps::Div(lhs, rhs);
#endif
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
#if defined(__aarch64__)
            Storage result;
            vst1q_f32(result.Data(), vfmaq_f32(vld1q_f32(c.Data()), vld1q_f32(a.Data()), vld1q_f32(b.Data())));
            return result;
#else
            return BaseOps::Fma(a, b, c);
#endif
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vminq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vmaxq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data())));
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage result;
            vst1q_f32(result.Data(), vabsq_f32(vld1q_f32(value.Data())));
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_f32(vceqq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data()))));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_f32(vcltq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data()))));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_f32(vcleq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data()))));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_f32(vcgtq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data()))));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_f32(vcgeq_f32(vld1q_f32(lhs.Data()), vld1q_f32(rhs.Data()))));
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(vmvnq_u32(MakeMask(mask)));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vandq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vorrq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(veorq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, float>::native_lanes];
            vst1q_u32(bits, MakeMask(mask));
            for (uint32_t bit: bits)
            {
                if (bit != 0)
                {
                    return true;
                }
            }
            return false;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, float>::native_lanes];
            vst1q_u32(bits, MakeMask(mask));
            for (uint32_t bit: bits)
            {
                if (bit == 0)
                {
                    return false;
                }
            }
            return true;
        }
    };

#if defined(__aarch64__) || defined(__ARM_FEATURE_FP64)
    template<>
    struct BackendTraits<NeonTag, double> : BackendTraits<ScalarTag, double>
    {
        using Base = BackendTraits<ScalarTag, double>;

        static constexpr int native_lanes = 2;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename Base::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<NeonTag, double>::Ops<BackendTraits<NeonTag, double>::native_lanes>
        : BackendTraits<NeonTag, double>::Base::template Ops<BackendTraits<NeonTag, double>::native_lanes>
    {
        using BaseOps  = BackendTraits<NeonTag, double>::Base::template Ops<BackendTraits<NeonTag, double>::native_lanes>;
        using Storage  = BackendTraits<NeonTag, double>::template Storage<BackendTraits<NeonTag, double>::native_lanes>;
        using MaskType = BackendTraits<NeonTag, double>::template MaskStorage<BackendTraits<NeonTag, double>::native_lanes>;

        static inline auto MaskFromRegister(uint64x2_t reg) noexcept -> MaskType
        {
            alignas(16) uint64_t bits[BackendTraits<NeonTag, double>::native_lanes];
            vst1q_u64(bits, reg);
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<NeonTag, double>::native_lanes; ++lane)
            {
                mask.Set(lane, bits[lane] != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> uint64x2_t
        {
            alignas(16) uint64_t bits[BackendTraits<NeonTag, double>::native_lanes];
            for (int lane = 0; lane < BackendTraits<NeonTag, double>::native_lanes; ++lane)
            {
                bits[lane] = mask.Get(lane) ? ~uint64_t {} : 0ULL;
            }
            return vld1q_u64(bits);
        }

        static constexpr auto Load(const double* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vld1q_f64(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const double* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vld1q_f64(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, double* pointer) noexcept
        {
            vst1q_f64(pointer, vld1q_f64(storage.Data()));
        }

        static constexpr void StoreAligned(const Storage& storage, double* pointer) noexcept
        {
            vst1q_f64(pointer, vld1q_f64(storage.Data()));
        }

        static auto LoadMasked(const double* pointer, const MaskType& mask, double fill) noexcept -> Storage
        {
            const uint64x2_t  maskVec = MakeMask(mask);
            const float64x2_t loadVec = vld1q_f64(pointer);
            const float64x2_t fillVec = vdupq_n_f64(fill);
            const uint64x2_t  blended = vorrq_u64(vandq_u64(maskVec, vreinterpretq_u64_f64(loadVec)),
                                                  vandq_u64(vmvnq_u64(maskVec), vreinterpretq_u64_f64(fillVec)));
            Storage           result;
            vst1q_f64(result.Data(), vreinterpretq_f64_u64(blended));
            return result;
        }

        static void StoreMasked(const Storage& storage, double* pointer, const MaskType& mask) noexcept
        {
            const uint64x2_t maskVec  = MakeMask(mask);
            const uint64x2_t srcBits  = vreinterpretq_u64_f64(vld1q_f64(storage.Data()));
            const uint64x2_t destBits = vreinterpretq_u64_f64(vld1q_f64(pointer));
            const uint64x2_t blended  = vorrq_u64(vandq_u64(maskVec, srcBits), vandq_u64(vmvnq_u64(maskVec), destBits));
            vst1q_f64(pointer, vreinterpretq_f64_u64(blended));
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vaddq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vsubq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vmulq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vdivq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Fma(const Storage& a,
                                  const Storage& b,
                                  const Storage& c) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vfmaq_f64(vld1q_f64(c.Data()), vld1q_f64(a.Data()), vld1q_f64(b.Data())));
            return result;
        }

        static constexpr auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vminq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vmaxq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data())));
            return result;
        }

        static constexpr auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage result;
            vst1q_f64(result.Data(), vabsq_f64(vld1q_f64(value.Data())));
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u64_f64(vceqq_f64(vld1q_f64(lhs.Data()), vld1q_f64(rhs.Data()))));
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(vmvnq_u64(MakeMask(mask)));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vandq_u64(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vorrq_u64(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(veorq_u64(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint64_t bits[BackendTraits<NeonTag, double>::native_lanes];
            vst1q_u64(bits, MakeMask(mask));
            for (uint64_t bit: bits)
            {
                if (bit != 0)
                {
                    return true;
                }
            }
            return false;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint64_t bits[BackendTraits<NeonTag, double>::native_lanes];
            vst1q_u64(bits, MakeMask(mask));
            for (uint64_t bit: bits)
            {
                if (bit == 0)
                {
                    return false;
                }
            }
            return true;
        }
    };
#endif

    template<>
    struct BackendTraits<NeonTag, bool> : BackendTraits<NeonTag, float>
    {
    };

    template<>
    struct BackendTraits<NeonTag, std::uint8_t> : BackendTraits<ScalarTag, std::uint8_t>
    {
        using Base = BackendTraits<ScalarTag, std::uint8_t>;

        static constexpr int native_lanes = 16;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<NeonTag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<NeonTag, std::int8_t> : BackendTraits<ScalarTag, std::int8_t>
    {
        using Base = BackendTraits<ScalarTag, std::int8_t>;

        static constexpr int native_lanes = 16;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<NeonTag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<NeonTag, std::uint8_t>::Ops<BackendTraits<NeonTag, std::uint8_t>::native_lanes>
        : BackendTraits<NeonTag, std::uint8_t>::Base::template Ops<BackendTraits<NeonTag, std::uint8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<NeonTag, std::uint8_t>::Base::template Ops<BackendTraits<NeonTag, std::uint8_t>::native_lanes>;
        using Storage  = BackendTraits<NeonTag, std::uint8_t>::template Storage<BackendTraits<NeonTag, std::uint8_t>::native_lanes>;
        using MaskType = BackendTraits<NeonTag, bool>::template MaskStorage<BackendTraits<NeonTag, std::uint8_t>::native_lanes>;

        static inline auto MaskFromRegister(uint8x16_t reg) noexcept -> MaskType
        {
            alignas(16) std::uint8_t bits[BackendTraits<NeonTag, std::uint8_t>::native_lanes];
            vst1q_u8(bits, reg);
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<NeonTag, std::uint8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, bits[lane] != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), vld1q_u8(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const std::uint8_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), vld1q_u8(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            vst1q_u8(pointer, vld1q_u8(storage.Data()));
        }

        static constexpr void StoreAligned(const Storage& storage, std::uint8_t* pointer) noexcept
        {
            vst1q_u8(pointer, vld1q_u8(storage.Data()));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), vandq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), vorrq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), veorq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_u8(result.Data(), vbicq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vceqq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vcltq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const auto eq = vceqq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data()));
            const auto lt = vcltq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data()));
            return MaskFromRegister(vorrq_u8(eq, lt));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vcgtq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data())));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const auto eq = vceqq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data()));
            const auto gt = vcgtq_u8(vld1q_u8(lhs.Data()), vld1q_u8(rhs.Data()));
            return MaskFromRegister(vorrq_u8(eq, gt));
        }
    };

    template<>
    struct BackendTraits<NeonTag, std::int8_t>::Ops<BackendTraits<NeonTag, std::int8_t>::native_lanes>
        : BackendTraits<NeonTag, std::int8_t>::Base::template Ops<BackendTraits<NeonTag, std::int8_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<NeonTag, std::int8_t>::Base::template Ops<BackendTraits<NeonTag, std::int8_t>::native_lanes>;
        using Storage  = BackendTraits<NeonTag, std::int8_t>::template Storage<BackendTraits<NeonTag, std::int8_t>::native_lanes>;
        using MaskType = BackendTraits<NeonTag, bool>::template MaskStorage<BackendTraits<NeonTag, std::int8_t>::native_lanes>;

        static inline auto MaskFromRegister(uint8x16_t reg) noexcept -> MaskType
        {
            alignas(16) std::uint8_t bits[BackendTraits<NeonTag, std::int8_t>::native_lanes];
            vst1q_u8(bits, reg);
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<NeonTag, std::int8_t>::native_lanes; ++lane)
            {
                mask.Set(lane, bits[lane] != 0);
            }
            return mask;
        }

        static constexpr auto Load(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_s8(result.Data(), vld1q_s8(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const std::int8_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_s8(result.Data(), vld1q_s8(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int8_t* pointer) noexcept
        {
            vst1q_s8(pointer, vld1q_s8(storage.Data()));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int8_t* pointer) noexcept
        {
            vst1q_s8(pointer, vld1q_s8(storage.Data()));
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            const auto lhsBits = vreinterpretq_u8_s8(vld1q_s8(lhs.Data()));
            const auto rhsBits = vreinterpretq_u8_s8(vld1q_s8(rhs.Data()));
            vst1q_s8(result.Data(), vreinterpretq_s8_u8(vandq_u8(lhsBits, rhsBits)));
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            const auto lhsBits = vreinterpretq_u8_s8(vld1q_s8(lhs.Data()));
            const auto rhsBits = vreinterpretq_u8_s8(vld1q_s8(rhs.Data()));
            vst1q_s8(result.Data(), vreinterpretq_s8_u8(vorrq_u8(lhsBits, rhsBits)));
            return result;
        }

        static constexpr auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            const auto lhsBits = vreinterpretq_u8_s8(vld1q_s8(lhs.Data()));
            const auto rhsBits = vreinterpretq_u8_s8(vld1q_s8(rhs.Data()));
            vst1q_s8(result.Data(), vreinterpretq_s8_u8(veorq_u8(lhsBits, rhsBits)));
            return result;
        }

        static constexpr auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            const auto lhsBits = vreinterpretq_u8_s8(vld1q_s8(lhs.Data()));
            const auto rhsBits = vreinterpretq_u8_s8(vld1q_s8(rhs.Data()));
            vst1q_s8(result.Data(), vreinterpretq_s8_u8(vbicq_u8(lhsBits, rhsBits)));
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vceqq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data())));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vcltq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data())));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const auto eq = vceqq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data()));
            const auto lt = vcltq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data()));
            return MaskFromRegister(vorrq_u8(eq, lt));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vcgtq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data())));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const auto eq = vceqq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data()));
            const auto gt = vcgtq_s8(vld1q_s8(lhs.Data()), vld1q_s8(rhs.Data()));
            return MaskFromRegister(vorrq_u8(eq, gt));
        }
    };

    template<>
    struct BackendTraits<NeonTag, std::int32_t> : BackendTraits<ScalarTag, std::int32_t>
    {
        using Base = BackendTraits<ScalarTag, std::int32_t>;

        static constexpr int native_lanes = 4;

        template<int Lanes>
        using Storage = typename Base::template Storage<Lanes>;

        template<int Lanes>
        using MaskStorage = typename BackendTraits<NeonTag, bool>::template MaskStorage<Lanes>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<NeonTag, std::int32_t>::Ops<BackendTraits<NeonTag, std::int32_t>::native_lanes>
        : BackendTraits<NeonTag, std::int32_t>::Base::template Ops<BackendTraits<NeonTag, std::int32_t>::native_lanes>
    {
        using BaseOps  = BackendTraits<NeonTag, std::int32_t>::Base::template Ops<BackendTraits<NeonTag, std::int32_t>::native_lanes>;
        using Storage  = BackendTraits<NeonTag, std::int32_t>::template Storage<BackendTraits<NeonTag, std::int32_t>::native_lanes>;
        using MaskType = BackendTraits<NeonTag, bool>::template MaskStorage<BackendTraits<NeonTag, std::int32_t>::native_lanes>;

        static inline auto MaskFromRegister(uint32x4_t reg) noexcept -> MaskType
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, std::int32_t>::native_lanes];
            vst1q_u32(bits, reg);
            MaskType mask {};
            for (int lane = 0; lane < BackendTraits<NeonTag, std::int32_t>::native_lanes; ++lane)
            {
                mask.Set(lane, bits[lane] != 0);
            }
            return mask;
        }

        static inline auto MakeMask(const MaskType& mask) noexcept -> uint32x4_t
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, std::int32_t>::native_lanes];
            for (int lane = 0; lane < BackendTraits<NeonTag, std::int32_t>::native_lanes; ++lane)
            {
                bits[lane] = mask.Get(lane) ? 0xFFFFFFFFu : 0u;
            }
            return vld1q_u32(bits);
        }

        static constexpr auto Load(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_s32(result.Data(), vld1q_s32(pointer));
            return result;
        }

        static constexpr auto LoadAligned(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            vst1q_s32(result.Data(), vld1q_s32(pointer));
            return result;
        }

        static constexpr void Store(const Storage& storage, std::int32_t* pointer) noexcept
        {
            vst1q_s32(pointer, vld1q_s32(storage.Data()));
        }

        static constexpr void StoreAligned(const Storage& storage, std::int32_t* pointer) noexcept
        {
            vst1q_s32(pointer, vld1q_s32(storage.Data()));
        }

        static auto LoadMasked(const std::int32_t* pointer, const MaskType& mask, std::int32_t fill) noexcept -> Storage
        {
            const uint32x4_t maskVec = MakeMask(mask);
            const int32x4_t  loadVec = vld1q_s32(pointer);
            const int32x4_t  fillVec = vdupq_n_s32(fill);
            const uint32x4_t blended = vorrq_u32(vandq_u32(maskVec, vreinterpretq_u32_s32(loadVec)),
                                                 vandq_u32(vmvnq_u32(maskVec), vreinterpretq_u32_s32(fillVec)));
            Storage          result;
            vst1q_s32(result.Data(), vreinterpretq_s32_u32(blended));
            return result;
        }

        static void StoreMasked(const Storage& storage, std::int32_t* pointer, const MaskType& mask) noexcept
        {
            const uint32x4_t maskVec  = MakeMask(mask);
            const uint32x4_t srcBits  = vreinterpretq_u32_s32(vld1q_s32(storage.Data()));
            const uint32x4_t destBits = vreinterpretq_u32_s32(vld1q_s32(pointer));
            const uint32x4_t blended  = vorrq_u32(vandq_u32(maskVec, srcBits), vandq_u32(vmvnq_u32(maskVec), destBits));
            vst1q_s32(pointer, vreinterpretq_s32_u32(blended));
        }

        static constexpr auto Add(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_s32(result.Data(), vaddq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            return result;
        }

        static constexpr auto Sub(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            vst1q_s32(result.Data(), vsubq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            return result;
        }

        static constexpr auto Mul(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
#if defined(__aarch64__)
            vst1q_s32(result.Data(), vmulq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
#else
            return BaseOps::Mul(lhs, rhs);
#endif
            return result;
        }

        static constexpr auto Div(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            return BaseOps::Div(lhs, rhs);
        }

        static constexpr auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage          result;
            const uint32x4_t blended = vandq_u32(vreinterpretq_u32_s32(vld1q_s32(lhs.Data())),
                                                 vreinterpretq_u32_s32(vld1q_s32(rhs.Data())));
            vst1q_s32(result.Data(), vreinterpretq_s32_u32(blended));
            return result;
        }

        static constexpr auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage          result;
            const uint32x4_t blended = vorrq_u32(vreinterpretq_u32_s32(vld1q_s32(lhs.Data())),
                                                 vreinterpretq_u32_s32(vld1q_s32(rhs.Data())));
            vst1q_s32(result.Data(), vreinterpretq_s32_u32(blended));
            return result;
        }

        static auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_s32(vceqq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data()))));
        }

        static auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_s32(vcltq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data()))));
        }

        static auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const uint32x4_t lt = vreinterpretq_u32_s32(vcltq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            const uint32x4_t eq = vreinterpretq_u32_s32(vceqq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            return MaskFromRegister(vorrq_u32(lt, eq));
        }

        static auto CompareGt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vreinterpretq_u32_s32(vcgtq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data()))));
        }

        static auto CompareGe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const uint32x4_t gt = vreinterpretq_u32_s32(vcgtq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            const uint32x4_t eq = vreinterpretq_u32_s32(vceqq_s32(vld1q_s32(lhs.Data()), vld1q_s32(rhs.Data())));
            return MaskFromRegister(vorrq_u32(gt, eq));
        }

        static auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(vmvnq_u32(MakeMask(mask)));
        }

        static auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vandq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(vorrq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(veorq_u32(MakeMask(lhs), MakeMask(rhs)));
        }

        static auto MaskAny(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, std::int32_t>::native_lanes];
            vst1q_u32(bits, MakeMask(mask));
            for (uint32_t bit: bits)
            {
                if (bit != 0)
                {
                    return true;
                }
            }
            return false;
        }

        static auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            alignas(16) uint32_t bits[BackendTraits<NeonTag, std::int32_t>::native_lanes];
            vst1q_u32(bits, MakeMask(mask));
            for (uint32_t bit: bits)
            {
                if (bit == 0)
                {
                    return false;
                }
            }
            return true;
        }
    };
#endif// defined(__ARM_NEON)

}// namespace NGIN::SIMD::detail
