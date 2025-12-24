#include "FiberPlatform.hpp"

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)

#include <NGIN/Primitives.hpp>
#include <NGIN/Execution/ThisThread.hpp>

#include <cerrno>
#include <exception>
#include <new>
#include <system_error>
#include <utility>
#include <cstdint>
#include <climits>

#include <ucontext.h>
#include <sys/mman.h>
#include <unistd.h>

namespace NGIN::Execution::detail
{
    struct FiberState
    {
        ucontext_t              context {};
        ucontext_t*             callerContext {nullptr};
        NGIN::Execution::ThisThread::ThreadId ownerThreadId {0};
        NGIN::Execution::FiberAllocatorRef allocator {};
        Fiber::Job              job {};
        Byte*                   stackAllocationBase {nullptr};
        UIntSize                stackAllocationSize {0};
        Byte*                   stackBase {nullptr};
        UIntSize                stackSize {Fiber::DEFAULT_STACK_SIZE};
        UIntSize                stackAlignment {16};
        std::exception_ptr      exception;
        bool                    running = false;
        bool                    stackUsesMmap = false;
    };

    namespace
    {
        thread_local FiberState* currentFiber = nullptr;
        thread_local bool        mainContextInitialized = false;

        constexpr UIntSize AlignUp(UIntSize value, UIntSize alignment) noexcept
        {
            if (alignment <= 1)
            {
                return value;
            }
            return (value + (alignment - 1)) & ~(alignment - 1);
        }

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
            const int err = errno;
            throw std::system_error(err, std::generic_category(), message);
        }
    }// namespace

    FiberState* CreateFiberState(FiberOptions options)
    {
        EnsureMainFiber();

        if (!options.allocator.IsValid())
        {
            options.allocator = NGIN::Execution::FiberAllocatorRef::System();
        }

        void* stateMem = options.allocator.Allocate(sizeof(FiberState), alignof(FiberState));
        if (!stateMem)
        {
            throw std::bad_alloc();
        }
        auto* state = ::new (stateMem) FiberState {};
        state->allocator      = options.allocator;
        state->ownerThreadId  = NGIN::Execution::ThisThread::GetId();
        state->stackSize      = options.stackSize == 0 ? Fiber::DEFAULT_STACK_SIZE : options.stackSize;
        state->stackAlignment = 16;

        if (options.guardPages)
        {
            auto pageSize = static_cast<long>(::sysconf(_SC_PAGESIZE));
            UIntSize page = pageSize > 0 ? static_cast<UIntSize>(pageSize) : 4096uz;

            UIntSize guardSize = options.guardSize != 0 ? options.guardSize : page;
            guardSize          = AlignUp(guardSize, page);
            state->stackSize   = AlignUp(state->stackSize, page);

            const UIntSize totalSize = guardSize + state->stackSize;

            int mmapFlags = MAP_PRIVATE;
#if defined(MAP_ANONYMOUS)
            mmapFlags |= MAP_ANONYMOUS;
#elif defined(MAP_ANON)
            mmapFlags |= MAP_ANON;
#endif

            void* region = ::mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, mmapFlags, -1, 0);
            if (region == MAP_FAILED)
            {
                state->~FiberState();
                state->allocator.Deallocate(stateMem, sizeof(FiberState), alignof(FiberState));
                ThrowErrno("Fiber: mmap failed");
            }

            if (::mprotect(region, guardSize, PROT_NONE) != 0)
            {
                ::munmap(region, totalSize);
                state->~FiberState();
                state->allocator.Deallocate(stateMem, sizeof(FiberState), alignof(FiberState));
                ThrowErrno("Fiber: mprotect guard pages failed");
            }

            state->stackAllocationBase = static_cast<Byte*>(region);
            state->stackAllocationSize = totalSize;
            state->stackBase           = state->stackAllocationBase + guardSize;
            state->stackUsesMmap       = true;
        }
        else
        {
            state->stackBase = static_cast<Byte*>(state->allocator.Allocate(state->stackSize, state->stackAlignment));
            if (!state->stackBase)
            {
                state->~FiberState();
                state->allocator.Deallocate(stateMem, sizeof(FiberState), alignof(FiberState));
                throw std::bad_alloc();
            }
            state->stackAllocationBase = state->stackBase;
            state->stackAllocationSize = state->stackSize;
            state->stackUsesMmap       = false;
        }

        if (getcontext(&state->context) == -1)
        {
            if (state->stackBase)
            {
                if (state->stackUsesMmap)
                {
                    ::munmap(state->stackAllocationBase, state->stackAllocationSize);
                }
                else
                {
                    state->allocator.Deallocate(state->stackBase, state->stackSize, state->stackAlignment);
                }
            }
            state->stackAllocationBase = nullptr;
            state->stackAllocationSize = 0;
            state->stackBase           = nullptr;
            state->~FiberState();
            state->allocator.Deallocate(stateMem, sizeof(FiberState), alignof(FiberState));
            ThrowErrno("Fiber: getcontext failed");
        }

        state->context.uc_stack.ss_sp   = state->stackBase;
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
        if (!state)
        {
            return;
        }
        if (state->stackBase)
        {
            if (state->stackUsesMmap)
            {
                ::munmap(state->stackAllocationBase, state->stackAllocationSize);
            }
            else
            {
                state->allocator.Deallocate(state->stackBase, state->stackSize, state->stackAlignment);
            }
            state->stackAllocationBase = nullptr;
            state->stackAllocationSize = 0;
            state->stackUsesMmap       = false;
            state->stackBase = nullptr;
        }
        auto alloc = state->allocator;
        state->~FiberState();
        alloc.Deallocate(state, sizeof(FiberState), alignof(FiberState));
    }

    void AssignJob(FiberState* state, Fiber::Job job)
    {
        if (!state)
        {
            std::terminate();
        }
        if (NGIN::Execution::ThisThread::GetId() != state->ownerThreadId)
        {
            std::terminate();
        }
        if (state->running)
        {
            std::terminate();
        }
        state->job       = std::move(job);
        state->exception = nullptr;
    }

    void ResumeFiber(FiberState* state)
    {
        if (!state)
        {
            std::terminate();
        }
        if (NGIN::Execution::ThisThread::GetId() != state->ownerThreadId)
        {
            std::terminate();
        }
        EnsureMainFiber();
        FiberState* previousFiber = currentFiber;
        currentFiber              = state;
        state->running = true;
        ucontext_t caller {};
        state->callerContext = &caller;
        if (swapcontext(&caller, &state->context) == -1)
        {
            state->callerContext = nullptr;
            currentFiber         = previousFiber;
            std::terminate();
        }
        state->callerContext = nullptr;
        state->running = false;
        currentFiber   = previousFiber;
    }

    void EnsureMainFiber()
    {
        if (!mainContextInitialized)
        {
            mainContextInitialized = true;
        }
    }

    bool IsMainFiberInitialized() noexcept
    {
        return mainContextInitialized;
    }

    bool IsInFiber() noexcept
    {
        return currentFiber != nullptr;
    }

    void YieldFiber()
    {
        if (currentFiber == nullptr)
        {
            std::terminate();
        }

        if (currentFiber->callerContext == nullptr)
        {
            std::terminate();
        }
        if (swapcontext(&currentFiber->context, currentFiber->callerContext) == -1)
        {
            std::terminate();
        }
    }

    bool FiberHasJob(const FiberState* state) noexcept
    {
        return state && static_cast<bool>(state->job);
    }

    bool FiberIsRunning(const FiberState* state) noexcept
    {
        return state && state->running;
    }

    bool FiberHasException(const FiberState* state) noexcept
    {
        return state && static_cast<bool>(state->exception);
    }

    std::exception_ptr FiberTakeException(FiberState* state) noexcept
    {
        if (!state)
        {
            return {};
        }
        auto ex          = state->exception;
        state->exception = nullptr;
        return ex;
    }

}// namespace NGIN::Execution::detail

#else
static_assert(false, "Fiber.posix.cpp included on unsupported platform");
#endif
