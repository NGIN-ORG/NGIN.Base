#pragma once

#include <NGIN/Execution/Fiber.hpp>
#include <NGIN/Defines.hpp>

namespace NGIN::Execution::detail
{
    struct FiberState;

    NGIN_BASE_LOCAL FiberState* CreateFiberState(UIntSize stackSize);
    NGIN_BASE_LOCAL void        DestroyFiberState(FiberState* state) noexcept;
    NGIN_BASE_LOCAL void        AssignJob(FiberState* state, Fiber::Job job);
    NGIN_BASE_LOCAL void        ResumeFiber(FiberState* state);
    NGIN_BASE_LOCAL void        EnsureMainFiber();
    NGIN_BASE_LOCAL bool        IsMainFiberInitialized() noexcept;
    NGIN_BASE_LOCAL bool        IsInFiber() noexcept;
    NGIN_BASE_LOCAL void        YieldFiber();
}// namespace NGIN::Execution::detail
