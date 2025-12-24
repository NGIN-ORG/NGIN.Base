#include "FiberPlatform.hpp"

#if defined(__linux__) && defined(__x86_64__)

#include "FiberContext.hpp"

#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Primitives.hpp>

#include <cerrno>
#include <exception>
#include <new>
#include <system_error>

#include <sys/mman.h>
#include <unistd.h>

namespace NGIN::Execution::detail
{
    struct FiberState
    {
        FiberContext                      context {};
        FiberContext*                     callerContext {nullptr};
        NGIN::Execution::ThisThread::ThreadId ownerThreadId {0};
        NGIN::Execution::FiberAllocatorRef allocator {};
        Fiber::Job                        job {};
        Byte*                             stackAllocationBase {nullptr};
        UIntSize                          stackAllocationSize {0};
        Byte*                             stackBase {nullptr};
        UIntSize                          stackSize {Fiber::DEFAULT_STACK_SIZE};
        UIntSize                          stackAlignment {16};
        std::exception_ptr                exception;
        bool                              running = false;
        bool                              stackUsesMmap = false;
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

        constexpr std::uintptr_t AlignDown(std::uintptr_t value, std::uintptr_t alignment) noexcept
        {
            if (alignment <= 1)
            {
                return value;
            }
            return value & ~(alignment - 1);
        }

        [[noreturn]] void FiberTrampolineReturn() noexcept
        {
            std::terminate();
        }

        [[noreturn]] void FiberTrampoline()
        {
            for (;;)
            {
                if (currentFiber == nullptr)
                {
                    std::terminate();
                }

                auto* state = currentFiber;

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

        void CaptureFpState(FiberContext& ctx) noexcept
        {
            asm volatile(
                    "stmxcsr %0\n\t"
                    "fnstcw %1\n\t"
                    : "=m"(ctx.mxcsr), "=m"(ctx.fpucw)
                    :
                    : "memory");
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
        state->allocator     = options.allocator;
        state->ownerThreadId = NGIN::Execution::ThisThread::GetId();
        state->stackSize     = options.stackSize == 0 ? Fiber::DEFAULT_STACK_SIZE : options.stackSize;
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

        CaptureFpState(state->context);

        const auto stackTop = reinterpret_cast<std::uintptr_t>(state->stackBase) + static_cast<std::uintptr_t>(state->stackSize);
        auto       rsp      = AlignDown(stackTop, 16);
        rsp -= 8;
        *reinterpret_cast<std::uintptr_t*>(rsp) = reinterpret_cast<std::uintptr_t>(&FiberTrampolineReturn);

        state->context.rsp = static_cast<std::uint64_t>(rsp);
        state->context.rip = reinterpret_cast<std::uint64_t>(&FiberTrampoline);
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
            state->stackBase           = nullptr;
            state->stackUsesMmap       = false;
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
        state->running            = true;
        FiberContext caller {};
        state->callerContext = &caller;
        NGIN_FiberContextSwitch(&caller, &state->context);
        state->callerContext = nullptr;
        state->running       = false;
        currentFiber         = previousFiber;
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

        NGIN_FiberContextSwitch(&currentFiber->context, currentFiber->callerContext);
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
static_assert(false, "Fiber.posix.custom_asm.inc included on unsupported platform/arch");
#endif

