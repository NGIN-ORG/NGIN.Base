// Fiber.win32.hpp
#pragma once
#include <Windows.h>
#include <stdexcept>
#include <functional>
#include <string>
#include <future>
#include <atomic>
#include <utility>
#include <cassert>
#include <type_traits>

#include <NGIN/Utilities/Callable.hpp>

#undef Yield

namespace NGIN::Async
{
    class Fiber
    {
    public:
        using Job = std::function<void()>;

        enum class State
        {
            Idle,
            Running,
            Completed,
            Error
        };

        // --- Constructors/Destructor (move only) ---
        explicit Fiber(
                std::string name = {},
                size_t stackSize = DefaultStackSize)
            : m_name(std::move(name)),
              m_stackSize(stackSize)
        {
            EnsureMainFiber();
            m_fiber = ::CreateFiberEx(
                    m_stackSize,
                    m_stackSize,
                    0,
                    &Fiber::Trampoline,
                    this);
            if (!m_fiber)
                ThrowLastError("CreateFiberEx failed");
        }

        // With job
        explicit Fiber(
                Job job,
                std::string name = {},
                size_t stackSize = DefaultStackSize)
            : Fiber(std::move(name), stackSize)
        {
            Assign(std::forward<Job>(job));
        }

        explicit Fiber(
                Job job,
                size_t stackSize = DefaultStackSize)
            : Fiber(std::move(std::string {}), stackSize)
        {
            Assign(std::forward<Job>(job));
        }

        ~Fiber()
        {
            // Never delete the currently running fiber!
            if (GetCurrentFiber() == m_fiber && s_mainFiber)
                SwitchToMainFiber();

            if (m_fiber)
            {
                ::DeleteFiber(m_fiber);
                m_fiber = nullptr;
            }
        }

        // Move-only
        Fiber(Fiber&& other) noexcept
        {
            MoveFrom(std::move(other));
        }
        Fiber& operator=(Fiber&& other) noexcept
        {
            if (this != &other)
            {
                this->~Fiber();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        // Non-copyable
        Fiber(const Fiber&)            = delete;
        Fiber& operator=(const Fiber&) = delete;

        // --- Public API ---

        /// Assign a job (callable) to this fiber (overwrites any previous unfinished job)
        void Assign(Job job)
        {
            if (!job)
                throw std::invalid_argument("Job must be callable");
            if (m_state == State::Running)
                throw std::runtime_error("Cannot assign job to a running fiber");
            m_job          = std::move(job);
            m_state        = State::Idle;
            m_exceptionPtr = nullptr;
        }

        /// Resume this fiber (runs the assigned job, or continues after yield)
        void Resume()
        {
            EnsureMainFiber();

            if (!m_fiber)
                throw std::runtime_error("Invalid fiber");
            if (m_state == State::Completed)
                return;

            State prev = m_state;
            m_state    = State::Running;
            ::SwitchToFiber(m_fiber);

            if (m_exceptionPtr)
                std::rethrow_exception(m_exceptionPtr);

            if (prev == State::Idle && m_state == State::Completed && m_promise)
            {
                m_promise->set_value();
                m_promise.reset();
            }
        }

        /// Cooperative yield back to scheduler (main fiber)
        static void Yield()
        {
            if (s_mainFiber && GetCurrentFiber() != s_mainFiber)
                ::SwitchToFiber(s_mainFiber);
        }

        /// Wait for fiber completion (blocks calling thread!)
        void Wait()
        {
            if (!m_fiber)
                throw std::runtime_error("Invalid fiber");
            if (IsCurrentFiber())
                throw std::runtime_error("Cannot wait on the current fiber");
            if (!m_promise)
                m_promise = std::make_unique<std::promise<void>>();
            std::future<void> fut = m_promise->get_future();
            while (m_state != State::Completed && m_state != State::Error)
                Resume();
            fut.get();
        }

        /// Set or get the fiber's name (for debugging)
        void SetName(std::string name)
        {
            m_name = std::move(name);
        }
        const std::string& Name() const noexcept
        {
            return m_name;
        }

        /// Introspect fiber state
        State GetState() const noexcept
        {
            return m_state;
        }

        /// Is this fiber completed?
        bool IsCompleted() const noexcept
        {
            return m_state == State::Completed;
        }

        /// Is this the currently running fiber?
        bool IsCurrentFiber() const noexcept
        {
            return m_fiber && (GetCurrentFiber() == m_fiber);
        }

        /// Get/set default stack size
        static size_t DefaultStackSizeValue() noexcept
        {
            return DefaultStackSize;
        }
        static void SetDefaultStackSize(size_t size) noexcept
        { /* no-op for now */
        }

        // --- Static: main fiber/scheduler interface ---

        /// Ensure the calling thread has a main fiber
        static void EnsureMainFiber()
        {
            if (!s_mainFiber)
            {
                s_mainFiber = ::ConvertThreadToFiber(nullptr);
                if (!s_mainFiber)
                    ThrowLastError("ConvertThreadToFiber failed");
            }
        }

        /// Switch to main fiber (cooperative yield)
        static void SwitchToMainFiber()
        {
            if (s_mainFiber && GetCurrentFiber() != s_mainFiber)
                ::SwitchToFiber(s_mainFiber);
        }

        /// Is main fiber initialized for this thread?
        static bool IsMainFiberInitialized() noexcept
        {
            return s_mainFiber != nullptr;
        }

    private:
        // --- Fields ---
        LPVOID m_fiber = nullptr;
        std::string m_name;
        size_t m_stackSize = DefaultStackSize;
        Job m_job;
        State m_state = State::Idle;
        std::unique_ptr<std::promise<void>> m_promise;
        std::exception_ptr m_exceptionPtr = nullptr;

        // Per-thread main fiber handle
        inline static thread_local LPVOID s_mainFiber = nullptr;
        static constexpr size_t DefaultStackSize      = 128 * 1024;// 128 KB

        // --- Private methods ---

        void MoveFrom(Fiber&& other) noexcept
        {
            m_fiber        = std::exchange(other.m_fiber, nullptr);
            m_name         = std::move(other.m_name);
            m_stackSize    = other.m_stackSize;
            m_job          = std::move(other.m_job);
            m_state        = other.m_state;
            m_promise      = std::move(other.m_promise);
            m_exceptionPtr = std::exchange(other.m_exceptionPtr, nullptr);
        }

        // Fiber entry point
        static VOID CALLBACK Trampoline(void* param)
        {
            Fiber* self = static_cast<Fiber*>(param);
            try
            {
                while (true)
                {
                    if (!self->m_job)
                    {
                        self->m_state = State::Idle;
                        Yield();
                        continue;
                    }
                    self->m_state = State::Running;
                    self->m_job();
                    self->m_job   = nullptr;
                    self->m_state = State::Completed;
                    Yield();
                    // DO NOT break; -- just continue the loop and wait for next job or deletion
                }
            } catch (...)
            {
                self->m_state        = State::Error;
                self->m_exceptionPtr = std::current_exception();
                Yield();
            }
        }

        static void ThrowLastError(const char* msg)
        {
            DWORD code = ::GetLastError();
            char buf[256];
            ::FormatMessageA(
                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, code, 0, buf, sizeof(buf), nullptr);
            throw std::runtime_error(
                    std::string(msg) + " (Win32 err " + std::to_string(code) + "): " + buf);
        }
    };
}// namespace NGIN::Async
