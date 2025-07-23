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
    private:
        constexpr static UIntSize DEFAULT_STACK_SIZE = 128 * 1024; // 128 KB
    public:
        using Entry = NGIN::Utilities::Callable<void()>;

        Fiber(Entry entry, UIntSize stackSize = DEFAULT_STACK_SIZE)
            : m_entry(std::move(entry)), m_active(false), m_stackSize(stackSize)
        {
            m_stack = std::make_unique<Byte[]>(stackSize);
            if (getcontext(&m_ctx) == -1)
                throw std::runtime_error("getcontext failed");
            m_ctx.uc_stack.ss_sp   = m_stack.get();
            m_ctx.uc_stack.ss_size = stackSize;
            m_ctx.uc_link          = nullptr;
            makecontext(&m_ctx, (void (*)()) &Fiber::Trampoline, 1, this);
        }

        ~Fiber() = default;

        void Resume()
        {
            m_active = true;
            swapcontext(&s_mainCtx, &m_ctx);
        }

        static void Yield()
        {
            swapcontext(&s_fiber->m_ctx, &s_mainCtx);
        }

    private:
        ucontext_t m_ctx;
        Entry m_entry;
        bool m_active;
        UIntSize m_stackSize;
        std::unique_ptr<Byte[]> m_stack;
        static thread_local Fiber* s_fiber;
        static thread_local ucontext_t s_mainCtx;

        static void Trampoline(Fiber* self)
        {
            s_fiber = self;
            self->m_entry();
            Fiber::Yield();
        }
    };
    thread_local Fiber* Fiber::s_fiber = nullptr;
    thread_local ucontext_t Fiber::s_mainCtx;
}// namespace NGIN::Async