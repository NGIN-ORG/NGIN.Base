// SPDX-License-Identifier: Apache-2.0

#include "NGIN/SIMD/Runtime.hpp"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

#if defined(__linux__) && defined(__arm__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

namespace NGIN::SIMD
{
    namespace
    {
        [[nodiscard]] auto DetectX86Features() noexcept -> RuntimeFeatures
        {
            RuntimeFeatures features {};

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
            __builtin_cpu_init();
            features.sse2     = __builtin_cpu_supports("sse2") != 0;
            features.avx2     = __builtin_cpu_supports("avx2") != 0;
            features.avx512f  = __builtin_cpu_supports("avx512f") != 0;
            features.avx512bw = __builtin_cpu_supports("avx512bw") != 0;
            features.avx512dq = __builtin_cpu_supports("avx512dq") != 0;
            features.avx512vl = __builtin_cpu_supports("avx512vl") != 0;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
            int registers[4] {};
            __cpuid(registers, 0);
            const int maximumLeaf = registers[0];
            if (maximumLeaf < 1)
            {
                return features;
            }

            __cpuidex(registers, 1, 0);
            features.sse2       = (registers[3] & (1 << 26)) != 0;
            const bool hasAvx   = (registers[2] & (1 << 28)) != 0;
            const bool hasXsave = (registers[2] & (1 << 27)) != 0;
            if (!hasAvx || !hasXsave)
            {
                return features;
            }

            const unsigned __int64 xcr0        = _xgetbv(0);
            const bool             avxState    = (xcr0 & 0x6) == 0x6;
            const bool             avx512State = (xcr0 & 0xE6) == 0xE6;
            if (!avxState || maximumLeaf < 7)
            {
                return features;
            }

            __cpuidex(registers, 7, 0);
            features.avx2 = (registers[1] & (1 << 5)) != 0;
            if (avx512State)
            {
                features.avx512f  = (registers[1] & (1 << 16)) != 0;
                features.avx512dq = (registers[1] & (1 << 17)) != 0;
                features.avx512bw = (registers[1] & (1 << 30)) != 0;
                features.avx512vl = (registers[1] & (1U << 31)) != 0;
            }
#endif
            return features;
        }
    }// namespace

    auto DetectRuntimeFeatures() noexcept -> RuntimeFeatures
    {
        RuntimeFeatures features = DetectX86Features();

#if defined(__aarch64__) || defined(_M_ARM64)
        // Advanced SIMD is mandatory in the AArch64 execution state.
        features.neon = true;
#elif defined(__linux__) && defined(__arm__)
        features.neon = (getauxval(AT_HWCAP) & HWCAP_NEON) != 0;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM)
        features.neon = true;
#endif
        return features;
    }

    auto GetRuntimeFeatures() noexcept -> const RuntimeFeatures&
    {
        static const RuntimeFeatures features = DetectRuntimeFeatures();
        return features;
    }

    auto GetCompiledBackends() noexcept -> CompiledBackends
    {
        CompiledBackends compiled {};
#if defined(NGIN_SIMD_RUNTIME_COMPILED_SSE2)
        compiled.sse2 = true;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX2)
        compiled.avx2 = true;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX512)
        compiled.avx512 = true;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_NEON)
        compiled.neon = true;
#endif
        return compiled;
    }

    auto GetRuntimeBackend() noexcept -> RuntimeBackend
    {
        return SelectRuntimeBackend(GetRuntimeFeatures(), GetCompiledBackends());
    }

}// namespace NGIN::SIMD
