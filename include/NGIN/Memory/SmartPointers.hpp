/// @file SmartPointers.hpp
/// @brief Header-only smart pointers (`Scoped`, `Shared`, `Ticket`) with allocator support.
///
/// Design goals
/// - Header-only and modern (C++23).
/// - Works with any allocator satisfying `NGIN::Memory::AllocatorConcept`.
/// - `Scoped<T, A>`: unique-ownership, minimal overhead.
/// - `Shared<T, A>` / `Ticket<T, A>`: reference-counted with weak references.
/// - Deterministic deallocation through the provided allocator.
#pragma once

#include <cstddef>
#include <atomic>
#include <utility>
#include <type_traits>
#include <new>

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

namespace NGIN::Memory
{
    namespace detail
    {
        template<class T, class Alloc>
        struct SharedControl final
        {
            std::atomic<std::size_t> strong {1};// number of Shared owners
            std::atomic<std::size_t> weak {1};  // number of Ticket owners + control's self-weak

            [[no_unique_address]] Alloc alloc {};
            void*                       base {nullptr};
            std::size_t                 totalBytes {0};
            std::size_t                 allocAlignment {alignof(std::max_align_t)};
            T*                          objectPtr {nullptr};

            SharedControl() = default;
            SharedControl(Alloc a, void* b, std::size_t bytes, std::size_t aln, T* obj) noexcept
                : alloc(std::move(a)), base(b), totalBytes(bytes), allocAlignment(aln), objectPtr(obj) {}

            void DestroyObject() noexcept(std::is_nothrow_destructible_v<T>)
            {
                if (objectPtr)
                {
                    objectPtr->~T();
                    objectPtr = nullptr;
                }
            }

            void DeallocateSelf() noexcept
            {
                if (base)
                {
                    alloc.Deallocate(base, totalBytes, allocAlignment);
                    base = nullptr;
                }
            }
        };

        template<class T>
        [[nodiscard]] constexpr bool IsNoexceptMove() noexcept
        {
            return std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>;
        }
    }// namespace detail

    ////////////////////////////////////////////////////////////////////////////////
    // Scoped<T, Alloc>: unique-ownership (like unique_ptr) with custom allocator
    ////////////////////////////////////////////////////////////////////////////////

    /// \brief Unique-ownership smart pointer using an allocator.
    ///
    /// - Manages objects allocated via `AllocateObject(alloc, ...)`.
    /// - Deallocates with the same allocator instance.
    /// - Move-only. Null-safe operations.
    template<class T, AllocatorConcept Alloc = SystemAllocator>
    class Scoped
    {
    public:
        using Element   = T;
        using AllocType = Alloc;

        static_assert(!std::is_array_v<T>, "Scoped does not manage arrays; use AllocateArray helpers.");

        constexpr Scoped() noexcept = default;
        constexpr Scoped(std::nullptr_t) noexcept {}

        explicit Scoped(T* ptr, Alloc alloc = Alloc {}) noexcept : m_ptr(ptr), m_alloc(std::move(alloc)) {}

        Scoped(const Scoped&)            = delete;
        Scoped& operator=(const Scoped&) = delete;

        Scoped(Scoped&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc> && std::is_nothrow_move_assignable_v<Alloc>)
            : m_ptr(other.m_ptr), m_alloc(std::move(other.m_alloc))
        {
            other.m_ptr = nullptr;
        }
        Scoped& operator=(Scoped&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc> && std::is_nothrow_move_assignable_v<Alloc>)
        {
            if (this != &other)
            {
                Reset();
                m_ptr       = other.m_ptr;
                m_alloc     = std::move(other.m_alloc);
                other.m_ptr = nullptr;
            }
            return *this;
        }

        ~Scoped() noexcept { Reset(); }

        /// \brief Raw pointer accessors.
        [[nodiscard]] T*   Get() const noexcept { return m_ptr; }
        [[nodiscard]] T&   operator*() const { return *m_ptr; }
        [[nodiscard]] T*   operator->() const noexcept { return m_ptr; }
        explicit           operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return m_ptr == nullptr; }
        [[nodiscard]] bool operator!=(std::nullptr_t) const noexcept { return m_ptr != nullptr; }

        void Reset(T* newPtr = nullptr) noexcept(std::is_nothrow_destructible_v<T>)
        {
            if (m_ptr)
            {
                DeallocateObject<Alloc, T>(m_alloc, m_ptr);
            }
            m_ptr = newPtr;
        }

        [[nodiscard]] T* Release() noexcept
        {
            T* p  = m_ptr;
            m_ptr = nullptr;
            return p;
        }

        void Swap(Scoped& other) noexcept
        {
            using std::swap;
            swap(m_ptr, other.m_ptr);
            swap(m_alloc, other.m_alloc);
        }

        [[nodiscard]] Alloc&       Allocator() noexcept { return m_alloc; }
        [[nodiscard]] const Alloc& Allocator() const noexcept { return m_alloc; }

    private:
        T*    m_ptr {nullptr};
        Alloc m_alloc {};
    };

    /// \brief Factory: allocate and construct T with a specific allocator.
    template<class T, AllocatorConcept Alloc = SystemAllocator, class... Args>
    [[nodiscard]] Scoped<T, Alloc> MakeScoped(Alloc alloc, Args&&... args)
    {
        T* obj = AllocateObject<Alloc, T>(alloc, std::forward<Args>(args)...);
        return Scoped<T, Alloc>(obj, std::move(alloc));
    }

    /// \brief Factory: allocate and construct T using `SystemAllocator`.
    template<class T, class... Args>
    [[nodiscard]] Scoped<T, SystemAllocator> MakeScoped(Args&&... args)
    {
        SystemAllocator alloc {};
        return MakeScoped<T, SystemAllocator>(alloc, std::forward<Args>(args)...);
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Shared<T, Alloc> and Ticket<T, Alloc>
    ////////////////////////////////////////////////////////////////////////////////

    template<class T, AllocatorConcept Alloc = SystemAllocator>
    class Ticket;// fwd

    /// \brief Reference-counted shared pointer with weak references.
    ///
    /// Self-weak strategy: the control block holds one implicit weak count to prevent premature
    /// deallocation after the last strong owner releases but while weak owners remain.
    template<class T, AllocatorConcept Alloc = SystemAllocator>
    class Shared
    {
    public:
        using Element   = T;
        using AllocType = Alloc;

        static_assert(!std::is_array_v<T>, "Shared does not manage arrays; use AllocateArray helpers.");

        constexpr Shared() noexcept = default;
        constexpr Shared(std::nullptr_t) noexcept {}

        // Copy: bump strong
        /// \brief Copy bumps strong count (relaxed since it's diagnostic-only).
        Shared(const Shared& other) noexcept : m_ctrl(other.m_ctrl)
        {
            if (m_ctrl)
                m_ctrl->strong.fetch_add(1, std::memory_order_relaxed);
        }
        Shared& operator=(const Shared& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_ctrl = other.m_ctrl;
                if (m_ctrl)
                    m_ctrl->strong.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Move: transfer
        Shared(Shared&& other) noexcept : m_ctrl(other.m_ctrl) { other.m_ctrl = nullptr; }
        Shared& operator=(Shared&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_ctrl       = other.m_ctrl;
                other.m_ctrl = nullptr;
            }
            return *this;
        }

        ~Shared() noexcept { Release(); }

        [[nodiscard]] T* Get() const noexcept { return m_ctrl ? m_ctrl->objectPtr : nullptr; }
        [[nodiscard]] T& operator*() const { return *Get(); }
        [[nodiscard]] T* operator->() const noexcept { return Get(); }
        explicit         operator bool() const noexcept { return Get() != nullptr; }

        /// \brief Current strong owners (best-effort; relaxed for low overhead).
        [[nodiscard]] std::size_t UseCount() const noexcept
        {
            return m_ctrl ? m_ctrl->strong.load(std::memory_order_relaxed) : 0;
        }

        void Reset() noexcept { *this = Shared {}; }
        void Reset(std::nullptr_t) noexcept { *this = Shared {}; }
        void Swap(Shared& other) noexcept { std::swap(m_ctrl, other.m_ctrl); }

        /// \brief True if no strong owners.
        [[nodiscard]] bool Expired() const noexcept { return UseCount() == 0; }

        // Grant Ticket access to private constructor for Lock()
        friend class Ticket<T, Alloc>;

        template<class U, AllocatorConcept A, class... Args>
        friend Shared<U, A> MakeShared(A alloc, Args&&... args);
        template<class U, AllocatorConcept A>
        friend Ticket<U, A> MakeTicket(const Shared<U, A>&) noexcept;

    private:
        using Control = detail::SharedControl<T, Alloc>;

        explicit Shared(Control* ctrl) noexcept : m_ctrl(ctrl) {}

        /// \brief Release one strong reference; destroy object on last strong, free on last weak.
        void Release() noexcept
        {
            if (!m_ctrl)
                return;
            if (m_ctrl->strong.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // We are the last strong owner: destroy the object, then drop the control's self-weak.
                m_ctrl->DestroyObject();
                if (m_ctrl->weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    m_ctrl->DeallocateSelf();
                }
            }
            m_ctrl = nullptr;
        }

        Control* m_ctrl {nullptr};
    };

    /// \brief Weak non-owning handle that can lock to a `Shared` if object still alive.
    template<class T, AllocatorConcept Alloc>
    class Ticket
    {
    public:
        constexpr Ticket() noexcept = default;
        constexpr Ticket(std::nullptr_t) noexcept {}

        // Copy: bump weak
        /// \brief Copy bumps weak count (relaxed).
        Ticket(const Ticket& other) noexcept : m_ctrl(other.m_ctrl)
        {
            if (m_ctrl)
                m_ctrl->weak.fetch_add(1, std::memory_order_relaxed);
        }
        Ticket& operator=(const Ticket& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_ctrl = other.m_ctrl;
                if (m_ctrl)
                    m_ctrl->weak.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Move
        Ticket(Ticket&& other) noexcept : m_ctrl(other.m_ctrl) { other.m_ctrl = nullptr; }
        Ticket& operator=(Ticket&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_ctrl       = other.m_ctrl;
                other.m_ctrl = nullptr;
            }
            return *this;
        }

        ~Ticket() noexcept { Release(); }

        void Reset() noexcept { *this = Ticket {}; }
        void Reset(std::nullptr_t) noexcept { *this = Ticket {}; }
        void Swap(Ticket& other) noexcept { std::swap(m_ctrl, other.m_ctrl); }

        [[nodiscard]] bool Expired() const noexcept
        {
            return !m_ctrl || m_ctrl->strong.load(std::memory_order_relaxed) == 0;
        }

        /// \brief Attempt to acquire a strong owner; returns empty on race/lifetime end.
        [[nodiscard]] Shared<T, Alloc> Lock() const noexcept
        {
            if (!m_ctrl)
                return {};

            // Try to increment strong if > 0
            std::size_t s = m_ctrl->strong.load(std::memory_order_relaxed);
            while (s != 0)
            {
                if (m_ctrl->strong.compare_exchange_weak(s, s + 1, std::memory_order_acquire, std::memory_order_relaxed))
                    return Shared<T, Alloc>(m_ctrl);
                // s is updated with the observed value; loop
            }
            return {};
        }

    private:
        using Control = detail::SharedControl<T, Alloc>;

        explicit Ticket(Control* ctrl) noexcept : m_ctrl(ctrl) {}

        /// \brief Drop one weak reference; free control if this was last weak and no strong remain.
        void Release() noexcept
        {
            if (!m_ctrl)
                return;
            if (m_ctrl->weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // Last weak holder: if no strong owners, free memory
                if (m_ctrl->strong.load(std::memory_order_relaxed) == 0)
                {
                    m_ctrl->DeallocateSelf();
                }
            }
            m_ctrl = nullptr;
        }

        Control* m_ctrl {nullptr};

        template<class U, AllocatorConcept A, class... Args>
        friend Shared<U, A> MakeShared(A alloc, Args&&... args);
        template<class U, AllocatorConcept A>
        friend Ticket<U, A> MakeTicket(const Shared<U, A>&) noexcept;
    };

    // Create a control block and T in one allocation
    /// \brief Create a control block and T in one allocation with a specific allocator.
    template<class T, AllocatorConcept Alloc = SystemAllocator, class... Args>
    [[nodiscard]] Shared<T, Alloc> MakeShared(Alloc alloc, Args&&... args)
    {
        using Control = detail::SharedControl<T, Alloc>;

        constexpr std::size_t tAlign    = alignof(T);
        constexpr std::size_t ctrlAlign = alignof(Control);
        const std::size_t     alignment = ctrlAlign > tAlign ? ctrlAlign : tAlign;

        // conservative size: control + possible padding + T
        const std::size_t total = sizeof(Control) + (tAlign - 1) + sizeof(T);

        void* base = alloc.Allocate(total, alignment);
        if (!base)
            throw std::bad_alloc {};

        // place the control block at base
        auto* ctrl = ::new (base) Control(std::move(alloc), base, total, alignment, nullptr);

        // carve out space for T after the control block
        auto*       raw   = static_cast<std::byte*>(base) + sizeof(Control);
        std::size_t space = total - sizeof(Control);

        // use std::align to find a properly aligned spot for T within the remaining space
        void* objVoid = static_cast<void*>(raw);
        if (std::align(alignof(T), sizeof(T), objVoid, space) == nullptr)
        {
            // Should not happen with the above sizing; fail safe:
            ctrl->DeallocateSelf();
            throw std::bad_alloc {};
        }

        // construct T in-place
        T* objPtr = std::construct_at(static_cast<T*>(objVoid), std::forward<Args>(args)...);

        ctrl->objectPtr = objPtr;
        ctrl->strong.store(1, std::memory_order_relaxed);
        ctrl->weak.store(1, std::memory_order_relaxed);// control’s self-weak

        return Shared<T, Alloc>(ctrl);
    }

    /// \brief Create a weak Ticket from a Shared, bumping weak count.
    template<class T, AllocatorConcept Alloc>
    [[nodiscard]] Ticket<T, Alloc> MakeTicket(const Shared<T, Alloc>& shared) noexcept
    {
        using Control = detail::SharedControl<T, Alloc>;
        Control* c    = shared.m_ctrl;
        if (c)
        {
            c->weak.fetch_add(1, std::memory_order_relaxed);
            return Ticket<T, Alloc>(c);
        }
        return Ticket<T, Alloc> {};
    }

    /// \brief Factory: allocate and construct T using `SystemAllocator`.
    template<class T, class... Args>
    [[nodiscard]] Shared<T, SystemAllocator> MakeShared(Args&&... args)
    {
        SystemAllocator alloc {};
        return MakeShared<T, SystemAllocator>(alloc, std::forward<Args>(args)...);
    }
}// namespace NGIN::Memory
