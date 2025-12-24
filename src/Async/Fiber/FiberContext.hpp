/// @file FiberContext.hpp
/// @brief Internal context switching primitives for stackful fibers.
#pragma once

#include <cstdint>

namespace NGIN::Execution::detail
{
    struct FiberContext final
    {
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
    };
    static_assert(sizeof(FiberContext) == 72);

    extern "C" void NGIN_FiberContextSwitch(FiberContext* from, const FiberContext* to) noexcept;
}// namespace NGIN::Execution::detail

