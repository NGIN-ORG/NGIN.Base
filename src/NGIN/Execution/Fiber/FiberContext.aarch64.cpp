/// @file FiberContext.aarch64.cpp
/// @brief AArch64 AAPCS64 context switch implementation (internal).

#include "FiberContext.hpp"

#include <cstddef>

namespace NGIN::Execution::detail
{
#if defined(__aarch64__)
    static_assert(sizeof(FiberContext) == 120);
    static_assert(offsetof(FiberContext, sp) == 0);
    static_assert(offsetof(FiberContext, pc) == 8);
    static_assert(offsetof(FiberContext, x19) == 16);
    static_assert(offsetof(FiberContext, x20) == 24);
    static_assert(offsetof(FiberContext, x21) == 32);
    static_assert(offsetof(FiberContext, x22) == 40);
    static_assert(offsetof(FiberContext, x23) == 48);
    static_assert(offsetof(FiberContext, x24) == 56);
    static_assert(offsetof(FiberContext, x25) == 64);
    static_assert(offsetof(FiberContext, x26) == 72);
    static_assert(offsetof(FiberContext, x27) == 80);
    static_assert(offsetof(FiberContext, x28) == 88);
    static_assert(offsetof(FiberContext, x29) == 96);
    static_assert(offsetof(FiberContext, x30) == 104);
    static_assert(offsetof(FiberContext, fpcr) == 112);
    static_assert(offsetof(FiberContext, fpsr) == 116);
#endif
}// namespace NGIN::Execution::detail

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
asm(R"(
.text

.globl NGIN_FiberContextSwitch
.type NGIN_FiberContextSwitch, %function
NGIN_FiberContextSwitch:
    // Save callee-saved registers (AAPCS64): x19-x28, x29 (fp). Save x30 (lr) as well.
    mov x9, sp
    str x9, [x0, #0]          // sp
    adr x10, 1f
    str x10, [x0, #8]         // pc (return label)

    stp x19, x20, [x0, #16]
    stp x21, x22, [x0, #32]
    stp x23, x24, [x0, #48]
    stp x25, x26, [x0, #64]
    stp x27, x28, [x0, #80]
    stp x29, x30, [x0, #96]

    mrs x11, fpcr
    mrs x12, fpsr
    str w11, [x0, #112]
    str w12, [x0, #116]

    // Restore target context
    ldr x9, [x1, #0]
    mov sp, x9
    ldr w11, [x1, #112]
    ldr w12, [x1, #116]
    msr fpcr, x11
    msr fpsr, x12

    ldp x19, x20, [x1, #16]
    ldp x21, x22, [x1, #32]
    ldp x23, x24, [x1, #48]
    ldp x25, x26, [x1, #64]
    ldp x27, x28, [x1, #80]
    ldp x29, x30, [x1, #96]

    ldr x10, [x1, #8]
    br x10
1:
    ret

.size NGIN_FiberContextSwitch, .-NGIN_FiberContextSwitch
)");
#endif
