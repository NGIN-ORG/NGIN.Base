/// @file Fiber.hpp
/// @brief Cross-platform fiber abstraction with platform-specific implementations.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Execution/Config.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <cstddef>
#include <exception>
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

    /// @brief Non-owning type-erased allocator used for fiber stack storage.
    class FiberAllocatorRef final
    {
    public:
        /// @brief Type-erased stack-allocation callback.
        using AllocateFn = void* (*) (void*, UIntSize, UIntSize) noexcept;
        /// @brief Type-erased stack-deallocation callback.
        using DeallocateFn = void (*)(void*, void*, UIntSize, UIntSize) noexcept;

        /// @brief Constructs an invalid allocator reference.
        constexpr FiberAllocatorRef() noexcept = default;

        /// @brief Constructs a reference from borrowed state and allocation callbacks.
        constexpr FiberAllocatorRef(void* self, AllocateFn allocate, DeallocateFn deallocate) noexcept
            : m_self(self), m_allocate(allocate), m_deallocate(deallocate)
        {
        }

        /// @brief Returns whether both allocation callbacks are installed.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_allocate != nullptr && m_deallocate != nullptr;
        }

        /// @brief Allocates a stack byte block, or returns `nullptr` when invalid or exhausted.
        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) const noexcept
        {
            if (!IsValid())
            {
                return nullptr;
            }
            return m_allocate(m_self, size, alignment);
        }

        /// @brief Releases a stack byte block; an invalid reference ignores the request.
        void Deallocate(void* ptr, UIntSize size, UIntSize alignment) const noexcept
        {
            if (!IsValid())
            {
                return;
            }
            m_deallocate(m_self, ptr, size, alignment);
        }

        /// @brief Returns a stateless reference backed by aligned global allocation.
        [[nodiscard]] static constexpr FiberAllocatorRef System() noexcept
        {
            return FiberAllocatorRef(
                    nullptr,
                    +[](void*, UIntSize size, UIntSize alignment) noexcept -> void* {
                        if (size == 0)
                        {
                            return nullptr;
                        }
                        const UIntSize aln = alignment == 0 ? alignof(std::max_align_t) : alignment;
                        return ::operator new(size, std::align_val_t(aln), std::nothrow);
                    },
                    +[](void*, void* ptr, UIntSize, UIntSize alignment) noexcept {
                        if (!ptr)
                        {
                            return;
                        }
                        const UIntSize aln = alignment == 0 ? alignof(std::max_align_t) : alignment;
                        ::operator delete(ptr, std::align_val_t(aln), std::nothrow);
                    });
        }

        /// @brief Creates a non-owning allocator reference from a compatible allocator.
        /// @warning The allocator must outlive this reference and all stacks allocated through it.
        template<class A>
        static constexpr FiberAllocatorRef From(A& allocator) noexcept
        {
            return FiberAllocatorRef(
                    &allocator,
                    +[](void* self, UIntSize size, UIntSize alignment) noexcept -> void* {
                        A* a = static_cast<A*>(self);
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
                        A* a = static_cast<A*>(self);
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

    /// @brief Stack allocation and guard-page options for a fiber.
    struct FiberOptions final
    {
        UIntSize          stackSize {DEFAULT_FIBER_STACK_SIZE};
        bool              guardPages {false};// best-effort; platform/backend dependent
        UIntSize          guardSize {0};     // best-effort; platform/backend dependent (0 = backend default)
        FiberAllocatorRef allocator {FiberAllocatorRef::System()};
    };

    /// @brief State transition observed after resuming a fiber.
    enum class FiberResumeResult : UInt8
    {
        Yielded,
        Completed,
        Faulted,
    };

    /// @brief Move-only reusable stackful execution context.
    class NGIN_EXECUTION_API Fiber
    {
    public:
        /// @brief Owning callable executed by a fiber.
        using Job = NGIN::Utilities::Callable<void()>;
        /// @brief Default stack size in bytes.
        constexpr static UIntSize DEFAULT_STACK_SIZE = DEFAULT_FIBER_STACK_SIZE;

        /// @brief Constructs an idle fiber with default options.
        Fiber();
        /// @brief Constructs an idle fiber with a requested stack size.
        explicit Fiber(UIntSize stackSize);
        /// @brief Constructs an idle fiber with explicit options.
        explicit Fiber(FiberOptions options);
        /// @brief Constructs a fiber with an initial job and stack size.
        Fiber(Job job, UIntSize stackSize = DEFAULT_STACK_SIZE);
        /// @brief Constructs a fiber with an initial job and explicit options.
        Fiber(Job job, FiberOptions options);
        /// @brief Releases the fiber context and its stack.
        ~Fiber();

        /// @brief Fibers are non-copyable because they own an execution stack.
        Fiber(const Fiber&) = delete;
        /// @brief Fibers are non-copy-assignable because they own an execution stack.
        Fiber& operator=(const Fiber&) = delete;
        /// @brief Transfers ownership of a fiber context.
        Fiber(Fiber&& other) noexcept;
        /// @brief Releases this context and transfers ownership from another fiber.
        Fiber& operator=(Fiber&& other) noexcept;

        /// @brief Assigns or replaces a job on a non-running fiber owned by the calling thread.
        /// @warning Terminates for an empty job, invalid fiber, wrong owner thread, or running fiber.
        void Assign(Job job);
        /// @brief Attempts to assign a job when the fiber is idle and has no pending job.
        /// @return `false` when the fiber is running or already has a job.
        /// @warning Terminates for an empty job, invalid fiber, or wrong owner thread.
        [[nodiscard]] bool TryAssign(Job job) noexcept;
        /// @brief Resumes the assigned job until it yields, completes, or faults.
        [[nodiscard]] FiberResumeResult Resume() noexcept;
        /// @brief Removes and returns the exception captured from a faulted job.
        [[nodiscard]] std::exception_ptr TakeException() noexcept;
        /// @brief Returns whether a job is assigned.
        [[nodiscard]] bool HasJob() const noexcept;
        /// @brief Returns whether this fiber is currently executing.
        [[nodiscard]] bool IsRunning() const noexcept;

        /// @brief Initializes fiber support for the calling thread when needed.
        static void EnsureMainFiber();
        /// @brief Returns whether fiber support is initialized for the calling thread.
        static bool IsMainFiberInitialized() noexcept;
        /// @brief Returns whether the calling thread is currently executing inside a fiber.
        static bool IsInFiber() noexcept;
        /// @brief Suspends the current fiber and returns control to its resumer.
        static void YieldNow() noexcept;

    private:
        detail::FiberState* m_state {nullptr};
    };
#else
    class NGIN_EXECUTION_API Fiber
    {
    public:
        using Job                                    = NGIN::Utilities::Callable<void()>;
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

        void        Assign(Job) { Require(); }
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
