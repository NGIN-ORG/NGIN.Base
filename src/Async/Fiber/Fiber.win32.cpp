#include "FiberPlatform.hpp"

#if defined(_WIN32)

#include <Windows.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace NGIN::Execution::detail
{
    struct FiberState
    {
        LPVOID             handle = nullptr;
        LPVOID             callerFiber = nullptr;
        Fiber::Job         job {};
        UIntSize           stackSize {Fiber::DEFAULT_STACK_SIZE};
        std::exception_ptr exception;
        bool               running = false;
    };

    namespace
    {
        thread_local FiberState* currentFiber = nullptr;
        thread_local LPVOID      mainFiber    = nullptr;

        [[noreturn]] void ThrowLastError(const char* message)
        {
            DWORD       error        = ::GetLastError();
            LPSTR       buffer       = nullptr;
            const DWORD flags        = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            DWORD       charsWritten = ::FormatMessageA(flags, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

            std::string fullMessage = message;
            if (charsWritten != 0 && buffer != nullptr)
            {
                fullMessage.append(": ");
                fullMessage.append(buffer, buffer + charsWritten);
                ::LocalFree(buffer);
            }
            else
            {
                fullMessage.append(" (error code: ").append(std::to_string(error)).append(")");
            }
            throw std::runtime_error(fullMessage);
        }

        void SwitchToMain()
        {
            if (mainFiber && ::GetCurrentFiber() != mainFiber)
            {
                ::SwitchToFiber(mainFiber);
            }
        }

        VOID CALLBACK Trampoline(void* param)
        {
            auto* state  = static_cast<FiberState*>(param);
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
    }// namespace

    FiberState* CreateFiberState(UIntSize stackSize)
    {
        EnsureMainFiber();

        auto* state      = new FiberState {};
        state->stackSize = stackSize == 0 ? Fiber::DEFAULT_STACK_SIZE : stackSize;
        state->handle    = ::CreateFiberEx(state->stackSize, state->stackSize, 0, &Trampoline, state);
        if (!state->handle)
        {
            delete state;
            ThrowLastError("Fiber: CreateFiberEx failed");
        }
        return state;
    }

    void DestroyFiberState(FiberState* state) noexcept
    {
        if (!state)
        {
            return;
        }

        if (state->handle)
        {
            if (::GetCurrentFiber() == state->handle)
            {
                SwitchToMain();
            }
            ::DeleteFiber(state->handle);
            state->handle = nullptr;
        }
        delete state;
    }

    void AssignJob(FiberState* state, Fiber::Job job)
    {
        if (!state)
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
        if (!state || !state->handle)
        {
            std::terminate();
        }
        EnsureMainFiber();
        FiberState* previousFiber = currentFiber;
        currentFiber              = state;
        state->running = true;
        state->callerFiber = ::GetCurrentFiber();
        ::SwitchToFiber(state->handle);
        state->callerFiber = nullptr;
        state->running = false;
        currentFiber   = previousFiber;
    }

    void EnsureMainFiber()
    {
        if (!mainFiber)
        {
            mainFiber = ::ConvertThreadToFiber(nullptr);
            if (!mainFiber)
            {
                ThrowLastError("Fiber: ConvertThreadToFiber failed");
            }
        }
    }

    bool IsMainFiberInitialized() noexcept
    {
        return mainFiber != nullptr;
    }

    bool IsInFiber() noexcept
    {
        if (!mainFiber)
        {
            return false;
        }
        return ::GetCurrentFiber() != mainFiber;
    }

    void YieldFiber()
    {
        if (currentFiber == nullptr)
        {
            std::terminate();
        }

        if (!currentFiber->callerFiber)
        {
            std::terminate();
        }

        ::SwitchToFiber(currentFiber->callerFiber);
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
static_assert(false, "Fiber.win32.cpp included on non-Windows platform");
#endif
