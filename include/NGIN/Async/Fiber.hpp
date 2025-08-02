/// @file Fiber.hpp
/// @brief Stack, Thread, and Fiber abstractions for NGIN (Windows-only).
#pragma once

// Only include the platform-specific fiber implementation
#if defined(_WIN32)
#include "Fiber.new.hpp"
#elif defined(__unix__) || defined(__linux__)
#include "Fiber.posix.hpp"
#else
#error "Platform not supported for Fiber"
#endif

#undef Yield
