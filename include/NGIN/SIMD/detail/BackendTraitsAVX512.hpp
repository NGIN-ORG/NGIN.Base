#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// AVX-512F/BW/DQ/VL native-width backend operations. This header is included
// by BackendTraits.hpp after the scalar and narrower backend definitions.

#include <immintrin.h>

namespace NGIN::SIMD::detail
{
    namespace avx512_detail
    {
        template<int Lanes, class MaskType, class Bits>
        [[nodiscard]] inline auto MaskFromBits(Bits bits) noexcept -> MaskType
        {
            MaskType result {};
            result.SetBits(static_cast<std::uint64_t>(bits));
            return result;
        }

        template<int Lanes, class Bits, class MaskType>
        [[nodiscard]] inline auto BitsFromMask(const MaskType& mask) noexcept -> Bits
        {
            return static_cast<Bits>(mask.ToBits());
        }

        template<int Lanes, class Bits>
        [[nodiscard]] constexpr auto AllMaskBits() noexcept -> Bits
        {
            if constexpr (Lanes == 64)
            {
                return static_cast<Bits>(~std::uint64_t {0});
            }
            else
            {
                return static_cast<Bits>((std::uint64_t {1} << Lanes) - 1);
            }
        }
    }// namespace avx512_detail

    template<class T, int Lanes>
    struct AVX512TraitBase : BackendTraits<ScalarTag, T>
    {
        using Base = BackendTraits<ScalarTag, T>;

        static constexpr int native_lanes = Lanes;

        template<int RequestedLanes>
        using Storage = typename Base::template Storage<RequestedLanes>;

        template<int RequestedLanes>
        using MaskStorage = typename Base::template MaskStorage<RequestedLanes>;

        template<int RequestedLanes>
        struct Ops : Base::template Ops<RequestedLanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, float> : AVX512TraitBase<float, 16>
    {
        using Base = AVX512TraitBase<float, 16>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, bool> : BackendTraits<AVX512Tag, float>
    {
    };

    template<>
    struct BackendTraits<AVX512Tag, double> : AVX512TraitBase<double, 8>
    {
        using Base = AVX512TraitBase<double, 8>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, std::int32_t> : AVX512TraitBase<std::int32_t, 16>
    {
        using Base = AVX512TraitBase<std::int32_t, 16>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, std::uint8_t> : AVX512TraitBase<std::uint8_t, 64>
    {
        using Base = AVX512TraitBase<std::uint8_t, 64>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, std::int8_t> : AVX512TraitBase<std::int8_t, 64>
    {
        using Base = AVX512TraitBase<std::int8_t, 64>;

        template<int Lanes>
        struct Ops : Base::template Ops<Lanes>
        {
        };
    };

    template<>
    struct BackendTraits<AVX512Tag, float>::Ops<BackendTraits<AVX512Tag, float>::native_lanes>
        : BackendTraits<ScalarTag, float>::Ops<BackendTraits<AVX512Tag, float>::native_lanes>
    {
        static constexpr int LANES = BackendTraits<AVX512Tag, float>::native_lanes;
        using BaseOps              = BackendTraits<ScalarTag, float>::Ops<LANES>;
        using Storage              = BackendTraits<AVX512Tag, float>::Storage<LANES>;
        using MaskType             = BackendTraits<AVX512Tag, float>::MaskStorage<LANES>;

        static constexpr bool has_native_overrides = true;

        [[nodiscard]] static inline auto MaskFromRegister(__mmask16 bits) noexcept -> MaskType
        {
            return avx512_detail::MaskFromBits<LANES, MaskType>(bits);
        }

        [[nodiscard]] static inline auto MakeMask(const MaskType& mask) noexcept -> __mmask16
        {
            return avx512_detail::BitsFromMask<LANES, __mmask16>(mask);
        }

        [[nodiscard]] static inline auto Load(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_loadu_ps(pointer));
            return result;
        }

        [[nodiscard]] static inline auto LoadAligned(const float* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_load_ps(pointer));
            return result;
        }

        [[nodiscard]] static inline auto LoadMasked(const float* pointer, const MaskType& mask, float fill) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_mask_loadu_ps(_mm512_set1_ps(fill), MakeMask(mask), pointer));
            return result;
        }

        static inline void Store(const Storage& storage, float* pointer) noexcept
        {
            _mm512_storeu_ps(pointer, _mm512_loadu_ps(storage.Data()));
        }

        static inline void StoreAligned(const Storage& storage, float* pointer) noexcept
        {
            _mm512_store_ps(pointer, _mm512_loadu_ps(storage.Data()));
        }

        static inline void StoreMasked(const Storage& storage, float* pointer, const MaskType& mask) noexcept
        {
            _mm512_mask_storeu_ps(pointer, MakeMask(mask), _mm512_loadu_ps(storage.Data()));
        }

        template<class IndexStorage>
        [[nodiscard]] static inline auto Gather(const float* base, const IndexStorage& indices) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4, "AVX-512 float gather requires 32-bit indices.");
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            Storage       result;
            _mm512_storeu_ps(result.Data(), _mm512_i32gather_ps(indexVector, base, sizeof(float)));
            return result;
        }

        template<class IndexStorage>
        [[nodiscard]] static inline auto GatherMasked(const float*        base,
                                                      const IndexStorage& indices,
                                                      const MaskType&     mask,
                                                      float               fill) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4, "AVX-512 float gather requires 32-bit indices.");
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            Storage       result;
            _mm512_storeu_ps(result.Data(),
                             _mm512_mask_i32gather_ps(_mm512_set1_ps(fill), MakeMask(mask), indexVector, base, sizeof(float)));
            return result;
        }

        template<class IndexStorage>
        static inline void Scatter(const Storage& storage, float* base, const IndexStorage& indices) noexcept
        {
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            _mm512_i32scatter_ps(base, indexVector, _mm512_loadu_ps(storage.Data()), sizeof(float));
        }

        template<class IndexStorage>
        static inline void ScatterMasked(const Storage&      storage,
                                         float*              base,
                                         const IndexStorage& indices,
                                         const MaskType&     mask) noexcept
        {
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            _mm512_mask_i32scatter_ps(base, MakeMask(mask), indexVector, _mm512_loadu_ps(storage.Data()), sizeof(float));
        }

#define NGIN_SIMD_AVX512_FLOAT_BINARY(Name, Intrinsic)                                                        \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> Storage         \
    {                                                                                                         \
        Storage result;                                                                                       \
        _mm512_storeu_ps(result.Data(), Intrinsic(_mm512_loadu_ps(lhs.Data()), _mm512_loadu_ps(rhs.Data()))); \
        return result;                                                                                        \
    }

        NGIN_SIMD_AVX512_FLOAT_BINARY(Add, _mm512_add_ps)
        NGIN_SIMD_AVX512_FLOAT_BINARY(Sub, _mm512_sub_ps)
        NGIN_SIMD_AVX512_FLOAT_BINARY(Mul, _mm512_mul_ps)
        NGIN_SIMD_AVX512_FLOAT_BINARY(Div, _mm512_div_ps)
        NGIN_SIMD_AVX512_FLOAT_BINARY(Min, _mm512_min_ps)
        NGIN_SIMD_AVX512_FLOAT_BINARY(Max, _mm512_max_ps)
#undef NGIN_SIMD_AVX512_FLOAT_BINARY

        [[nodiscard]] static inline auto Fma(const Storage& a, const Storage& b, const Storage& c) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(),
                             _mm512_fmadd_ps(_mm512_loadu_ps(a.Data()), _mm512_loadu_ps(b.Data()), _mm512_loadu_ps(c.Data())));
            return result;
        }

        [[nodiscard]] static inline auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage       result;
            const __m512i bits = _mm512_and_si512(_mm512_castps_si512(_mm512_loadu_ps(value.Data())),
                                                  _mm512_set1_epi32(0x7FFF'FFFF));
            _mm512_storeu_ps(result.Data(), _mm512_castsi512_ps(bits));
            return result;
        }

        [[nodiscard]] static inline auto BitwiseAnd(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_and_ps(_mm512_loadu_ps(lhs.Data()), _mm512_loadu_ps(rhs.Data())));
            return result;
        }

        [[nodiscard]] static inline auto BitwiseOr(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_or_ps(_mm512_loadu_ps(lhs.Data()), _mm512_loadu_ps(rhs.Data())));
            return result;
        }

        [[nodiscard]] static inline auto BitwiseXor(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_xor_ps(_mm512_loadu_ps(lhs.Data()), _mm512_loadu_ps(rhs.Data())));
            return result;
        }

        [[nodiscard]] static inline auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_ps(result.Data(), _mm512_andnot_ps(_mm512_loadu_ps(rhs.Data()), _mm512_loadu_ps(lhs.Data())));
            return result;
        }

#define NGIN_SIMD_AVX512_FLOAT_COMPARE(Name, Predicate)                                                                   \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> MaskType                    \
    {                                                                                                                     \
        return MaskFromRegister(_mm512_cmp_ps_mask(_mm512_loadu_ps(lhs.Data()), _mm512_loadu_ps(rhs.Data()), Predicate)); \
    }

        NGIN_SIMD_AVX512_FLOAT_COMPARE(CompareEq, _CMP_EQ_OQ)
        NGIN_SIMD_AVX512_FLOAT_COMPARE(CompareNe, _CMP_NEQ_UQ)
        NGIN_SIMD_AVX512_FLOAT_COMPARE(CompareLt, _CMP_LT_OQ)
        NGIN_SIMD_AVX512_FLOAT_COMPARE(CompareLe, _CMP_LE_OQ)
#undef NGIN_SIMD_AVX512_FLOAT_COMPARE

        [[nodiscard]] static inline auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(~MakeMask(mask)));
        }

        [[nodiscard]] static inline auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) & MakeMask(rhs)));
        }

        [[nodiscard]] static inline auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) | MakeMask(rhs)));
        }

        [[nodiscard]] static inline auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) ^ MakeMask(rhs)));
        }

        [[nodiscard]] static inline auto MaskAny(const MaskType& mask) noexcept -> bool { return MakeMask(mask) != 0; }
        [[nodiscard]] static inline auto MaskAll(const MaskType& mask) noexcept -> bool { return MakeMask(mask) == 0xFFFF; }
    };

    template<>
    struct BackendTraits<AVX512Tag, double>::Ops<BackendTraits<AVX512Tag, double>::native_lanes>
        : BackendTraits<ScalarTag, double>::Ops<BackendTraits<AVX512Tag, double>::native_lanes>
    {
        static constexpr int LANES = BackendTraits<AVX512Tag, double>::native_lanes;
        using BaseOps              = BackendTraits<ScalarTag, double>::Ops<LANES>;
        using Storage              = BackendTraits<AVX512Tag, double>::Storage<LANES>;
        using MaskType             = BackendTraits<AVX512Tag, double>::MaskStorage<LANES>;

        static constexpr bool has_native_overrides = true;

        [[nodiscard]] static inline auto MaskFromRegister(__mmask8 bits) noexcept -> MaskType
        {
            return avx512_detail::MaskFromBits<LANES, MaskType>(bits);
        }

        [[nodiscard]] static inline auto MakeMask(const MaskType& mask) noexcept -> __mmask8
        {
            return avx512_detail::BitsFromMask<LANES, __mmask8>(mask);
        }

        [[nodiscard]] static inline auto Load(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_pd(result.Data(), _mm512_loadu_pd(pointer));
            return result;
        }

        [[nodiscard]] static inline auto LoadAligned(const double* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_pd(result.Data(), _mm512_load_pd(pointer));
            return result;
        }

        [[nodiscard]] static inline auto LoadMasked(const double* pointer, const MaskType& mask, double fill) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_pd(result.Data(), _mm512_mask_loadu_pd(_mm512_set1_pd(fill), MakeMask(mask), pointer));
            return result;
        }

        static inline void Store(const Storage& storage, double* pointer) noexcept
        {
            _mm512_storeu_pd(pointer, _mm512_loadu_pd(storage.Data()));
        }

        static inline void StoreAligned(const Storage& storage, double* pointer) noexcept
        {
            _mm512_store_pd(pointer, _mm512_loadu_pd(storage.Data()));
        }

        static inline void StoreMasked(const Storage& storage, double* pointer, const MaskType& mask) noexcept
        {
            _mm512_mask_storeu_pd(pointer, MakeMask(mask), _mm512_loadu_pd(storage.Data()));
        }

        template<class IndexStorage>
        [[nodiscard]] static inline auto Gather(const double* base, const IndexStorage& indices) noexcept -> Storage
        {
            static_assert(sizeof(typename IndexStorage::value_type) == 4, "AVX-512 double gather requires 32-bit indices.");
            const __m256i indexVector = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.Data()));
            Storage       result;
            _mm512_storeu_pd(result.Data(), _mm512_i32gather_pd(indexVector, base, sizeof(double)));
            return result;
        }

        template<class IndexStorage>
        [[nodiscard]] static inline auto GatherMasked(const double*       base,
                                                      const IndexStorage& indices,
                                                      const MaskType&     mask,
                                                      double              fill) noexcept -> Storage
        {
            const __m256i indexVector = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.Data()));
            Storage       result;
            _mm512_storeu_pd(result.Data(),
                             _mm512_mask_i32gather_pd(_mm512_set1_pd(fill), MakeMask(mask), indexVector, base, sizeof(double)));
            return result;
        }

        template<class IndexStorage>
        static inline void Scatter(const Storage& storage, double* base, const IndexStorage& indices) noexcept
        {
            const __m256i indexVector = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.Data()));
            _mm512_i32scatter_pd(base, indexVector, _mm512_loadu_pd(storage.Data()), sizeof(double));
        }

        template<class IndexStorage>
        static inline void ScatterMasked(const Storage&      storage,
                                         double*             base,
                                         const IndexStorage& indices,
                                         const MaskType&     mask) noexcept
        {
            const __m256i indexVector = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.Data()));
            _mm512_mask_i32scatter_pd(base, MakeMask(mask), indexVector, _mm512_loadu_pd(storage.Data()), sizeof(double));
        }

#define NGIN_SIMD_AVX512_DOUBLE_BINARY(Name, Intrinsic)                                                       \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> Storage         \
    {                                                                                                         \
        Storage result;                                                                                       \
        _mm512_storeu_pd(result.Data(), Intrinsic(_mm512_loadu_pd(lhs.Data()), _mm512_loadu_pd(rhs.Data()))); \
        return result;                                                                                        \
    }

        NGIN_SIMD_AVX512_DOUBLE_BINARY(Add, _mm512_add_pd)
        NGIN_SIMD_AVX512_DOUBLE_BINARY(Sub, _mm512_sub_pd)
        NGIN_SIMD_AVX512_DOUBLE_BINARY(Mul, _mm512_mul_pd)
        NGIN_SIMD_AVX512_DOUBLE_BINARY(Div, _mm512_div_pd)
        NGIN_SIMD_AVX512_DOUBLE_BINARY(Min, _mm512_min_pd)
        NGIN_SIMD_AVX512_DOUBLE_BINARY(Max, _mm512_max_pd)
#undef NGIN_SIMD_AVX512_DOUBLE_BINARY

        [[nodiscard]] static inline auto Fma(const Storage& a, const Storage& b, const Storage& c) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_pd(result.Data(),
                             _mm512_fmadd_pd(_mm512_loadu_pd(a.Data()), _mm512_loadu_pd(b.Data()), _mm512_loadu_pd(c.Data())));
            return result;
        }

        [[nodiscard]] static inline auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage       result;
            const __m512i bits = _mm512_and_si512(_mm512_castpd_si512(_mm512_loadu_pd(value.Data())),
                                                  _mm512_set1_epi64(0x7FFF'FFFF'FFFF'FFFFLL));
            _mm512_storeu_pd(result.Data(), _mm512_castsi512_pd(bits));
            return result;
        }

#define NGIN_SIMD_AVX512_DOUBLE_COMPARE(Name, Predicate)                                                                  \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> MaskType                    \
    {                                                                                                                     \
        return MaskFromRegister(_mm512_cmp_pd_mask(_mm512_loadu_pd(lhs.Data()), _mm512_loadu_pd(rhs.Data()), Predicate)); \
    }

        NGIN_SIMD_AVX512_DOUBLE_COMPARE(CompareEq, _CMP_EQ_OQ)
        NGIN_SIMD_AVX512_DOUBLE_COMPARE(CompareNe, _CMP_NEQ_UQ)
        NGIN_SIMD_AVX512_DOUBLE_COMPARE(CompareLt, _CMP_LT_OQ)
        NGIN_SIMD_AVX512_DOUBLE_COMPARE(CompareLe, _CMP_LE_OQ)
#undef NGIN_SIMD_AVX512_DOUBLE_COMPARE

        [[nodiscard]] static inline auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask8>(~MakeMask(mask)));
        }
        [[nodiscard]] static inline auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask8>(MakeMask(lhs) & MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask8>(MakeMask(lhs) | MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask8>(MakeMask(lhs) ^ MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskAny(const MaskType& mask) noexcept -> bool { return MakeMask(mask) != 0; }
        [[nodiscard]] static inline auto MaskAll(const MaskType& mask) noexcept -> bool { return MakeMask(mask) == 0xFF; }
    };

    template<>
    struct BackendTraits<AVX512Tag, std::int32_t>::Ops<BackendTraits<AVX512Tag, std::int32_t>::native_lanes>
        : BackendTraits<ScalarTag, std::int32_t>::Ops<BackendTraits<AVX512Tag, std::int32_t>::native_lanes>
    {
        static constexpr int LANES = BackendTraits<AVX512Tag, std::int32_t>::native_lanes;
        using BaseOps              = BackendTraits<ScalarTag, std::int32_t>::Ops<LANES>;
        using Storage              = BackendTraits<AVX512Tag, std::int32_t>::Storage<LANES>;
        using MaskType             = BackendTraits<AVX512Tag, std::int32_t>::MaskStorage<LANES>;

        static constexpr bool has_native_overrides = true;

        [[nodiscard]] static inline auto MaskFromRegister(__mmask16 bits) noexcept -> MaskType
        {
            return avx512_detail::MaskFromBits<LANES, MaskType>(bits);
        }
        [[nodiscard]] static inline auto MakeMask(const MaskType& mask) noexcept -> __mmask16
        {
            return avx512_detail::BitsFromMask<LANES, __mmask16>(mask);
        }

        [[nodiscard]] static inline auto Load(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_loadu_si512(pointer));
            return result;
        }
        [[nodiscard]] static inline auto LoadAligned(const std::int32_t* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_load_si512(pointer));
            return result;
        }
        [[nodiscard]] static inline auto LoadMasked(const std::int32_t* pointer,
                                                    const MaskType&     mask,
                                                    std::int32_t        fill) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_mask_loadu_epi32(_mm512_set1_epi32(fill), MakeMask(mask), pointer));
            return result;
        }
        static inline void Store(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm512_storeu_si512(pointer, _mm512_loadu_si512(storage.Data()));
        }
        static inline void StoreAligned(const Storage& storage, std::int32_t* pointer) noexcept
        {
            _mm512_store_si512(pointer, _mm512_loadu_si512(storage.Data()));
        }
        static inline void StoreMasked(const Storage& storage, std::int32_t* pointer, const MaskType& mask) noexcept
        {
            _mm512_mask_storeu_epi32(pointer, MakeMask(mask), _mm512_loadu_si512(storage.Data()));
        }

#define NGIN_SIMD_AVX512_INT_BINARY(Name, Intrinsic)                                                                   \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> Storage                  \
    {                                                                                                                  \
        Storage result;                                                                                                \
        _mm512_storeu_si512(result.Data(), Intrinsic(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data()))); \
        return result;                                                                                                 \
    }

        NGIN_SIMD_AVX512_INT_BINARY(Add, _mm512_add_epi32)
        NGIN_SIMD_AVX512_INT_BINARY(Sub, _mm512_sub_epi32)
        NGIN_SIMD_AVX512_INT_BINARY(Mul, _mm512_mullo_epi32)
        NGIN_SIMD_AVX512_INT_BINARY(Min, _mm512_min_epi32)
        NGIN_SIMD_AVX512_INT_BINARY(Max, _mm512_max_epi32)
        NGIN_SIMD_AVX512_INT_BINARY(BitwiseAnd, _mm512_and_si512)
        NGIN_SIMD_AVX512_INT_BINARY(BitwiseOr, _mm512_or_si512)
        NGIN_SIMD_AVX512_INT_BINARY(BitwiseXor, _mm512_xor_si512)
#undef NGIN_SIMD_AVX512_INT_BINARY

        [[nodiscard]] static inline auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_andnot_si512(_mm512_loadu_si512(rhs.Data()), _mm512_loadu_si512(lhs.Data())));
            return result;
        }
        [[nodiscard]] static inline auto Abs(const Storage& value) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_abs_epi32(_mm512_loadu_si512(value.Data())));
            return result;
        }

#define NGIN_SIMD_AVX512_INT_COMPARE(Name, Predicate)                                                                              \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> MaskType                             \
    {                                                                                                                              \
        return MaskFromRegister(_mm512_cmp_epi32_mask(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data()), Predicate)); \
    }

        NGIN_SIMD_AVX512_INT_COMPARE(CompareEq, _MM_CMPINT_EQ)
        NGIN_SIMD_AVX512_INT_COMPARE(CompareNe, _MM_CMPINT_NE)
        NGIN_SIMD_AVX512_INT_COMPARE(CompareLt, _MM_CMPINT_LT)
        NGIN_SIMD_AVX512_INT_COMPARE(CompareLe, _MM_CMPINT_LE)
#undef NGIN_SIMD_AVX512_INT_COMPARE

        template<class IndexStorage>
        [[nodiscard]] static inline auto Gather(const std::int32_t* base, const IndexStorage& indices) noexcept -> Storage
        {
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            Storage       result;
            _mm512_storeu_si512(result.Data(), _mm512_i32gather_epi32(indexVector, base, sizeof(std::int32_t)));
            return result;
        }
        template<class IndexStorage>
        [[nodiscard]] static inline auto GatherMasked(const std::int32_t* base,
                                                      const IndexStorage& indices,
                                                      const MaskType&     mask,
                                                      std::int32_t        fill) noexcept -> Storage
        {
            const __m512i indexVector = _mm512_loadu_si512(indices.Data());
            Storage       result;
            _mm512_storeu_si512(
                    result.Data(),
                    _mm512_mask_i32gather_epi32(_mm512_set1_epi32(fill), MakeMask(mask), indexVector, base, sizeof(std::int32_t)));
            return result;
        }
        template<class IndexStorage>
        static inline void Scatter(const Storage& storage, std::int32_t* base, const IndexStorage& indices) noexcept
        {
            _mm512_i32scatter_epi32(base, _mm512_loadu_si512(indices.Data()), _mm512_loadu_si512(storage.Data()), sizeof(std::int32_t));
        }
        template<class IndexStorage>
        static inline void ScatterMasked(const Storage&      storage,
                                         std::int32_t*       base,
                                         const IndexStorage& indices,
                                         const MaskType&     mask) noexcept
        {
            _mm512_mask_i32scatter_epi32(base,
                                         MakeMask(mask),
                                         _mm512_loadu_si512(indices.Data()),
                                         _mm512_loadu_si512(storage.Data()),
                                         sizeof(std::int32_t));
        }

        [[nodiscard]] static inline auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(~MakeMask(mask)));
        }
        [[nodiscard]] static inline auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) & MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) | MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask16>(MakeMask(lhs) ^ MakeMask(rhs)));
        }
        [[nodiscard]] static inline auto MaskAny(const MaskType& mask) noexcept -> bool { return MakeMask(mask) != 0; }
        [[nodiscard]] static inline auto MaskAll(const MaskType& mask) noexcept -> bool { return MakeMask(mask) == 0xFFFF; }
    };

    template<class T>
    struct AVX512ByteOps : BackendTraits<ScalarTag, T>::template Ops<64>
    {
        static constexpr int LANES = 64;
        using BaseOps              = typename BackendTraits<ScalarTag, T>::template Ops<LANES>;
        using Storage              = typename BackendTraits<AVX512Tag, T>::template Storage<LANES>;
        using MaskType             = typename BackendTraits<AVX512Tag, T>::template MaskStorage<LANES>;

        static constexpr bool has_native_overrides = true;

        [[nodiscard]] static inline auto MaskFromRegister(__mmask64 bits) noexcept -> MaskType
        {
            return avx512_detail::MaskFromBits<LANES, MaskType>(bits);
        }
        [[nodiscard]] static inline auto MakeMask(const MaskType& mask) noexcept -> __mmask64
        {
            return avx512_detail::BitsFromMask<LANES, __mmask64>(mask);
        }
        [[nodiscard]] static inline auto Load(const T* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_loadu_si512(pointer));
            return result;
        }
        [[nodiscard]] static inline auto LoadAligned(const T* pointer) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_load_si512(pointer));
            return result;
        }
        [[nodiscard]] static inline auto LoadMasked(const T* pointer, const MaskType& mask, T fill) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(),
                                _mm512_mask_loadu_epi8(_mm512_set1_epi8(static_cast<char>(fill)), MakeMask(mask), pointer));
            return result;
        }
        static inline void Store(const Storage& storage, T* pointer) noexcept
        {
            _mm512_storeu_si512(pointer, _mm512_loadu_si512(storage.Data()));
        }
        static inline void StoreAligned(const Storage& storage, T* pointer) noexcept
        {
            _mm512_store_si512(pointer, _mm512_loadu_si512(storage.Data()));
        }
        static inline void StoreMasked(const Storage& storage, T* pointer, const MaskType& mask) noexcept
        {
            _mm512_mask_storeu_epi8(pointer, MakeMask(mask), _mm512_loadu_si512(storage.Data()));
        }

#define NGIN_SIMD_AVX512_BYTE_BINARY(Name, Intrinsic)                                                                  \
    [[nodiscard]] static inline auto Name(const Storage& lhs, const Storage& rhs) noexcept -> Storage                  \
    {                                                                                                                  \
        Storage result;                                                                                                \
        _mm512_storeu_si512(result.Data(), Intrinsic(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data()))); \
        return result;                                                                                                 \
    }

        NGIN_SIMD_AVX512_BYTE_BINARY(Add, _mm512_add_epi8)
        NGIN_SIMD_AVX512_BYTE_BINARY(Sub, _mm512_sub_epi8)
        NGIN_SIMD_AVX512_BYTE_BINARY(BitwiseAnd, _mm512_and_si512)
        NGIN_SIMD_AVX512_BYTE_BINARY(BitwiseOr, _mm512_or_si512)
        NGIN_SIMD_AVX512_BYTE_BINARY(BitwiseXor, _mm512_xor_si512)
#undef NGIN_SIMD_AVX512_BYTE_BINARY

        [[nodiscard]] static inline auto AndNot(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            _mm512_storeu_si512(result.Data(), _mm512_andnot_si512(_mm512_loadu_si512(rhs.Data()), _mm512_loadu_si512(lhs.Data())));
            return result;
        }
        [[nodiscard]] static inline auto Min(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            if constexpr (std::is_signed_v<T>)
            {
                _mm512_storeu_si512(result.Data(), _mm512_min_epi8(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data())));
            }
            else
            {
                _mm512_storeu_si512(result.Data(), _mm512_min_epu8(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data())));
            }
            return result;
        }
        [[nodiscard]] static inline auto Max(const Storage& lhs, const Storage& rhs) noexcept -> Storage
        {
            Storage result;
            if constexpr (std::is_signed_v<T>)
            {
                _mm512_storeu_si512(result.Data(), _mm512_max_epi8(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data())));
            }
            else
            {
                _mm512_storeu_si512(result.Data(), _mm512_max_epu8(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data())));
            }
            return result;
        }
        [[nodiscard]] static inline auto Abs(const Storage& value) noexcept -> Storage
        {
            if constexpr (std::is_signed_v<T>)
            {
                Storage result;
                _mm512_storeu_si512(result.Data(), _mm512_abs_epi8(_mm512_loadu_si512(value.Data())));
                return result;
            }
            else
            {
                return value;
            }
        }

        [[nodiscard]] static inline auto CompareEq(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(lhs.Data()), _mm512_loadu_si512(rhs.Data())));
        }
        [[nodiscard]] static inline auto CompareNe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(static_cast<__mmask64>(~_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(lhs.Data()),
                                                                                   _mm512_loadu_si512(rhs.Data()))));
        }
        [[nodiscard]] static inline auto CompareLt(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m512i left  = _mm512_loadu_si512(lhs.Data());
            const __m512i right = _mm512_loadu_si512(rhs.Data());
            if constexpr (std::is_signed_v<T>)
            {
                return MaskFromRegister(_mm512_cmp_epi8_mask(left, right, _MM_CMPINT_LT));
            }
            else
            {
                return MaskFromRegister(_mm512_cmp_epu8_mask(left, right, _MM_CMPINT_LT));
            }
        }
        [[nodiscard]] static inline auto CompareLe(const Storage& lhs, const Storage& rhs) noexcept -> MaskType
        {
            const __m512i left  = _mm512_loadu_si512(lhs.Data());
            const __m512i right = _mm512_loadu_si512(rhs.Data());
            if constexpr (std::is_signed_v<T>)
            {
                return MaskFromRegister(_mm512_cmp_epi8_mask(left, right, _MM_CMPINT_LE));
            }
            else
            {
                return MaskFromRegister(_mm512_cmp_epu8_mask(left, right, _MM_CMPINT_LE));
            }
        }

        [[nodiscard]] static inline auto MaskNot(const MaskType& mask) noexcept -> MaskType
        {
            return MaskFromRegister(~MakeMask(mask));
        }
        [[nodiscard]] static inline auto MaskAnd(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(MakeMask(lhs) & MakeMask(rhs));
        }
        [[nodiscard]] static inline auto MaskOr(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(MakeMask(lhs) | MakeMask(rhs));
        }
        [[nodiscard]] static inline auto MaskXor(const MaskType& lhs, const MaskType& rhs) noexcept -> MaskType
        {
            return MaskFromRegister(MakeMask(lhs) ^ MakeMask(rhs));
        }
        [[nodiscard]] static inline auto MaskAny(const MaskType& mask) noexcept -> bool { return MakeMask(mask) != 0; }
        [[nodiscard]] static inline auto MaskAll(const MaskType& mask) noexcept -> bool
        {
            return MakeMask(mask) == ~__mmask64 {0};
        }
    };

    template<>
    struct BackendTraits<AVX512Tag, std::uint8_t>::Ops<BackendTraits<AVX512Tag, std::uint8_t>::native_lanes>
        : AVX512ByteOps<std::uint8_t>
    {
    };

    template<>
    struct BackendTraits<AVX512Tag, std::int8_t>::Ops<BackendTraits<AVX512Tag, std::int8_t>::native_lanes>
        : AVX512ByteOps<std::int8_t>
    {
    };

}// namespace NGIN::SIMD::detail
