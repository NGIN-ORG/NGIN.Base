#pragma once

// SPDX-License-Identifier: Apache-2.0

/// @file Runtime.hpp
/// @brief Runtime CPU feature detection and selection for separately compiled SIMD kernels.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "NGIN/Defines.hpp"

namespace NGIN::SIMD
{

    /// @brief SIMD instruction-set families understood by the runtime dispatcher.
    enum class RuntimeBackend : std::uint8_t
    {
        Scalar,
        SSE2,
        AVX2,
        AVX512,
        Neon,
    };

    /// @brief CPU and operating-system SIMD state detected for the current process.
    struct RuntimeFeatures final
    {
        bool sse2     = false;
        bool avx2     = false;
        bool avx512f  = false;
        bool avx512bw = false;
        bool avx512dq = false;
        bool avx512vl = false;
        bool neon     = false;

        /// @brief Returns whether all features required by @p backend are usable.
        [[nodiscard]] constexpr auto Supports(RuntimeBackend backend) const noexcept -> bool
        {
            switch (backend)
            {
                case RuntimeBackend::Scalar:
                    return true;
                case RuntimeBackend::SSE2:
                    return sse2;
                case RuntimeBackend::AVX2:
                    return avx2;
                case RuntimeBackend::AVX512:
                    return avx512f && avx512bw && avx512dq && avx512vl;
                case RuntimeBackend::Neon:
                    return neon;
            }
            return false;
        }
    };

    /// @brief SIMD variants compiled into the linked NGIN.Base Foundation library.
    struct CompiledBackends final
    {
        bool scalar = true;
        bool sse2   = false;
        bool avx2   = false;
        bool avx512 = false;
        bool neon   = false;

        /// @brief Returns whether @p backend has a linked implementation.
        [[nodiscard]] constexpr auto Contains(RuntimeBackend backend) const noexcept -> bool
        {
            switch (backend)
            {
                case RuntimeBackend::Scalar:
                    return scalar;
                case RuntimeBackend::SSE2:
                    return sse2;
                case RuntimeBackend::AVX2:
                    return avx2;
                case RuntimeBackend::AVX512:
                    return avx512;
                case RuntimeBackend::Neon:
                    return neon;
            }
            return false;
        }
    };

    /// @brief Detects SIMD features usable by both the CPU and operating system.
    [[nodiscard]] NGIN_FOUNDATION_API auto DetectRuntimeFeatures() noexcept -> RuntimeFeatures;

    /// @brief Returns the cached feature set for the current process.
    [[nodiscard]] NGIN_FOUNDATION_API auto GetRuntimeFeatures() noexcept -> const RuntimeFeatures&;

    /// @brief Returns the SIMD variants compiled into the linked Foundation library.
    [[nodiscard]] NGIN_FOUNDATION_API auto GetCompiledBackends() noexcept -> CompiledBackends;

    /// @brief Selects the highest usable compiled backend.
    [[nodiscard]] constexpr auto SelectRuntimeBackend(const RuntimeFeatures&  features,
                                                      const CompiledBackends& compiled) noexcept -> RuntimeBackend
    {
        if (compiled.avx512 && features.Supports(RuntimeBackend::AVX512))
        {
            return RuntimeBackend::AVX512;
        }
        if (compiled.avx2 && features.Supports(RuntimeBackend::AVX2))
        {
            return RuntimeBackend::AVX2;
        }
        if (compiled.sse2 && features.Supports(RuntimeBackend::SSE2))
        {
            return RuntimeBackend::SSE2;
        }
        if (compiled.neon && features.Supports(RuntimeBackend::Neon))
        {
            return RuntimeBackend::Neon;
        }
        return RuntimeBackend::Scalar;
    }

    /// @brief Returns the backend selected for the linked Foundation library.
    [[nodiscard]] NGIN_FOUNDATION_API auto GetRuntimeBackend() noexcept -> RuntimeBackend;

    /// @brief Returns a stable display name for @p backend.
    [[nodiscard]] constexpr auto RuntimeBackendName(RuntimeBackend backend) noexcept -> std::string_view
    {
        switch (backend)
        {
            case RuntimeBackend::Scalar:
                return "Scalar";
            case RuntimeBackend::SSE2:
                return "SSE2";
            case RuntimeBackend::AVX2:
                return "AVX2";
            case RuntimeBackend::AVX512:
                return "AVX-512";
            case RuntimeBackend::Neon:
                return "NEON";
        }
        return "Unknown";
    }

    /// @brief A set of separately compiled function variants resolved from runtime CPU features.
    ///
    /// Function must be a function-pointer type. Null entries are skipped, allowing callers to
    /// compile only the ISA variants supported by their toolchain and deployment policy.
    template<class Function>
        requires std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>
    struct RuntimeDispatchTable final
    {
        Function scalar {};
        Function sse2 {};
        Function avx2 {};
        Function avx512 {};
        Function neon {};

        /// @brief Resolves the highest non-null variant supported by @p features.
        /// @return A callable variant, or null when the table has no scalar fallback.
        [[nodiscard]] constexpr auto Resolve(const RuntimeFeatures& features) const noexcept -> Function
        {
            if (avx512 != nullptr && features.Supports(RuntimeBackend::AVX512))
            {
                return avx512;
            }
            if (avx2 != nullptr && features.Supports(RuntimeBackend::AVX2))
            {
                return avx2;
            }
            if (sse2 != nullptr && features.Supports(RuntimeBackend::SSE2))
            {
                return sse2;
            }
            if (neon != nullptr && features.Supports(RuntimeBackend::Neon))
            {
                return neon;
            }
            return scalar;
        }

        /// @brief Resolves against the cached features for the current process.
        [[nodiscard]] auto Resolve() const noexcept -> Function
        {
            return Resolve(GetRuntimeFeatures());
        }
    };

}// namespace NGIN::SIMD
