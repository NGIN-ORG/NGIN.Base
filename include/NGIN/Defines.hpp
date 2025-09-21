#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define NGIN_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define NGIN_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define NGIN_ALWAYS_INLINE inline
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

namespace NGIN
{

    [[noreturn]] inline void Unreachable()
    {
#if defined(_MSC_VER) && !defined(__clang__)// MSVC
        __assume(false);
#else// GCC, Clang
        __builtin_unreachable();
#endif
    }

}// namespace NGIN
