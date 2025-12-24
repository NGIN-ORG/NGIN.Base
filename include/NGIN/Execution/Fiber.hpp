/// @file Fiber.hpp
/// @brief Cross-platform fiber abstraction with platform-specific implementations.
#pragma once

#include <NGIN/Execution/Config.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <exception>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

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

    inline constexpr UIntSize DEFAULT_FIBER_STACK_SIZE = 128uz * 1024uz;

    class FiberAllocatorRef final
    {
    public:
        using AllocateFn   = void* (*)(void*, UIntSize, UIntSize) noexcept;
        using DeallocateFn = void (*)(void*, void*, UIntSize, UIntSize) noexcept;

        constexpr FiberAllocatorRef() noexcept = default;

        constexpr FiberAllocatorRef(void* self, AllocateFn allocate, DeallocateFn deallocate) noexcept
            : m_self(self)
            , m_allocate(allocate)
            , m_deallocate(deallocate)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_allocate != nullptr && m_deallocate != nullptr;
        }

        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) const noexcept
        {
            if (!IsValid())
            {
                return nullptr;
            }
            return m_allocate(m_self, size, alignment);
        }

        void Deallocate(void* ptr, UIntSize size, UIntSize alignment) const noexcept
        {
            if (!IsValid())
            {
                return;
            }
            m_deallocate(m_self, ptr, size, alignment);
        }

        [[nodiscard]] static constexpr FiberAllocatorRef System() noexcept
        {
            return FiberAllocatorRef(
                    nullptr,
                    +[](void*, UIntSize size, UIntSize alignment) noexcept -> void* {
                        if (size == 0)
                        {
                            return nullptr;
                        }
                        const auto aln = alignment == 0 ? alignof(std::max_align_t) : alignment;
                        return ::operator new(size, std::align_val_t(aln), std::nothrow);
                    },
                    +[](void*, void* ptr, UIntSize, UIntSize alignment) noexcept {
                        if (!ptr)
                        {
                            return;
                        }
                        const auto aln = alignment == 0 ? alignof(std::max_align_t) : alignment;
                        ::operator delete(ptr, std::align_val_t(aln), std::nothrow);
                    });
        }

        template<class A>
        static constexpr FiberAllocatorRef From(A& allocator) noexcept
        {
            return FiberAllocatorRef(
                    &allocator,
                    +[](void* self, UIntSize size, UIntSize alignment) noexcept -> void* {
                        auto* a = static_cast<A*>(self);
                        if constexpr (requires(A& x, UIntSize s, UIntSize al) { x.Allocate(s, al); })
                        {
                            return a->Allocate(size, alignment);
                        }
                        else
                        {
                            return nullptr;
                        }
                    },
                    +[](void* self, void* ptr, UIntSize size, UIntSize alignment) noexcept {
                        auto* a = static_cast<A*>(self);
                        if constexpr (requires(A& x, void* p, UIntSize s, UIntSize al) { x.Deallocate(p, s, al); })
                        {
                            a->Deallocate(ptr, size, alignment);
                        }
                    });
        }

    private:
        void*        m_self {nullptr};
        AllocateFn   m_allocate {nullptr};
        DeallocateFn m_deallocate {nullptr};
    };

    struct FiberOptions final
    {
        UIntSize          stackSize {DEFAULT_FIBER_STACK_SIZE};
        bool              guardPages {false}; // best-effort; platform/backend dependent
        UIntSize          guardSize {0};      // best-effort; platform/backend dependent (0 = backend default)
        FiberAllocatorRef allocator {FiberAllocatorRef::System()};
    };

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
        constexpr static UIntSize DEFAULT_STACK_SIZE = DEFAULT_FIBER_STACK_SIZE;

        Fiber();
        explicit Fiber(UIntSize stackSize);
        explicit Fiber(FiberOptions options);
        Fiber(Job job, UIntSize stackSize = DEFAULT_STACK_SIZE);
        Fiber(Job job, FiberOptions options);
        ~Fiber();

        Fiber(const Fiber&)            = delete;
        Fiber& operator=(const Fiber&) = delete;
        Fiber(Fiber&& other) noexcept;
        Fiber& operator=(Fiber&& other) noexcept;

        void Assign(Job job);
        [[nodiscard]] bool TryAssign(Job job) noexcept;
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
