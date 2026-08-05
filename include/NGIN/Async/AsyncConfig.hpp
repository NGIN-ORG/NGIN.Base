/// @file AsyncConfig.hpp
/// @brief Compile-time exception configuration shared by asynchronous primitives.
#pragma once

#ifndef NGIN_ASYNC_CAPTURE_EXCEPTIONS
#define NGIN_ASYNC_CAPTURE_EXCEPTIONS 1
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define NGIN_ASYNC_HAS_EXCEPTIONS 1
#else
#define NGIN_ASYNC_HAS_EXCEPTIONS 0
#endif

#if !NGIN_ASYNC_HAS_EXCEPTIONS
#undef NGIN_ASYNC_CAPTURE_EXCEPTIONS
#define NGIN_ASYNC_CAPTURE_EXCEPTIONS 0
#endif
