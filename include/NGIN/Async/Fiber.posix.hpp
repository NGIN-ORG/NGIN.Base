// Platform-specific implementation for POSIX (Linux/Unix)
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Callable.hpp>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <ucontext.h>

namespace NGIN::Async
{
    class Fiber
    {
    public:
        using Job                                    = NGIN::Utilities::Callable<void()>;
        constexpr static UIntSize DEFAULT_STACK_SIZE = 128 * 1024;
        Fiber(const Fiber&)                          = delete;
        Fiber& operator=(const Fiber&)               = delete;
        Fiber(Fiber&&)                               = delete;
        Fiber& operator=(Fiber&&)                    = delete;

        // Ensure the main context is initialized for the calling thread.
        static void EnsureMainFiber()
        {
            if (!s_mainCtxInitialized)
            {
                if (getcontext(&s_mainCtx) == -1)
                    throw std::runtime_error("getcontext failed");
                s_mainCtxInitialized = true;
            }
        }

        static bool IsMainFiberInitialized() noexcept
        {
            return s_mainCtxInitialized;
        }

        Fiber(UIntSize stackSize = DEFAULT_STACK_SIZE)
        {
            EnsureMainFiber();
            m_stack = std::make_unique<Byte[]>(stackSize);
            if (getcontext(&m_ctx) == -1)
                throw std::runtime_error("getcontext failed");
            m_ctx.uc_stack.ss_sp   = m_stack.get();
            m_ctx.uc_stack.ss_size = stackSize;
            m_ctx.uc_link          = nullptr;
            makecontext(&m_ctx, (void (*)()) &Fiber::Trampoline, 1, this);
        }

        Fiber(Job job, UIntSize stackSize = DEFAULT_STACK_SIZE)
        {
            EnsureMainFiber();
            m_stack = std::make_unique<Byte[]>(stackSize);
            if (getcontext(&m_ctx) == -1)
                throw std::runtime_error("getcontext failed");
            m_ctx.uc_stack.ss_sp   = m_stack.get();
            m_ctx.uc_stack.ss_size = stackSize;
            m_ctx.uc_link          = nullptr;
            makecontext(&m_ctx, (void (*)()) &Fiber::Trampoline, 1, this);
            assign(std::move(job));
        }

        ~Fiber()
        {
            // No direct equivalent to SwitchToFiber, but cleanup can be handled here if needed
            // Context and stack memory are managed by unique_ptr
        }

        void assign(Job job) noexcept
        {
            m_job = std::move(job);
        }

        void Resume()
        {
            s_fiber = this;
            swapcontext(&s_mainCtx, &m_ctx);
        }

        static void Yield()
        {
            if (s_fiber)
                swapcontext(&s_fiber->m_ctx, &s_mainCtx);
        }

    private:
        ucontext_t m_ctx;
        Job m_job;
        std::unique_ptr<Byte[]> m_stack;
        static thread_local Fiber* s_fiber;
        static thread_local ucontext_t s_mainCtx;
        static thread_local bool s_mainCtxInitialized;

        static void Trampoline(Fiber* self)
        {
            s_fiber = self;
            while (true)
            {
                if (!self->m_job)
                {
                    Yield();
                    continue;
                }
                self->m_job();
                self->m_job = {};
                Yield();
            }
        }
    };
    thread_local Fiber* Fiber::s_fiber = nullptr;
    thread_local ucontext_t Fiber::s_mainCtx;
    thread_local bool Fiber::s_mainCtxInitialized = false;
}// namespace NGIN::Async