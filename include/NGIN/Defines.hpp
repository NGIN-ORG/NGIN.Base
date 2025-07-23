#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define NGIN_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define NGIN_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define NGIN_ALWAYS_INLINE inline
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