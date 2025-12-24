/// @file Config.hpp
/// @brief Compile-time configuration and capability macros for NGIN::Execution.
#pragma once

// Stackful fibers are supported on Windows (OS fibers) and on POSIX builds where ucontext is available in this repo.
#if defined(_WIN32) || defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#ifndef NGIN_EXECUTION_HAS_STACKFUL_FIBERS
#define NGIN_EXECUTION_HAS_STACKFUL_FIBERS 1
#endif
#else
#ifndef NGIN_EXECUTION_HAS_STACKFUL_FIBERS
#define NGIN_EXECUTION_HAS_STACKFUL_FIBERS 0
#endif
#endif

// Fiber backend identifiers (compile-time selection).
#define NGIN_EXECUTION_FIBER_BACKEND_NONE 0
#define NGIN_EXECUTION_FIBER_BACKEND_WIN_FIBER 1
#define NGIN_EXECUTION_FIBER_BACKEND_UCONTEXT 2
#define NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM 3

#ifndef NGIN_EXECUTION_FIBER_BACKEND
#if NGIN_EXECUTION_HAS_STACKFUL_FIBERS
#if defined(_WIN32)
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_WIN_FIBER
#elif defined(__linux__) && defined(__x86_64__)
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM
#elif defined(__linux__) && defined(__aarch64__)
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_UCONTEXT
#else
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_NONE
#endif
#else
#define NGIN_EXECUTION_FIBER_BACKEND NGIN_EXECUTION_FIBER_BACKEND_NONE
#endif
#endif

// Thread backend policy: OS threads are the default target backend. A std::thread fallback is possible but not enabled here.
#ifndef NGIN_EXECUTION_THREAD_BACKEND_OS
#define NGIN_EXECUTION_THREAD_BACKEND_OS 1
#endif

// If enabled and stackful fibers are unsupported, including Fiber headers becomes a hard error.
#ifndef NGIN_EXECUTION_FIBER_HARD_DISABLE
#define NGIN_EXECUTION_FIBER_HARD_DISABLE 0
#endif
