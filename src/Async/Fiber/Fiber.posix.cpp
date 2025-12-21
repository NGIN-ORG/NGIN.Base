#include "FiberPlatform.hpp"

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)

#include <NGIN/Primitives.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cstdint>
#include <climits>

#include <ucontext.h>

namespace NGIN::Execution::detail
{
    struct FiberState
    {
        ucontext_t              context {};
        Fiber::Job              job {};
        std::unique_ptr<Byte[]> stack {};
        UIntSize                stackSize {Fiber::DEFAULT_STACK_SIZE};
        std::exception_ptr      exception;
        bool                    running = false;
    };

    namespace
    {
        thread_local FiberState* currentFiber = nullptr;
        thread_local ucontext_t  mainContext;
        thread_local bool        mainContextInitialized = false;

        std::uintptr_t CombinePointerParts(int low, int high) noexcept
        {
            const auto lowU  = static_cast<std::uint32_t>(low);
            const auto highU = static_cast<std::uint32_t>(high);
#if INTPTR_MAX > INT32_MAX
            return (static_cast<std::uintptr_t>(highU) << 32) | static_cast<std::uintptr_t>(lowU);
#else
            (void)highU;
            return static_cast<std::uintptr_t>(lowU);
#endif
        }

        void Trampoline(int low, int high)
        {
            auto* state = reinterpret_cast<FiberState*>(CombinePointerParts(low, high));
            currentFiber = state;
            for (;;)
            {
                if (!state->job)
                {
                    YieldFiber();
                    continue;
                }

                try
                {
                    state->job();
                    state->exception = nullptr;
                } catch (...)
                {
                    state->exception = std::current_exception();
                }

                state->job = {};
                YieldFiber();
            }
        }

        [[noreturn]] void ThrowErrno(const char* message)
        {
            throw std::runtime_error(message);
        }
    }// namespace

    FiberState* CreateFiberState(UIntSize stackSize)
    {
        EnsureMainFiber();

        auto* state      = new FiberState {};
        state->stackSize = stackSize == 0 ? Fiber::DEFAULT_STACK_SIZE : stackSize;
        state->stack     = std::make_unique<Byte[]>(state->stackSize);

        if (getcontext(&state->context) == -1)
        {
            delete state;
            ThrowErrno("Fiber: getcontext failed");
        }

        state->context.uc_stack.ss_sp   = state->stack.get();
        state->context.uc_stack.ss_size = state->stackSize;
        state->context.uc_link          = nullptr;

        const auto ptr = reinterpret_cast<std::uintptr_t>(state);
        const auto low = static_cast<int>(static_cast<std::uint32_t>(ptr));
#if INTPTR_MAX > INT32_MAX
        const auto high = static_cast<int>(static_cast<std::uint32_t>(ptr >> 32));
#else
        const auto high = 0;
#endif
        makecontext(&state->context, reinterpret_cast<void (*)()>(&Trampoline), 2, low, high);
        return state;
    }

    void DestroyFiberState(FiberState* state) noexcept
    {
        delete state;
    }

    void AssignJob(FiberState* state, Fiber::Job job)
    {
        if (!state)
        {
            throw std::runtime_error("Fiber: invalid state for AssignJob");
        }
        if (state->running)
        {
            throw std::runtime_error("Fiber: cannot assign job while running");
        }
        state->job       = std::move(job);
        state->exception = nullptr;
    }

    void ResumeFiber(FiberState* state)
    {
        if (!state)
        {
            throw std::runtime_error("Fiber: invalid state for Resume");
        }
        EnsureMainFiber();
        currentFiber   = state;
        state->running = true;
        if (swapcontext(&mainContext, &state->context) == -1)
        {
            throw std::runtime_error("Fiber: swapcontext failed");
        }
        state->running = false;
        currentFiber   = nullptr;

        if (state->exception)
        {
            auto ex          = state->exception;
            state->exception = nullptr;
            std::rethrow_exception(ex);
        }
    }

    void EnsureMainFiber()
    {
        if (!mainContextInitialized)
        {
            if (getcontext(&mainContext) == -1)
            {
                throw std::runtime_error("Fiber: getcontext failed for main context");
            }
            mainContextInitialized = true;
        }
    }

    bool IsMainFiberInitialized() noexcept
    {
        return mainContextInitialized;
    }

    void YieldFiber()
    {
        if (currentFiber == nullptr)
        {
            return;
        }
        if (swapcontext(&currentFiber->context, &mainContext) == -1)
        {
            throw std::runtime_error("Fiber: swapcontext failed during yield");
        }
    }

}// namespace NGIN::Execution::detail

#else
static_assert(false, "Fiber.posix.cpp included on unsupported platform");
#endif
