/// @file Fiber.hpp
/// @brief Cross-platform fiber abstraction with platform-specific implementations.
#pragma once

#include <NGIN/Execution/Config.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <exception>

namespace NGIN::Execution
{
#if (!NGIN_EXECUTION_HAS_STACKFUL_FIBERS) && NGIN_EXECUTION_FIBER_HARD_DISABLE
#error "NGIN::Execution::Fiber is disabled (NGIN_EXECUTION_HAS_STACKFUL_FIBERS == 0)."
#endif

#if NGIN_EXECUTION_HAS_STACKFUL_FIBERS
    namespace detail
    {
        struct FiberState;
    }

    enum class FiberResumeResult : UInt8
    {
        Yielded,
        Completed,
        Faulted,
    };

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
        [[nodiscard]] FiberResumeResult Resume() noexcept;
        [[nodiscard]] std::exception_ptr TakeException() noexcept;
        [[nodiscard]] bool HasJob() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

        static void EnsureMainFiber();
        static bool IsMainFiberInitialized() noexcept;
        static bool IsInFiber() noexcept;
        static void YieldNow() noexcept;

    private:
        detail::FiberState* m_state {nullptr};
    };
#else
    class NGIN_BASE_API Fiber
    {
    public:
        using Job = NGIN::Utilities::Callable<void()>;
        constexpr static UIntSize DEFAULT_STACK_SIZE = 0;

        Fiber() noexcept { Require(); }
        explicit Fiber(UIntSize) noexcept { Require(); }
        Fiber(Job, UIntSize = 0) noexcept { Require(); }

        Fiber(const Fiber&)            = delete;
        Fiber& operator=(const Fiber&) = delete;
        Fiber(Fiber&&) noexcept { Require(); }
        Fiber& operator=(Fiber&&) noexcept
        {
            Require();
            return *this;
        }

        void Assign(Job) { Require(); }
        static void EnsureMainFiber() { Require(); }
        static bool IsMainFiberInitialized() noexcept { return false; }
        static bool IsInFiber() noexcept { return false; }
        static void YieldNow() noexcept { Require(); }

    private:
        template<bool Enabled = (NGIN_EXECUTION_HAS_STACKFUL_FIBERS != 0)>
        static constexpr void Require() noexcept
        {
            static_assert(Enabled, "NGIN::Execution::Fiber is disabled (NGIN_EXECUTION_HAS_STACKFUL_FIBERS == 0).");
        }
    };
#endif
}// namespace NGIN::Execution
