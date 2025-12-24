#include <NGIN/Execution/Config.hpp>

#if (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_UCONTEXT)
#include "Fiber.posix.ucontext.inc"
#elif (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
#include "Fiber.posix.custom_asm.inc"
#else
#error "Unsupported NGIN_EXECUTION_FIBER_BACKEND for POSIX build."
#endif
