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
        using Job = NGIN::Utilities::Callable<void()>;
        constexpr static UIntSize DEFAULT_STACK_SIZE = 128uz * 1024uz;
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
            Assign(std::move(job));
        }

        ~Fiber()
        {
            // No direct equivalent to SwitchToFiber or DeleteFiber for POSIX
            // Context and stack memory are managed by unique_ptr
            // If currently running, yield back to main context
            // (Not strictly necessary, but for API symmetry)
        }

        void Assign(Job job) noexcept
        {
            m_job = std::move(job);
        }

        void Resume()
        {
            if (!m_stack || !m_ctx.uc_stack.ss_sp)
                throw std::runtime_error("Invalid fiber context");
            s_fiber = this;
            swapcontext(&s_mainCtx, &m_ctx);
        }

        static void Yield()
        {
            // Only yield if not already in main context
            if (s_fiber && &s_mainCtx != &s_fiber->m_ctx)
                swapcontext(&s_fiber->m_ctx, &s_mainCtx);
        }

    private:
        ucontext_t m_ctx;
        Job m_job;
        std::unique_ptr<Byte[]> m_stack;
        inline static thread_local Fiber* s_fiber = nullptr;
        inline static thread_local ucontext_t s_mainCtx;
        inline static thread_local bool s_mainCtxInitialized = false;

        static void Trampoline(Fiber* self)
        {
            s_fiber = self;
            while (true)
            {
                // Wait for a job
                if (!self->m_job)
                {
                    Yield();
                    continue;
                }
                // Run the job
                self->m_job();
                // Clear slot & go back to scheduler
                self->m_job = {};
                Yield();
            }
        }
    };
}// namespace NGIN::Async