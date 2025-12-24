#include <NGIN/Execution/Fiber.hpp>

#include "FiberPlatform.hpp"

#include <stdexcept>
#include <utility>

namespace NGIN::Execution
{
    Fiber::Fiber()
        : Fiber(DEFAULT_STACK_SIZE)
    {
    }

    Fiber::Fiber(UIntSize stackSize)
    {
        detail::EnsureMainFiber();
        m_state = detail::CreateFiberState(stackSize == 0 ? DEFAULT_STACK_SIZE : stackSize);
    }

    Fiber::Fiber(Job job, UIntSize stackSize)
        : Fiber(stackSize)
    {
        Assign(std::move(job));
    }

    Fiber::~Fiber()
    {
        if (m_state != nullptr)
        {
            detail::DestroyFiberState(m_state);
            m_state = nullptr;
        }
    }

    Fiber::Fiber(Fiber&& other) noexcept
        : m_state(other.m_state)
    {
        other.m_state = nullptr;
    }

    Fiber& Fiber::operator=(Fiber&& other) noexcept
    {
        if (this != &other)
        {
            if (m_state != nullptr)
            {
                detail::DestroyFiberState(m_state);
            }
            m_state       = other.m_state;
            other.m_state = nullptr;
        }
        return *this;
    }

    void Fiber::Assign(Job job)
    {
        if (!job)
        {
            throw std::invalid_argument("NGIN::Execution::Fiber::Assign requires a callable job");
        }
        if (m_state == nullptr)
        {
            std::terminate();
        }
        detail::AssignJob(m_state, std::move(job));
    }

    FiberResumeResult Fiber::Resume() noexcept
    {
        if (m_state == nullptr)
        {
            std::terminate();
        }

        try
        {
            detail::ResumeFiber(m_state);
        } catch (...)
        {
            std::terminate();
        }

        if (detail::FiberHasException(m_state))
        {
            return FiberResumeResult::Faulted;
        }

        return detail::FiberHasJob(m_state) ? FiberResumeResult::Yielded : FiberResumeResult::Completed;
    }

    std::exception_ptr Fiber::TakeException() noexcept
    {
        if (m_state == nullptr)
        {
            return {};
        }
        return detail::FiberTakeException(m_state);
    }

    bool Fiber::HasJob() const noexcept
    {
        return m_state != nullptr && detail::FiberHasJob(m_state);
    }

    bool Fiber::IsRunning() const noexcept
    {
        return m_state != nullptr && detail::FiberIsRunning(m_state);
    }

    void Fiber::EnsureMainFiber()
    {
        detail::EnsureMainFiber();
    }

    bool Fiber::IsMainFiberInitialized() noexcept
    {
        return detail::IsMainFiberInitialized();
    }

    bool Fiber::IsInFiber() noexcept
    {
        return detail::IsInFiber();
    }

    void Fiber::YieldNow() noexcept
    {
        try
        {
            detail::YieldFiber();
        } catch (...)
        {
            std::terminate();
        }
    }
}// namespace NGIN::Execution
