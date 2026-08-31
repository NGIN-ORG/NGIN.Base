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

// Normalized compiler feature macros -----------------------------------------
// Keep compiler-specific predefined macros at this boundary so the public
// headers use the same availability rules on GCC, Clang, and MSVC.
#ifndef NGIN_SIMD_HAS_AVX512
#if !NGIN_SIMD_DISABLE_AVX512 && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
#define NGIN_SIMD_HAS_AVX512 1
#else
#define NGIN_SIMD_HAS_AVX512 0
#endif
#endif

#ifndef NGIN_SIMD_HAS_AVX2
#if !NGIN_SIMD_DISABLE_AVX2 && (defined(__AVX2__) || defined(_M_AVX2))
#define NGIN_SIMD_HAS_AVX2 1
#else
#define NGIN_SIMD_HAS_AVX2 0
#endif
#endif

#ifndef NGIN_SIMD_HAS_SSE2
#if !NGIN_SIMD_DISABLE_SSE2 && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define NGIN_SIMD_HAS_SSE2 1
#else
#define NGIN_SIMD_HAS_SSE2 0
#endif
#endif

#ifndef NGIN_SIMD_HAS_NEON
#if !NGIN_SIMD_DISABLE_NEON && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM))
#define NGIN_SIMD_HAS_NEON 1
#else
#define NGIN_SIMD_HAS_NEON 0
#endif
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

#if NGIN_SIMD_HAS_AVX512
#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::AVX512Tag
#elif NGIN_SIMD_HAS_AVX2
#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::AVX2Tag
#elif NGIN_SIMD_HAS_SSE2
#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::SSE2Tag
#elif NGIN_SIMD_HAS_NEON
#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::NeonTag
#else
#define NGIN_SIMD_DEFAULT_BACKEND ::NGIN::SIMD::ScalarTag
#endif

#endif// NGIN_SIMD_DEFAULT_BACKEND
