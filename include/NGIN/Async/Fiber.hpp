/// @file Fiber.hpp
/// @brief Cross-platform fiber abstraction with platform-specific implementations.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Primitives.hpp>
#include <print>
#include <NGIN/Utilities/Callable.hpp>

namespace NGIN::Async
{
    class NGIN_BASE_API Fiber
    {
    public:
        using Job                                    = NGIN::Utilities::Callable<void()>;
        constexpr static UIntSize DEFAULT_STACK_SIZE = 128uz * 1024uz;

        Fiber();
        explicit Fiber(UIntSize stackSize);
        Fiber(Job job, UIntSize stackSize = DEFAULT_STACK_SIZE);
        ~Fiber();

        Fiber(const Fiber&)            = delete;
        Fiber& operator=(const Fiber&) = delete;
        Fiber(Fiber&& other) noexcept;
        Fiber& operator=(Fiber&& other) noexcept;

        void Assign(Job job);
        void Resume();

        static void EnsureMainFiber();
        static bool IsMainFiberInitialized() noexcept;
        static void Yield();

    private:
        struct Impl;
        NGIN::Memory::Scoped<Impl> m_impl;
    };
}// namespace NGIN::Async

#undef Yield
