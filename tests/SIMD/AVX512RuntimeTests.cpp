// SPDX-License-Identifier: Apache-2.0

#include "catch2/catch_test_macros.hpp"

#include "NGIN/SIMD.hpp"

#if defined(NGIN_SIMD_TEST_HAS_AVX512_KERNEL)
auto RunAVX512KernelTests() noexcept -> bool;
#endif

TEST_CASE("AVX-512 compiled kernel executes when supported")
{
#if defined(NGIN_SIMD_TEST_HAS_AVX512_KERNEL)
    if (!NGIN::SIMD::GetRuntimeFeatures().Supports(NGIN::SIMD::RuntimeBackend::AVX512))
    {
        SKIP("AVX-512F/BW/DQ/VL CPU and OS state is unavailable");
    }
    CHECK(RunAVX512KernelTests());
#else
    SKIP("the compiler does not support the NGIN AVX-512 feature baseline");
#endif
}
