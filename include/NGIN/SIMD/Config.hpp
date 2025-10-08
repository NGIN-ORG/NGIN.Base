#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Common compile-time configuration knobs for the NGIN SIMD façade.

#include <cstdint>

// Backend availability toggles ------------------------------------------------
// Define to 1 to force-disable the corresponding backend regardless of compiler
// feature macros. This is useful for targeted testing or working around known
// toolchain issues.
#ifndef NGIN_SIMD_DISABLE_AVX512
#define NGIN_SIMD_DISABLE_AVX512 0
#endif

#ifndef NGIN_SIMD_DISABLE_AVX2
#define NGIN_SIMD_DISABLE_AVX2 0
#endif

#ifndef NGIN_SIMD_DISABLE_SSE2
#define NGIN_SIMD_DISABLE_SSE2 0
#endif

#ifndef NGIN_SIMD_DISABLE_NEON
#define NGIN_SIMD_DISABLE_NEON 0
#endif

// Math policy configuration ---------------------------------------------------
// Permits users to select between strict and approximation-oriented math
// kernels once they are implemented. The macro is expected to expand to a
// fully-qualified policy type.
#ifndef NGIN_SIMD_MATH_POLICY
#define NGIN_SIMD_MATH_POLICY ::NGIN::SIMD::StrictMathPolicy
#endif

// Backend selection -----------------------------------------------------------
// The default backend macro expands to a fully-qualified tag type. If a user
// supplies their own override, we respect it verbatim.
#ifndef NGIN_SIMD_DEFAULT_BACKEND

#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::ScalarTag

#endif // NGIN_SIMD_DEFAULT_BACKEND
