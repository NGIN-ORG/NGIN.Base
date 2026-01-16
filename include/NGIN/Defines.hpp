#pragma once

#include <cassert>
#include <cstdlib>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__has_include)
#if __has_include(<immintrin.h>)
#include <immintrin.h>
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define NGIN_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define NGIN_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define NGIN_ALWAYS_INLINE inline
#endif

#ifndef NGIN_FORCEINLINE
#define NGIN_FORCEINLINE NGIN_ALWAYS_INLINE
#endif

#ifndef NGIN_LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define NGIN_LIKELY(x) __builtin_expect(!!(x), 1)
#define NGIN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NGIN_LIKELY(x) (x)
#define NGIN_UNLIKELY(x) (x)
#endif
#endif

#ifndef NGIN_BASE_API
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(NGIN_BASE_SHARED_BUILD)
#define NGIN_BASE_API __declspec(dllexport)
#elif defined(NGIN_BASE_SHARED)
#define NGIN_BASE_API __declspec(dllimport)
#else
#define NGIN_BASE_API
#endif
#define NGIN_BASE_LOCAL
#else
#if defined(NGIN_BASE_SHARED_BUILD) || defined(NGIN_BASE_SHARED)
#define NGIN_BASE_API __attribute__((visibility("default")))
#else
#define NGIN_BASE_API
#endif
#define NGIN_BASE_LOCAL __attribute__((visibility("hidden")))
#endif
#endif
#ifndef NGIN_BASE_LOCAL
#define NGIN_BASE_LOCAL
#endif

#ifndef NGIN_CPU_RELAX
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#if defined(_MSC_VER)
#define NGIN_CPU_RELAX() _mm_pause()
#elif defined(__GNUC__) || defined(__clang__)
#define NGIN_CPU_RELAX() __builtin_ia32_pause()
#else
#define NGIN_CPU_RELAX() __asm__ __volatile__("pause")
#endif
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
#if defined(_MSC_VER)
#define NGIN_CPU_RELAX() __yield()
#elif defined(__clang__)
#define NGIN_CPU_RELAX() __builtin_arm_hint(1)
#elif defined(__GNUC__)
#if defined(__aarch64__)
#define NGIN_CPU_RELAX() __builtin_aarch64_yield()
#else
#define NGIN_CPU_RELAX() __asm__ __volatile__("yield")
#endif
#else
#define NGIN_CPU_RELAX() do { } while (false)
#endif
#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
#if defined(__GNUC__) || defined(__clang__)
#define NGIN_CPU_RELAX() __builtin_ppc_yield()
#else
#define NGIN_CPU_RELAX() __asm__ __volatile__("or 27,27,27")
#endif
#else
#define NGIN_CPU_RELAX() do { } while (false)
#endif
#endif

namespace NGIN
{

    namespace detail
    {
        [[noreturn]] inline void Abort([[maybe_unused]] const char* msg) noexcept
        {
            (void)msg;
            std::abort();
        }
    }

// Contract helpers
#ifndef NGIN_ASSERT
#define NGIN_ASSERT(expr) assert(expr)
#endif

#ifndef NGIN_ABORT
#define NGIN_ABORT(msg) ::NGIN::detail::Abort(msg)
#endif

    [[noreturn]] inline void Unreachable()
    {
#if defined(_MSC_VER) && !defined(__clang__)// MSVC
        __assume(false);
#else// GCC, Clang
        __builtin_unreachable();
#endif
    }

#ifndef NGIN_UNREACHABLE
#define NGIN_UNREACHABLE() ::NGIN::Unreachable()
#endif

}// namespace NGIN
