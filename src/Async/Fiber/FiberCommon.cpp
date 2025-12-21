#include <NGIN/Execution/Fiber.hpp>

#include "FiberPlatform.hpp"

#include <stdexcept>
#include <utility>

namespace NGIN::Execution
{
    struct Fiber::Impl
    {
        detail::FiberState* state = nullptr;

        Impl()                                 = default;
        Impl(const Impl&)                      = delete;
        Impl& operator=(const Impl&)           = delete;
        Impl(Impl&& other) noexcept            = default;
        Impl& operator=(Impl&& other) noexcept = default;
        ~Impl()
        {
            if (state != nullptr)
            {
                detail::DestroyFiberState(state);
                state = nullptr;
            }
        }
    };

    namespace
    {
        [[noreturn]] void ThrowInvalidFiber()
        {
            throw std::runtime_error("NGIN::Execution::Fiber: invalid fiber state");
        }
    }// namespace

    Fiber::Fiber()
        : Fiber(DEFAULT_STACK_SIZE)
    {
    }

    Fiber::Fiber(UIntSize stackSize)
        : m_impl(NGIN::Memory::MakeScoped<Impl>())
    {
        detail::EnsureMainFiber();
        m_impl->state = detail::CreateFiberState(stackSize == 0 ? DEFAULT_STACK_SIZE : stackSize);
    }

    Fiber::Fiber(Job job, UIntSize stackSize)
        : Fiber(stackSize)
    {
        Assign(std::move(job));
    }

    Fiber::~Fiber() = default;

    Fiber::Fiber(Fiber&& other) noexcept = default;

    Fiber& Fiber::operator=(Fiber&& other) noexcept
    {
        if (this != &other)
        {
            m_impl = std::move(other.m_impl);
        }
        return *this;
    }

    void Fiber::Assign(Job job)
    {
        if (!job)
        {
            throw std::invalid_argument("NGIN::Execution::Fiber::Assign requires a callable job");
        }
        if (!m_impl || m_impl->state == nullptr)
        {
            ThrowInvalidFiber();
        }
        detail::AssignJob(m_impl->state, std::move(job));
    }

    void Fiber::Resume()
    {
        if (!m_impl || m_impl->state == nullptr)
        {
            ThrowInvalidFiber();
        }
        detail::ResumeFiber(m_impl->state);
    }

    void Fiber::EnsureMainFiber()
    {
        detail::EnsureMainFiber();
    }

    bool Fiber::IsMainFiberInitialized() noexcept
    {
        return detail::IsMainFiberInitialized();
    }

    void Fiber::Yield()
    {
        detail::YieldFiber();
    }
}// namespace NGIN::Execution
