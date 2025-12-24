#include <NGIN/Execution/Config.hpp>

#if (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_WIN_FIBER)
#include "Fiber.win32.winfiber.inc"
#elif (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
#error "NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM is not implemented yet for Windows."
#else
#error "Unsupported NGIN_EXECUTION_FIBER_BACKEND for Windows build."
#endif
