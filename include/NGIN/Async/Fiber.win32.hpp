// Fiber.hpp
#pragma once
#include <Windows.h>
#include <stdexcept>
#include <coroutine>
#include <NGIN/Utilities/Callable.hpp>

#undef Yield

namespace NGIN::Async
{
    class Fiber
    {
    public:
        using Job                                  = NGIN::Utilities::Callable<void()>;
        constexpr static size_t DEFAULT_STACK_SIZE = 128uz * 1024uz;
        Fiber(const Fiber&)                        = delete;
        Fiber& operator=(const Fiber&)             = delete;
        Fiber(Fiber&&)                             = delete;
        Fiber& operator=(Fiber&&)                  = delete;

        // Ensure the main fiber is initialized for the calling thread.
        static void EnsureMainFiber()
        {
            if (!s_mainFiber)
            {
                s_mainFiber = ConvertThreadToFiber(nullptr);
                if (!s_mainFiber)
                    throw std::runtime_error("ConvertThreadToFiber failed");
            }
        }

        static bool IsMainFiberInitialized() noexcept
        {
            return s_mainFiber != nullptr;
        }

        Fiber(size_t stackSize = DEFAULT_STACK_SIZE)
        {
            if (!s_mainFiber)
            {
                s_mainFiber = ConvertThreadToFiber(nullptr);
                if (!s_mainFiber)
                    throw std::runtime_error("ConvertThreadToFiber failed");
            }
            m_fiber = CreateFiberEx(stackSize, stackSize, 0, &Trampoline, this);
            if (!m_fiber)
                throw std::runtime_error("CreateFiberEx failed");
        }

        Fiber(Job job, size_t stackSize = DEFAULT_STACK_SIZE)
        {
            if (!s_mainFiber)
            {
                s_mainFiber = ConvertThreadToFiber(nullptr);
                if (!s_mainFiber)
                    throw std::runtime_error("ConvertThreadToFiber failed");
            }
            m_fiber = CreateFiberEx(stackSize, stackSize, 0, &Trampoline, this);
            if (!m_fiber)
                throw std::runtime_error("CreateFiberEx failed");

            Assign(std::move(job));
        }

        ~Fiber()
        {
            if (GetCurrentFiber() == m_fiber && s_mainFiber)
                SwitchToFiber(s_mainFiber);
            if (m_fiber)
                DeleteFiber(m_fiber);
        }

        /// Assign *any* job to this fiber
        void Assign(Job job) noexcept
        {
            m_job = std::move(job);
        }

        /// Kick it off (it will run *one* job then yield)
        void Resume()
        {
            if (!m_fiber)
                throw std::runtime_error("Invalid fiber");
            SwitchToFiber(m_fiber);
        }

        /// Cooperative yield back to scheduler
        static void Yield()
        {
            if (s_mainFiber && GetCurrentFiber() != s_mainFiber)
                SwitchToFiber(s_mainFiber);
        }

    private:
        LPVOID m_fiber = nullptr;
        Job m_job;// <-- generic job slot
        inline static thread_local LPVOID s_mainFiber = nullptr;

        static VOID CALLBACK Trampoline(void* param)
        {
            auto* self = static_cast<Fiber*>(param);
            while (true)
            {
                // 1) wait for a job
                if (!self->m_job)
                {
                    Yield();
                    continue;// No job, just yield again
                }
                // 2) run it
                self->m_job();

                // 3) clear slot & go back to scheduler
                self->m_job = {};
                Yield();
            }
        }
    };
}// namespace NGIN::Async
