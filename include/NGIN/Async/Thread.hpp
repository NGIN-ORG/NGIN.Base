/// @file Fiber.hpp
/// @brief Stack, Thread, and Fiber abstractions for NGIN (Windows-only).
#pragma once

// Only include the platform-specific thread implementation
#if defined(_WIN32)
#include "Thread.win32.hpp"
#elif defined(__unix__) || defined(__linux__)
#include "Thread.posix.hpp"
#else
#error "Platform not supported for Thread"
#endif
