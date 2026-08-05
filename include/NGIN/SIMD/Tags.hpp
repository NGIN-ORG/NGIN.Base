#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Backend tag definitions and primary type forward declarations for the
// NGIN SIMD façade.

#include <concepts>

#include "NGIN/SIMD/Config.hpp"

namespace NGIN::SIMD
{

    // Backend tags ----------------------------------------------------------------
    // Tags encode the instruction-set family used to implement a particular SIMD
    // specialization. Extend this list as new backends are added.
    struct ScalarTag final
    {
    };
    struct SSE2Tag final
    {
    };
    struct AVX2Tag final
    {
    };
    struct AVX512Tag final
    {
    };
    struct NeonTag final
    {
    };

    // Math policy tags ------------------------------------------------------------
    struct StrictMathPolicy final
    {
    };
    struct FastMathPolicy final
    {
    };

    // Conversion mode tags -------------------------------------------------------
    struct ExactConversion final
    {
    };
    struct SaturateConversion final
    {
    };
    struct TruncateConversion final
    {
    };

    // Forward declarations --------------------------------------------------------
    template<class T, class Backend = NGIN_SIMD_DEFAULT_BACKEND, int Lanes = -1>
    struct Vec;

    template<int Lanes, class Backend = NGIN_SIMD_DEFAULT_BACKEND>
    struct Mask;

    /// @brief Reinterprets a SIMD object as an equal-sized target type without changing bits.
    template<class To, class From>
    [[nodiscard]] constexpr auto BitCast(const From& from) noexcept -> To;

    /// @brief Selects each result lane from @p ifTrue or @p ifFalse according to a mask.
    template<int Lanes, class Backend, class T>
    [[nodiscard]] constexpr auto Select(const Mask<Lanes, Backend>&   mask,
                                        const Vec<T, Backend, Lanes>& ifTrue,
                                        const Vec<T, Backend, Lanes>& ifFalse) noexcept -> Vec<T, Backend, Lanes>;

    // Concepts --------------------------------------------------------------------
    template<class V>
    concept SimdVecConcept = requires {
        typename V::value_type;
        { V::lanes } -> std::convertible_to<int>;
    };

    template<class V>
    inline constexpr int lanes_v = V::lanes;

    // Default backend aliases -----------------------------------------------------
    using DefaultBackend = NGIN_SIMD_DEFAULT_BACKEND;
    using MathPolicy     = NGIN_SIMD_MATH_POLICY;

}// namespace NGIN::SIMD
