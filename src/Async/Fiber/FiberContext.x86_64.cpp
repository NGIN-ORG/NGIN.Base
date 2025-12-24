/// @file FiberContext.x86_64.cpp
/// @brief x86_64 SysV context switch implementation (internal).

#include "FiberContext.hpp"

#include <cstddef>

namespace NGIN::Execution::detail
{
    static_assert(offsetof(FiberContext, rsp) == 0);
    static_assert(offsetof(FiberContext, rip) == 8);
    static_assert(offsetof(FiberContext, rbx) == 16);
    static_assert(offsetof(FiberContext, rbp) == 24);
    static_assert(offsetof(FiberContext, r12) == 32);
    static_assert(offsetof(FiberContext, r13) == 40);
    static_assert(offsetof(FiberContext, r14) == 48);
    static_assert(offsetof(FiberContext, r15) == 56);
    static_assert(offsetof(FiberContext, mxcsr) == 64);
    static_assert(offsetof(FiberContext, fpucw) == 68);
}// namespace NGIN::Execution::detail

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
asm(R"(
.text

.globl NGIN_FiberContextSwitch
.type NGIN_FiberContextSwitch, @function
NGIN_FiberContextSwitch:
    cld
    movq %rbx, 16(%rdi)
    movq %rbp, 24(%rdi)
    movq %r12, 32(%rdi)
    movq %r13, 40(%rdi)
    movq %r14, 48(%rdi)
    movq %r15, 56(%rdi)
    movq %rsp, 0(%rdi)
    leaq 1f(%rip), %rax
    movq %rax, 8(%rdi)
    stmxcsr 64(%rdi)
    fnstcw 68(%rdi)

    movq 0(%rsi), %rsp
    ldmxcsr 64(%rsi)
    fldcw 68(%rsi)
    movq 16(%rsi), %rbx
    movq 24(%rsi), %rbp
    movq 32(%rsi), %r12
    movq 40(%rsi), %r13
    movq 48(%rsi), %r14
    movq 56(%rsi), %r15
    movq 8(%rsi), %rax
    jmp *%rax
1:
    ret

.size NGIN_FiberContextSwitch, .-NGIN_FiberContextSwitch
)");
#endif
