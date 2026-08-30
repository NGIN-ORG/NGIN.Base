/// @file FiberContext.hpp
/// @brief Internal context switching primitives for stackful fibers.
#pragma once

#include <cstdint>

namespace NGIN::Execution::detail
{
    struct FiberContext final
    {
#if defined(__x86_64__)
        std::uint64_t rsp {0};
        std::uint64_t rip {0};
        std::uint64_t rbx {0};
        std::uint64_t rbp {0};
        std::uint64_t r12 {0};
        std::uint64_t r13 {0};
        std::uint64_t r14 {0};
        std::uint64_t r15 {0};
        std::uint32_t mxcsr {0};
        std::uint32_t fpucw {0};
#elif defined(__aarch64__)
        std::uint64_t sp {0};
        std::uint64_t pc {0};
        std::uint64_t x19 {0};
        std::uint64_t x20 {0};
        std::uint64_t x21 {0};
        std::uint64_t x22 {0};
        std::uint64_t x23 {0};
        std::uint64_t x24 {0};
        std::uint64_t x25 {0};
        std::uint64_t x26 {0};
        std::uint64_t x27 {0};
        std::uint64_t x28 {0};
        std::uint64_t x29 {0};
        std::uint64_t x30 {0};
        std::uint32_t fpcr {0};
        std::uint32_t fpsr {0};
#else
#error "FiberContext is not implemented for this architecture."
#endif
    };

    extern "C" void NGIN_FiberContextSwitch(FiberContext* from, const FiberContext* to) noexcept;
}// namespace NGIN::Execution::detail
