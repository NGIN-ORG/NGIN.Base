#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Scalar baseline implementation of the NGIN SIMD façade. Future backends hook
// into the same interface by specializing the underlying storage operations.

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
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
            return BackendTraits<Backend, T>::native_lanes;
        }

        template<class Mode>
        inline constexpr bool IsSupportedConversionMode = std::is_same_v<Mode, ExactConversion> ||
                                                          std::is_same_v<Mode, SaturateConversion> ||
                                                          std::is_same_v<Mode, TruncateConversion>;

        template<class To, class From>
        [[nodiscard]] constexpr auto ClampIntegral(From value) noexcept -> To
        {
            using Common                          = std::common_type_t<From, To>;
            const auto                  promoted  = static_cast<Common>(value);
            const auto                  minValue  = static_cast<Common>(std::numeric_limits<To>::lowest());
            const auto                  maxValue  = static_cast<Common>(std::numeric_limits<To>::max());
            const auto                  clamped   = std::clamp(promoted, minValue, maxValue);
            [[maybe_unused]] const auto roundTrip = static_cast<Common>(static_cast<To>(clamped));
            return static_cast<To>(clamped);
        }

        template<class To, class From>
        [[nodiscard]] constexpr auto ExactConvertLane(From value) noexcept -> To
        {
            if constexpr (std::is_same_v<To, From>)
            {
                return value;
            }
            else if constexpr (std::is_integral_v<From> && std::is_integral_v<To>)
            {
                using Common        = std::common_type_t<From, To>;
                const auto promoted = static_cast<Common>(value);
                const auto minValue = static_cast<Common>(std::numeric_limits<To>::lowest());
                const auto maxValue = static_cast<Common>(std::numeric_limits<To>::max());
                if (!(promoted >= minValue && promoted <= maxValue))
                {
                    assert(!"Exact conversion out of range for integral types.");
                    const auto fallback = std::clamp(promoted, minValue, maxValue);
                    return static_cast<To>(fallback);
                }
                const auto converted = static_cast<To>(value);
                if (static_cast<Common>(converted) != promoted)
                {
                    assert(!"Exact conversion lost integral precision.");
                }
                return converted;
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>)
            {
                using Limits = std::numeric_limits<To>;
                if (!std::isfinite(value))
                {
                    assert(!"Exact conversion requires finite floating-point input.");
                    return std::signbit(value) ? Limits::lowest() : Limits::max();
                }
                if (std::trunc(value) != value)
                {
                    assert(!"Exact conversion requires integer-valued floating-point input.");
                }
                const auto minBound  = static_cast<long double>(Limits::lowest());
                const auto maxBound  = static_cast<long double>(Limits::max());
                const auto wideValue = static_cast<long double>(value);
                if (wideValue < minBound || wideValue > maxBound)
                {
                    assert(!"Exact conversion out of range for floating-to-integral conversion.");
                    return wideValue < minBound ? Limits::lowest() : Limits::max();
                }
                const auto converted = static_cast<To>(wideValue);
                if (static_cast<long double>(converted) != wideValue)
                {
                    assert(!"Exact conversion lost floating-to-integral precision.");
                    return wideValue < minBound ? Limits::lowest() : Limits::max();
                }
                return converted;
            }
            else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>)
            {
                const auto converted = static_cast<To>(value);
                if (!std::isfinite(converted) || static_cast<From>(converted) != value)
                {
                    assert(!"Exact conversion lost integral-to-floating precision.");
                }
                return converted;
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_floating_point_v<To>)
            {
                if (!std::isfinite(value))
                {
                    return static_cast<To>(value);
                }
                const auto converted = static_cast<To>(value);
                if (static_cast<From>(converted) != value)
                {
                    assert(!"Exact conversion lost floating-point precision.");
                }
                return converted;
            }
            else
            {
                static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<To>,
                              "Convert requires arithmetic lane types.");
                return static_cast<To>(value);
            }
        }

        template<class To, class From>
        [[nodiscard]] constexpr auto SaturateConvertLane(From value) noexcept -> To
        {
            if constexpr (std::is_same_v<To, From>)
            {
                return value;
            }
            else if constexpr (std::is_integral_v<From> && std::is_integral_v<To>)
            {
                return ClampIntegral<To>(value);
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>)
            {
                if (std::isnan(value))
                {
                    return static_cast<To>(0);
                }
                if (!std::isfinite(value))
                {
                    return std::signbit(value) ? std::numeric_limits<To>::lowest() : std::numeric_limits<To>::max();
                }
                using Limits        = std::numeric_limits<To>;
                const auto minBound = static_cast<long double>(Limits::lowest());
                const auto maxBound = static_cast<long double>(Limits::max());
                auto       rounded  = std::nearbyint(static_cast<long double>(value));
                if (!std::isfinite(rounded))
                {
                    rounded = static_cast<long double>(value);
                }
                rounded = std::clamp(rounded, minBound, maxBound);
                return static_cast<To>(rounded);
            }
            else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>)
            {
                return static_cast<To>(value);
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_floating_point_v<To>)
            {
                if (!std::isfinite(value))
                {
                    return static_cast<To>(value);
                }
                const auto minValue = static_cast<From>(std::numeric_limits<To>::lowest());
                const auto maxValue = static_cast<From>(std::numeric_limits<To>::max());
                const auto clamped  = std::clamp(value, minValue, maxValue);
                return static_cast<To>(clamped);
            }
            else
            {
                return static_cast<To>(value);
            }
        }

        template<class To, class From>
        [[nodiscard]] constexpr auto TruncateConvertLane(From value) noexcept -> To
        {
            if constexpr (std::is_same_v<To, From>)
            {
                return value;
            }
            else if constexpr (std::is_integral_v<From> && std::is_integral_v<To>)
            {
                return ClampIntegral<To>(value);
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>)
            {
                if (std::isnan(value))
                {
                    return static_cast<To>(0);
                }
                if (!std::isfinite(value))
                {
                    return std::signbit(value) ? std::numeric_limits<To>::lowest() : std::numeric_limits<To>::max();
                }
                using Limits         = std::numeric_limits<To>;
                const auto minBound  = static_cast<long double>(Limits::lowest());
                const auto maxBound  = static_cast<long double>(Limits::max());
                auto       truncated = std::trunc(static_cast<long double>(value));
                truncated            = std::clamp(truncated, minBound, maxBound);
                return static_cast<To>(truncated);
            }
            else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>)
            {
                return static_cast<To>(value);
            }
            else if constexpr (std::is_floating_point_v<From> && std::is_floating_point_v<To>)
            {
                if (!std::isfinite(value))
                {
                    return static_cast<To>(value);
                }
                const auto truncated = std::trunc(value);
                const auto minValue  = static_cast<From>(std::numeric_limits<To>::lowest());
                const auto maxValue  = static_cast<From>(std::numeric_limits<To>::max());
                const auto clamped   = std::clamp(truncated, minValue, maxValue);
                return static_cast<To>(clamped);
            }
            else
            {
                return static_cast<To>(value);
            }
        }

#if defined(__SSE2__) || defined(__AVX2__)
        namespace strict_vector_math
        {
            // Portions adapted from Julien Pommier's sse_mathfun (zlib license)
            // and Cephes (public domain) polynomial coefficients.

            inline constexpr float EXP_HI       = 88.3762626647949F;
            inline constexpr float EXP_LO       = -88.3762626647949F;
            inline constexpr float LOG2_E       = 1.44269504088896341F;
            inline constexpr float C1           = 0.693359375F;
            inline constexpr float C2           = -2.12194440e-4F;
            inline constexpr float MIN_NORM_POS = 1.17549435e-38F;
            inline constexpr float CEPHES_FOPI  = 1.27323954473516F;

            [[nodiscard]] inline auto ExpPs(__m128 x) noexcept -> __m128
            {
                __m128  one = _mm_set1_ps(1.0F);
                __m128i emm0;

                x = _mm_min_ps(x, _mm_set1_ps(EXP_HI));
                x = _mm_max_ps(x, _mm_set1_ps(EXP_LO));

                __m128 fx = _mm_mul_ps(x, _mm_set1_ps(LOG2_E));
                fx        = _mm_add_ps(fx, _mm_set1_ps(0.5F));

                emm0 = _mm_cvttps_epi32(fx);
                __m128 tmp = _mm_cvtepi32_ps(emm0);

                __m128 mask = _mm_cmpgt_ps(tmp, fx);
                mask        = _mm_and_ps(mask, one);
                fx          = _mm_sub_ps(tmp, mask);

                tmp = _mm_mul_ps(fx, _mm_set1_ps(C1));
                __m128 z = _mm_mul_ps(fx, _mm_set1_ps(C2));
                x        = _mm_sub_ps(x, tmp);
                x        = _mm_sub_ps(x, z);

                const __m128 c0 = _mm_set1_ps(1.9875691500e-4F);
                const __m128 c1 = _mm_set1_ps(1.3981999507e-3F);
                const __m128 c2 = _mm_set1_ps(8.3334519073e-3F);
                const __m128 c3 = _mm_set1_ps(4.1665795894e-2F);
                const __m128 c4 = _mm_set1_ps(1.6666665459e-1F);
                const __m128 c5 = _mm_set1_ps(5.0000001201e-1F);

                __m128 y = c0;
                y        = _mm_add_ps(_mm_mul_ps(y, x), c1);
                y        = _mm_add_ps(_mm_mul_ps(y, x), c2);
                y        = _mm_add_ps(_mm_mul_ps(y, x), c3);
                y        = _mm_add_ps(_mm_mul_ps(y, x), c4);
                y        = _mm_add_ps(_mm_mul_ps(y, x), c5);

                y = _mm_mul_ps(y, _mm_mul_ps(x, x));
                y = _mm_add_ps(y, x);
                y = _mm_add_ps(y, one);

                emm0 = _mm_cvttps_epi32(_mm_add_ps(fx, _mm_set1_ps(127.0F)));
                emm0 = _mm_slli_epi32(emm0, 23);
                __m128 pow2n = _mm_castsi128_ps(emm0);
                return _mm_mul_ps(y, pow2n);
            }

            [[nodiscard]] inline auto LogPs(__m128 x) noexcept -> __m128
            {
                __m128i emm0;
                __m128  one = _mm_set1_ps(1.0F);
                const __m128 original    = x;
                const __m128 invalidMask = _mm_cmple_ps(original, _mm_setzero_ps());

                x = _mm_max_ps(x, _mm_set1_ps(MIN_NORM_POS));
                emm0 = _mm_srli_epi32(_mm_castps_si128(x), 23);

                x = _mm_and_ps(x, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFF)));
                x = _mm_or_ps(x, _mm_set1_ps(0.5F));

                emm0 = _mm_sub_epi32(emm0, _mm_set1_epi32(0x7F));
                __m128 e = _mm_cvtepi32_ps(emm0);
                e        = _mm_add_ps(e, one);

                __m128 mask = _mm_cmplt_ps(x, _mm_set1_ps(0.707106781186547524F));
                __m128 tmp  = _mm_and_ps(x, mask);
                x           = _mm_sub_ps(x, one);
                e           = _mm_sub_ps(e, _mm_and_ps(one, mask));
                x           = _mm_add_ps(x, tmp);

                __m128 z = _mm_mul_ps(x, x);

                const __m128 p0 = _mm_set1_ps(7.0376836292e-2F);
                const __m128 p1 = _mm_set1_ps(-1.1514610310e-1F);
                const __m128 p2 = _mm_set1_ps(1.1676998740e-1F);
                const __m128 p3 = _mm_set1_ps(-1.2420140846e-1F);
                const __m128 p4 = _mm_set1_ps(1.4249322787e-1F);
                const __m128 p5 = _mm_set1_ps(-1.6668057665e-1F);
                const __m128 p6 = _mm_set1_ps(2.0000714765e-1F);
                const __m128 p7 = _mm_set1_ps(-2.4999993993e-1F);
                const __m128 p8 = _mm_set1_ps(3.3333331174e-1F);

                __m128 y = p0;
                y        = _mm_add_ps(_mm_mul_ps(y, x), p1);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p2);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p3);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p4);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p5);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p6);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p7);
                y        = _mm_add_ps(_mm_mul_ps(y, x), p8);

                y = _mm_mul_ps(y, x);
                y = _mm_add_ps(y, _mm_mul_ps(_mm_set1_ps(-0.5F), z));
                y = _mm_add_ps(y, x);

                y = _mm_add_ps(y, _mm_mul_ps(e, _mm_set1_ps(C2)));
                y = _mm_add_ps(y, _mm_mul_ps(e, _mm_set1_ps(C1)));

                const __m128 zeroMask = _mm_cmpeq_ps(original, _mm_setzero_ps());
                const __m128 negMask  = _mm_cmplt_ps(original, _mm_setzero_ps());
                const __m128 nanVec   = _mm_set1_ps(std::numeric_limits<float>::quiet_NaN());
                const __m128 negInf   = _mm_set1_ps(-std::numeric_limits<float>::infinity());

                __m128 result = y;
                result        = _mm_or_ps(_mm_and_ps(zeroMask, negInf), _mm_andnot_ps(zeroMask, result));
                result        = _mm_or_ps(_mm_and_ps(negMask, nanVec), _mm_andnot_ps(negMask, result));
                result        = _mm_or_ps(_mm_and_ps(invalidMask, nanVec), _mm_andnot_ps(invalidMask, result));
                return result;
            }

            [[nodiscard]] inline auto SqrtPs(__m128 value) noexcept -> __m128
            {
                return _mm_sqrt_ps(value);
            }

            inline void SinCosPs(__m128 x, __m128& outSin, __m128& outCos) noexcept
            {
                __m128 signSin = _mm_and_ps(x, _mm_set1_ps(-0.0F));
                x              = _mm_and_ps(x, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)));

                __m128 y = _mm_mul_ps(x, _mm_set1_ps(CEPHES_FOPI));
                __m128i emm2 = _mm_cvttps_epi32(y);
                emm2         = _mm_add_epi32(emm2, _mm_set1_epi32(1));
                emm2         = _mm_and_si128(emm2, _mm_set1_epi32(~1));
                y            = _mm_cvtepi32_ps(emm2);

                __m128i emm0 = _mm_and_si128(emm2, _mm_set1_epi32(4));
                emm0         = _mm_slli_epi32(emm0, 29);
                __m128 swapSign = _mm_castsi128_ps(emm0);
                signSin        = _mm_xor_ps(signSin, swapSign);

                __m128i emm4 = _mm_sub_epi32(emm2, _mm_set1_epi32(2));
                emm4          = _mm_and_si128(emm4, _mm_set1_epi32(4));
                __m128 signCos = _mm_castsi128_ps(emm4);

                emm0 = _mm_and_si128(emm2, _mm_set1_epi32(2));
                emm0 = _mm_cmpeq_epi32(emm0, _mm_setzero_si128());
                __m128 polyMask = _mm_castsi128_ps(emm0);

                const __m128 pi1 = _mm_set1_ps(1.5707962512969971F);
                const __m128 pi2 = _mm_set1_ps(7.5497894158615964e-08F);
                const __m128 pi3 = _mm_set1_ps(5.390302529957764e-15F);
                __m128       xr  = _mm_sub_ps(x, _mm_mul_ps(y, pi1));
                xr                = _mm_sub_ps(xr, _mm_mul_ps(y, pi2));
                xr                = _mm_sub_ps(xr, _mm_mul_ps(y, pi3));

                __m128 z = _mm_mul_ps(xr, xr);

                const __m128 sincof_p0 = _mm_set1_ps(-1.9515295891e-4F);
                const __m128 sincof_p1 = _mm_set1_ps(8.3321608736e-3F);
                const __m128 sincof_p2 = _mm_set1_ps(-1.6666654611e-1F);

                __m128 sinPoly = sincof_p0;
                sinPoly        = _mm_add_ps(_mm_mul_ps(sinPoly, z), sincof_p1);
                sinPoly        = _mm_add_ps(_mm_mul_ps(sinPoly, z), sincof_p2);
                sinPoly        = _mm_mul_ps(sinPoly, _mm_mul_ps(z, xr));
                sinPoly        = _mm_add_ps(sinPoly, xr);

                const __m128 coscof_p0 = _mm_set1_ps(2.443315711809948e-5F);
                const __m128 coscof_p1 = _mm_set1_ps(-1.388731625493765e-3F);
                const __m128 coscof_p2 = _mm_set1_ps(4.166664568298827e-2F);

                __m128 cosPoly = coscof_p0;
                cosPoly        = _mm_add_ps(_mm_mul_ps(cosPoly, z), coscof_p1);
                cosPoly        = _mm_add_ps(_mm_mul_ps(cosPoly, z), coscof_p2);
                cosPoly        = _mm_mul_ps(cosPoly, _mm_mul_ps(z, z));
                cosPoly        = _mm_add_ps(cosPoly, _mm_mul_ps(_mm_set1_ps(-0.5F), z));
                cosPoly        = _mm_add_ps(cosPoly, _mm_set1_ps(1.0F));

                __m128 sinResult = _mm_or_ps(_mm_and_ps(polyMask, sinPoly), _mm_andnot_ps(polyMask, cosPoly));
                __m128 cosResult = _mm_or_ps(_mm_and_ps(polyMask, cosPoly), _mm_andnot_ps(polyMask, sinPoly));

                outSin = _mm_xor_ps(sinResult, signSin);
                outCos = _mm_xor_ps(cosResult, signCos);
            }

            [[nodiscard]] inline auto SinPs(__m128 value) noexcept -> __m128
            {
                __m128 s;
                __m128 c;
                SinCosPs(value, s, c);
                return s;
            }

            [[nodiscard]] inline auto CosPs(__m128 value) noexcept -> __m128
            {
                __m128 s;
                __m128 c;
                SinCosPs(value, s, c);
                return c;
            }

#if defined(__AVX2__)
            [[nodiscard]] inline auto ExpPs256(__m256 value) noexcept -> __m256
            {
                const __m128 low  = ExpPs(_mm256_castps256_ps128(value));
                const __m128 high = ExpPs(_mm256_extractf128_ps(value, 1));
                return _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1);
            }

            [[nodiscard]] inline auto LogPs256(__m256 value) noexcept -> __m256
            {
                const __m128 low  = LogPs(_mm256_castps256_ps128(value));
                const __m128 high = LogPs(_mm256_extractf128_ps(value, 1));
                return _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1);
            }

            [[nodiscard]] inline auto SinPs256(__m256 value) noexcept -> __m256
            {
                const __m128 low  = SinPs(_mm256_castps256_ps128(value));
                const __m128 high = SinPs(_mm256_extractf128_ps(value, 1));
                return _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1);
            }

            [[nodiscard]] inline auto CosPs256(__m256 value) noexcept -> __m256
            {
                const __m128 low  = CosPs(_mm256_castps256_ps128(value));
                const __m128 high = CosPs(_mm256_extractf128_ps(value, 1));
                return _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1);
            }

            [[nodiscard]] inline auto SqrtPs256(__m256 value) noexcept -> __m256
            {
                return _mm256_sqrt_ps(value);
            }
#endif

        }// namespace strict_vector_math

        namespace fast_math_detail
        {
            inline constexpr float LN2                 = 0.69314718055994530942F;
            inline constexpr float LN2_HIGH            = 0.693359375F;
            inline constexpr float LN2_LOW             = -2.12194440e-4F;
            inline constexpr float LOG2_E              = 1.4426950408889634074F;
            inline constexpr float EXP_MAX_INPUT       = 88.72283905206835F;
            inline constexpr float EXP_MIN_INPUT       = -87.33654475F;
            inline constexpr float SQRT_HALF           = 0.70710678118654752440F;
            inline constexpr float HALF_PI_HIGH        = 1.57079625129699707031F;
            inline constexpr float HALF_PI_LOW         = 7.54978941586159635335e-08F;
            inline constexpr float INV_HALF_PI         = 0.63661977236758134308F;
            inline constexpr float TWO_POW_24          = 16777216.0F;

            struct AngleReductionResult
            {
                float reduced;
                int   quadrant;
            };

            [[nodiscard]] inline auto ReduceToHalfPi(float angle) noexcept -> AngleReductionResult
            {
                if (!std::isfinite(angle))
                {
                    return {std::numeric_limits<float>::quiet_NaN(), 0};
                }

                const float k        = std::round(angle * INV_HALF_PI);
                const float highPart = k * HALF_PI_HIGH;
                const float reduced  = (angle - highPart) - k * HALF_PI_LOW;
                const int   quadrant = static_cast<int>(k) & 0x3;
                return {reduced, quadrant};
            }

            [[nodiscard]] inline constexpr auto EvaluateSinPolynomial(float x) noexcept -> float
            {
                const float x2 = x * x;
                return x + x * x2 * (-1.6666667163e-1F +
                                     x2 * (8.3333337680e-3F +
                                           x2 * (-1.9841270114e-4F + x2 * 2.7557314297e-6F)));
            }

            [[nodiscard]] inline constexpr auto EvaluateCosPolynomial(float x) noexcept -> float
            {
                const float x2 = x * x;
                return 1.0F + x2 * (-5.0000000745e-1F +
                                    x2 * (4.1666645683e-2F +
                                          x2 * (-1.3887316255e-3F + x2 * 2.4801587642e-5F)));
            }

            [[nodiscard]] inline auto EvaluateExpPolynomial(float residual) noexcept -> float
            {
                constexpr float c0 = 1.9875691500e-4F;
                constexpr float c1 = 1.3981999507e-3F;
                constexpr float c2 = 8.3334519073e-3F;
                constexpr float c3 = 4.1665795894e-2F;
                constexpr float c4 = 1.6666665459e-1F;
                constexpr float c5 = 5.0000001201e-1F;

                const float     r2   = residual * residual;
                const float     poly = (((((c0 * residual + c1) * residual + c2) * residual + c3) * residual + c4) * residual + c5) * r2;
                return poly + residual + 1.0F;
            }

            [[nodiscard]] inline auto FastExpFloat(float value) noexcept -> float
            {
                if (std::isnan(value))
                {
                    return value;
                }
                if (value > EXP_MAX_INPUT)
                {
                    return std::numeric_limits<float>::infinity();
                }
                if (value < EXP_MIN_INPUT)
                {
                    return 0.0F;
                }

                const float scaled   = value * LOG2_E;
                const int   exponent = static_cast<int>(std::round(scaled));
                const float residual = (value - static_cast<float>(exponent) * LN2_HIGH) - static_cast<float>(exponent) * LN2_LOW;
                const float mantissa = EvaluateExpPolynomial(residual);
                const int   biased   = exponent + 127;
                const auto  bits     = static_cast<std::uint32_t>(biased) << 23U;
                const float pow2     = std::bit_cast<float>(bits);
                return mantissa * pow2;
            }

            [[nodiscard]] inline auto FastLogFloat(float value) noexcept -> float
            {
                if (std::isnan(value))
                {
                    return value;
                }
                if (value < 0.0F)
                {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                if (value == 0.0F)
                {
                    return -std::numeric_limits<float>::infinity();
                }
                if (std::isinf(value))
                {
                    return value;
                }

                std::uint32_t bits     = std::bit_cast<std::uint32_t>(value);
                int           exponent = 0;
                if ((bits & 0x7F800000U) == 0U)
                {
                    value *= TWO_POW_24;
                    bits     = std::bit_cast<std::uint32_t>(value);
                    exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127 - 24;
                }
                else
                {
                    exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127;
                }

                std::uint32_t mantissaBits = (bits & 0x007FFFFFU) | 0x3F800000U;
                float         mantissa     = std::bit_cast<float>(mantissaBits);
                if (mantissa < SQRT_HALF)
                {
                    mantissa *= 2.0F;
                    --exponent;
                }

                const float y  = mantissa - 1.0F;
                const float p  = y * (1.0F + y * (-0.5F + y * (0.333333343F + y * (-0.25F + y * 0.2F))));
                const float ln = static_cast<float>(exponent) * LN2 + p;
                return ln;
            }

            [[nodiscard]] inline auto FastSinFloat(float value) noexcept -> float
            {
                if (std::isnan(value))
                {
                    return value;
                }
                if (std::isinf(value))
                {
                    return std::numeric_limits<float>::quiet_NaN();
                }

                const auto reduction = ReduceToHalfPi(value);
                if (std::isnan(reduction.reduced))
                {
                    return reduction.reduced;
                }

                const float sinBase = EvaluateSinPolynomial(reduction.reduced);
                const float cosBase = EvaluateCosPolynomial(reduction.reduced);

                switch (reduction.quadrant)
                {
                case 0:
                    return sinBase;
                case 1:
                    return cosBase;
                case 2:
                    return -sinBase;
                default:
                    return -cosBase;
                }
            }

            [[nodiscard]] inline auto FastCosFloat(float value) noexcept -> float
            {
                if (std::isnan(value))
                {
                    return value;
                }
                if (std::isinf(value))
                {
                    return std::numeric_limits<float>::quiet_NaN();
                }

                const auto reduction = ReduceToHalfPi(value);
                if (std::isnan(reduction.reduced))
                {
                    return reduction.reduced;
                }

                const float sinBase = EvaluateSinPolynomial(reduction.reduced);
                const float cosBase = EvaluateCosPolynomial(reduction.reduced);

                switch (reduction.quadrant)
                {
                case 0:
                    return cosBase;
                case 1:
                    return -sinBase;
                case 2:
                    return -cosBase;
                default:
                    return sinBase;
                }
            }

            [[nodiscard]] inline auto FastSqrtFloat(float value) noexcept -> float
            {
                if (value < 0.0F)
                {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                if (std::isnan(value) || value == 0.0F || std::isinf(value))
                {
                    return value;
                }

#if defined(__SSE__)
                const __m128 input    = _mm_set_ss(value);
                const __m128 rsqrt    = _mm_rsqrt_ss(input);
                float        inv      = _mm_cvtss_f32(rsqrt);
                const float  half     = 0.5F * value;
                inv                   = inv * (1.5F - half * inv * inv);
                inv                   = inv * (1.5F - half * inv * inv);
                const float sqrtValue = value * inv;
                return sqrtValue;
#else
                return std::sqrt(value);
#endif
            }
        }// namespace fast_math_detail
#endif// defined(__SSE2__) || defined(__AVX2__)

        template<class Policy>
        inline constexpr bool IsSupportedMathPolicy = std::is_same_v<Policy, StrictMathPolicy> ||
                                                      std::is_same_v<Policy, FastMathPolicy>;

        template<class Policy, class Backend, class T>
        struct MathPolicyLane;

        template<class Backend, class T>
        struct MathPolicyLane<StrictMathPolicy, Backend, T>
        {
            static_assert(std::is_floating_point_v<T>, "StrictMathPolicy requires floating-point lane types.");

            [[nodiscard]] static auto Exp(T value) noexcept -> T
            {
                const auto promoted = static_cast<long double>(value);
                const auto result   = std::exp(promoted);
                return static_cast<T>(result);
            }

            [[nodiscard]] static auto Log(T value) noexcept -> T
            {
                const auto promoted = static_cast<long double>(value);
                const auto result   = std::log(promoted);
                return static_cast<T>(result);
            }

            [[nodiscard]] static auto Sin(T value) noexcept -> T
            {
                const auto promoted = static_cast<long double>(value);
                const auto result   = std::sin(promoted);
                return static_cast<T>(result);
            }

            [[nodiscard]] static auto Cos(T value) noexcept -> T
            {
                const auto promoted = static_cast<long double>(value);
                const auto result   = std::cos(promoted);
                return static_cast<T>(result);
            }

            [[nodiscard]] static auto Sqrt(T value) noexcept -> T
            {
                const auto promoted = static_cast<long double>(value);
                const auto result   = std::sqrt(promoted);
                return static_cast<T>(result);
            }
        };

	        template<class Backend, class T>
	        struct MathPolicyLane<FastMathPolicy, Backend, T> : MathPolicyLane<StrictMathPolicy, Backend, T>
	        {
	            static_assert(std::is_floating_point_v<T>, "FastMathPolicy requires floating-point lane types.");

	            [[nodiscard]] static auto Exp(T value) noexcept -> T
	            {
	                return std::exp(value);
	            }

	            [[nodiscard]] static auto Log(T value) noexcept -> T
	            {
	                return std::log(value);
	            }

	            [[nodiscard]] static auto Sin(T value) noexcept -> T
	            {
	                return std::sin(value);
	            }

	            [[nodiscard]] static auto Cos(T value) noexcept -> T
	            {
	                return std::cos(value);
	            }

	            [[nodiscard]] static auto Sqrt(T value) noexcept -> T
	            {
	                return std::sqrt(value);
	            }
	        };

        template<class Policy, class VecType>
        struct VectorizedMath
        {
            static constexpr bool supportsExp  = false;
            static constexpr bool supportsLog  = false;
            static constexpr bool supportsSin  = false;
            static constexpr bool supportsCos  = false;
            static constexpr bool supportsSqrt = false;
        };

#if defined(__SSE2__)
        template<int Lanes>
        struct VectorizedMath<StrictMathPolicy, Vec<float, SSE2Tag, Lanes>>
        {
            using VecType = Vec<float, SSE2Tag, Lanes>;
            static constexpr bool lane_match = VecType::lanes == detail::BackendTraits<SSE2Tag, float>::native_lanes;
            static constexpr bool supportsExp  = false;
            static constexpr bool supportsLog  = false;
            static constexpr bool supportsSin  = false;
            static constexpr bool supportsCos  = false;
            static constexpr bool supportsSqrt = lane_match;

            [[nodiscard]] static auto Exp(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm_storeu_ps(result.storage.Data(), strict_vector_math::ExpPs(_mm_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Log(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm_storeu_ps(result.storage.Data(), strict_vector_math::LogPs(_mm_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Sin(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm_storeu_ps(result.storage.Data(), strict_vector_math::SinPs(_mm_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Cos(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm_storeu_ps(result.storage.Data(), strict_vector_math::CosPs(_mm_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Sqrt(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm_storeu_ps(result.storage.Data(), strict_vector_math::SqrtPs(_mm_loadu_ps(value.storage.Data())));
                return result;
            }
        };
#endif

#if defined(__AVX2__)
        template<int Lanes>
        struct VectorizedMath<StrictMathPolicy, Vec<float, AVX2Tag, Lanes>>
        {
            using VecType = Vec<float, AVX2Tag, Lanes>;
            static constexpr bool lane_match = VecType::lanes == detail::BackendTraits<AVX2Tag, float>::native_lanes;
            static constexpr bool supportsExp  = false;
            static constexpr bool supportsLog  = false;
            static constexpr bool supportsSin  = false;
            static constexpr bool supportsCos  = false;
            static constexpr bool supportsSqrt = lane_match;

            [[nodiscard]] static auto Exp(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm256_storeu_ps(result.storage.Data(), strict_vector_math::ExpPs256(_mm256_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Log(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm256_storeu_ps(result.storage.Data(), strict_vector_math::LogPs256(_mm256_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Sin(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm256_storeu_ps(result.storage.Data(), strict_vector_math::SinPs256(_mm256_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Cos(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm256_storeu_ps(result.storage.Data(), strict_vector_math::CosPs256(_mm256_loadu_ps(value.storage.Data())));
                return result;
            }

            [[nodiscard]] static auto Sqrt(const VecType& value) noexcept -> VecType
            {
                static_assert(lane_match);
                VecType result;
                _mm256_storeu_ps(result.storage.Data(), strict_vector_math::SqrtPs256(_mm256_loadu_ps(value.storage.Data())));
                return result;
            }
        };
#endif

#if defined(__SSE2__)
        template<>
        struct MathPolicyLane<FastMathPolicy, SSE2Tag, float>
        {
            [[nodiscard]] static auto Exp(float value) noexcept -> float
            {
                return fast_math_detail::FastExpFloat(value);
            }

            [[nodiscard]] static auto Log(float value) noexcept -> float
            {
                return fast_math_detail::FastLogFloat(value);
            }

            [[nodiscard]] static auto Sin(float value) noexcept -> float
            {
                return fast_math_detail::FastSinFloat(value);
            }

            [[nodiscard]] static auto Cos(float value) noexcept -> float
            {
                return fast_math_detail::FastCosFloat(value);
            }

            [[nodiscard]] static auto Sqrt(float value) noexcept -> float
            {
                return fast_math_detail::FastSqrtFloat(value);
            }
        };
#endif

#if defined(__AVX2__)
        template<>
        struct MathPolicyLane<FastMathPolicy, AVX2Tag, float>
        {
            [[nodiscard]] static auto Exp(float value) noexcept -> float
            {
                return fast_math_detail::FastExpFloat(value);
            }

            [[nodiscard]] static auto Log(float value) noexcept -> float
            {
                return fast_math_detail::FastLogFloat(value);
            }

            [[nodiscard]] static auto Sin(float value) noexcept -> float
            {
                return fast_math_detail::FastSinFloat(value);
            }

            [[nodiscard]] static auto Cos(float value) noexcept -> float
            {
                return fast_math_detail::FastCosFloat(value);
            }

            [[nodiscard]] static auto Sqrt(float value) noexcept -> float
            {
                return fast_math_detail::FastSqrtFloat(value);
            }
        };
#endif

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

    template<class To,
             class Mode = ExactConversion,
             class FromT,
             class Backend,
             int Lanes>
        requires std::is_arithmetic_v<To> && std::is_arithmetic_v<FromT>
    [[nodiscard]] constexpr auto Convert(const Vec<FromT, Backend, Lanes>& value) noexcept
            -> Vec<To, Backend, Vec<FromT, Backend, Lanes>::lanes>
    {
        static_assert(detail::IsSupportedConversionMode<Mode>,
                      "Unsupported conversion mode; use ExactConversion, SaturateConversion, or TruncateConversion.");

        using SourceVec = Vec<FromT, Backend, Lanes>;
        using ResultVec = Vec<To, Backend, SourceVec::lanes>;

        ResultVec result;
        for (int lane = 0; lane < ResultVec::lanes; ++lane)
        {
            const auto laneValue = value.GetLane(lane);
            if constexpr (std::is_same_v<Mode, ExactConversion>)
            {
                result.storage.Set(lane, detail::ExactConvertLane<To>(laneValue));
            }
            else if constexpr (std::is_same_v<Mode, SaturateConversion>)
            {
                result.storage.Set(lane, detail::SaturateConvertLane<To>(laneValue));
            }
            else
            {
                result.storage.Set(lane, detail::TruncateConvertLane<To>(laneValue));
            }
        }
        return result;
    }

    namespace detail
    {

        template<class Policy, class T, class Backend, int Lanes>
        struct MathPolicyAdapter
        {
            using VecType = Vec<T, Backend, Lanes>;
            using LaneOps = MathPolicyLane<Policy, Backend, T>;
            using Vectorized = VectorizedMath<Policy, VecType>;

            [[nodiscard]] static auto Exp(const VecType& value) noexcept -> VecType
            {
                if constexpr (Vectorized::supportsExp)
                {
                    return Vectorized::Exp(value);
                }
                else
                {
                    VecType result;
                    for (int lane = 0; lane < VecType::lanes; ++lane)
                    {
                        result.storage.Set(lane, LaneOps::Exp(value.GetLane(lane)));
                    }
                    return result;
                }
            }

            [[nodiscard]] static auto Log(const VecType& value) noexcept -> VecType
            {
                if constexpr (Vectorized::supportsLog)
                {
                    return Vectorized::Log(value);
                }
                else
                {
                    VecType result;
                    for (int lane = 0; lane < VecType::lanes; ++lane)
                    {
                        result.storage.Set(lane, LaneOps::Log(value.GetLane(lane)));
                    }
                    return result;
                }
            }

            [[nodiscard]] static auto Sin(const VecType& value) noexcept -> VecType
            {
                if constexpr (Vectorized::supportsSin)
                {
                    return Vectorized::Sin(value);
                }
                else
                {
                    VecType result;
                    for (int lane = 0; lane < VecType::lanes; ++lane)
                    {
                        result.storage.Set(lane, LaneOps::Sin(value.GetLane(lane)));
                    }
                    return result;
                }
            }

            [[nodiscard]] static auto Cos(const VecType& value) noexcept -> VecType
            {
                if constexpr (Vectorized::supportsCos)
                {
                    return Vectorized::Cos(value);
                }
                else
                {
                    VecType result;
                    for (int lane = 0; lane < VecType::lanes; ++lane)
                    {
                        result.storage.Set(lane, LaneOps::Cos(value.GetLane(lane)));
                    }
                    return result;
                }
            }

            [[nodiscard]] static auto Sqrt(const VecType& value) noexcept -> VecType
            {
                if constexpr (Vectorized::supportsSqrt)
                {
                    return Vectorized::Sqrt(value);
                }
                else
                {
                    VecType result;
                    for (int lane = 0; lane < VecType::lanes; ++lane)
                    {
                        result.storage.Set(lane, LaneOps::Sqrt(value.GetLane(lane)));
                    }
                    return result;
                }
            }
        };

    }// namespace detail

    template<class Policy = MathPolicy, class T, class Backend, int Lanes>
        requires detail::IsSupportedMathPolicy<Policy>
    [[nodiscard]] inline auto Exp(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        return detail::MathPolicyAdapter<Policy, T, Backend, Lanes>::Exp(value);
    }

    template<class Policy = MathPolicy, class T, class Backend, int Lanes>
        requires detail::IsSupportedMathPolicy<Policy>
    [[nodiscard]] inline auto Log(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        return detail::MathPolicyAdapter<Policy, T, Backend, Lanes>::Log(value);
    }

    template<class Policy = MathPolicy, class T, class Backend, int Lanes>
        requires detail::IsSupportedMathPolicy<Policy>
    [[nodiscard]] inline auto Sin(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        return detail::MathPolicyAdapter<Policy, T, Backend, Lanes>::Sin(value);
    }

    template<class Policy = MathPolicy, class T, class Backend, int Lanes>
        requires detail::IsSupportedMathPolicy<Policy>
    [[nodiscard]] inline auto Cos(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        return detail::MathPolicyAdapter<Policy, T, Backend, Lanes>::Cos(value);
    }

    template<class Policy = MathPolicy, class T, class Backend, int Lanes>
        requires detail::IsSupportedMathPolicy<Policy>
    [[nodiscard]] inline auto Sqrt(const Vec<T, Backend, Lanes>& value) noexcept -> Vec<T, Backend, Lanes>
    {
        return detail::MathPolicyAdapter<Policy, T, Backend, Lanes>::Sqrt(value);
    }

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
