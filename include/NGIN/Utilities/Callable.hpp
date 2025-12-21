#pragma once

#include <cstddef>    // std::byte, std::size_t
#include <type_traits>// std::decay_t, std::enable_if_t, std::is_nothrow_move_constructible_v,
                      // std::is_copy_constructible_v, std::is_move_constructible_v, std::is_invocable_r_v
#include <utility>    // std::forward, std::move, std::swap
#include <new>        // operator new, operator delete
#include <stdexcept>  // std::bad_function_call, std::runtime_error
#include <cstring>    // std::memcpy
#include <exception>  // std::terminate
#include <functional>

namespace NGIN::Utilities
{
    /// \brief Type-erased callable wrapper with small-buffer optimization (SBO).
    ///
    /// Callable provides type-erased storage and invocation for any callable object
    /// (function pointer, lambda, functor) matching a given signature. If the callable
    /// fits within an internal buffer (size and alignment constraints), it is stored inline;
    /// otherwise, heap allocation is used. Copy and move semantics are supported, and
    /// invoking an empty Callable throws std::bad_function_call. This class is designed
    /// for high performance and minimal memory overhead, aiming to outperform std::function.
    ///
    /// @tparam Signature Function signature, e.g. R(Args...)
    template<typename Signature>
    class Callable;

    // Specialization for R(Args...)
    template<typename R, typename... Args>
    class Callable<R(Args...)>
    {
    public:
        /// \brief Default-constructs an empty Callable.
        Callable() noexcept = default;

        /// \brief Constructs a Callable from any compatible callable object.
        ///
        /// Stores the callable inline if it fits the buffer size and alignment constraints and is nothrow-move-constructible;
        /// otherwise, allocates on the heap. Disabled for Callable itself.
        ///
        /// @tparam F Callable type invocable as R(Args...)
        /// @param f Callable object to store
        template<
                typename F,
                typename = std::enable_if_t<
                        std::is_invocable_r_v<R, F&, Args...> &&
                        !std::is_same_v<std::decay_t<F>, Callable>>>
        Callable(F&& f)
        {
            Init(std::forward<F>(f));
        }

        /// \brief Copy-constructs a Callable from another.
        ///
        /// Copies the stored callable, using heap or inline buffer as appropriate.
        /// Throws std::runtime_error if the target is not copyable.
        /// @param other Callable to copy from
        Callable(const Callable& other)
        {
            CopyFrom(other);
        }

        /// \brief Move-constructs a Callable from another.
        ///
        /// Moves the stored callable, leaving the source empty.
        /// @param other Callable to move from
        Callable(Callable&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        /// \brief Copy-assigns from another Callable.
        ///
        /// Provides the strong exception guarantee: if copying throws, this Callable is unchanged.
        /// Throws std::runtime_error if the target is not copyable.
        /// @param other Callable to copy from
        Callable& operator=(const Callable& other)
        {
            if (this != &other)
            {
                Callable copy(other);
                Swap(copy);
            }
            return *this;
        }

        /// \brief Move-assigns from another Callable.
        ///
        /// Destroys any existing callable, then moves from the source.
        /// @param other Callable to move from
        Callable& operator=(Callable&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        /// \brief Assigns nullptr, making this Callable empty.
        Callable& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        /// \brief Assigns from any compatible callable object.
        ///
        /// Destroys any existing callable, then stores the new one.
        /// Disabled for Callable itself.
        /// @tparam F Callable type invocable as R(Args...)
        /// @param f Callable object to store
        template<
                typename F,
                typename = std::enable_if_t<
                        std::is_invocable_r_v<R, F&, Args...> &&
                        !std::is_same_v<std::decay_t<F>, Callable>>>
        Callable& operator=(F&& f)
        {
            Reset();
            Init(std::forward<F>(f));
            return *this;
        }

        /// \brief Destructor. Destroys any stored callable.
        ~Callable()
        {
            Reset();
        }

        /// \brief Invokes the stored callable.
        ///
        /// Throws std::bad_function_call if empty.
        /// @param args Arguments to pass to the callable
        /// @return Result of invoking the callable
        auto operator()(Args... args) const -> R
        {
            if (!m_vtable)
            {
                throw std::bad_function_call();
            }
            return m_vtable->invoke(const_cast<void*>(GetPtr()), std::forward<Args>(args)...);
        }

        /// \brief Checks if the Callable is non-empty.
        /// @return True if a callable is stored, false otherwise
        explicit operator bool() const noexcept
        {
            return m_vtable != nullptr;
        }

        /// \brief Destroys any stored callable and makes this empty.
        void Reset() noexcept
        {
            if (m_vtable)
            {
                m_vtable->destroy(GetPtr());
            }
            m_vtable    = nullptr;
            m_usingHeap = false;
        }

        /// \brief Swaps the contents of two Callables.
        ///
        /// Both inline and heap-stored cases are handled.
        /// @param other Callable to swap with
        void Swap(Callable& other) noexcept
        {
            if (this == &other)
            {
                return;
            }

            if (m_usingHeap && other.m_usingHeap)
            {
                using std::swap;
                swap(m_storage.heapPtr, other.m_storage.heapPtr);
                swap(m_vtable, other.m_vtable);
                return;
            }

            Callable temp(std::move(other));
            other = std::move(*this);
            *this = std::move(temp);
        }

    private:
        /// \brief Number of bytes reserved for inline storage (SBO).
        static constexpr std::size_t BUFFER_SIZE = sizeof(void*) * 4;

        /// \brief Required alignment for the inline buffer.
        static constexpr std::size_t ALIGNMENT = alignof(std::max_align_t);

        /// \brief Type-erased function pointer for invocation.
        ///
        /// Accepts a pointer to the stored object and arguments, returns R.
        using InvokeFn = R (*)(void*, Args&&...);

        /// \brief Type-erased function pointer for copy construction.
        ///
        /// Constructs a copy of the stored object at dest from src.
        using CopyFn = void (*)(void* dest, const void* src);

        /// \brief Type-erased function pointer for move construction.
        ///
        /// Constructs a moved object at dest from src.
        using MoveFn = void (*)(void* dest, void* src);

        /// \brief Type-erased function pointer for destruction.
        ///
        /// Destroys the stored object and, if heap-allocated, deallocates memory.
        using DestroyFn = void (*)(void* storagePtr) noexcept;

        struct VTable
        {
            InvokeFn invoke;
            CopyFn copy;
            MoveFn move;
            DestroyFn destroy;
            std::size_t size;
            std::size_t alignment;
        };

        template<typename T, bool IsHeap>
        struct VTableFor
        {
            static auto Invoke(void* ptr, Args&&... args) -> R
            {
                auto* obj = static_cast<T*>(ptr);
                return (*obj)(std::forward<Args>(args)...);
            }

            static void Copy(void* dest, const void* src)
            {
                if constexpr (std::is_trivially_copyable_v<T>)
                {
                    std::memcpy(dest, src, sizeof(T));
                }
                else if constexpr (std::is_copy_constructible_v<T>)
                {
                    new (dest) T(*static_cast<const T*>(src));
                }
                else
                {
                    std::terminate();
                }
            }

            static void Move(void* dest, void* src)
            {
                if constexpr (std::is_trivially_copyable_v<T>)
                {
                    std::memcpy(dest, src, sizeof(T));
                }
                else if constexpr (std::is_move_constructible_v<T>)
                {
                    new (dest) T(std::move(*static_cast<T*>(src)));
                }
                else
                {
                    std::terminate();
                }
            }

            static void Destroy(void* ptr) noexcept
            {
                auto* obj = static_cast<T*>(ptr);
                if constexpr (IsHeap)
                {
                    if constexpr (!std::is_trivially_destructible_v<T>)
                    {
                        obj->~T();
                    }
                    ::operator delete(obj, sizeof(T), std::align_val_t {alignof(T)});
                }
                else
                {
                    if constexpr (!std::is_trivially_destructible_v<T>)
                    {
                        obj->~T();
                    }
                }
            }

            static constexpr VTable value = {
                    .invoke = &Invoke,
                    .copy = (std::is_copy_constructible_v<T> || std::is_trivially_copyable_v<T>) ? &Copy : nullptr,
                    .move = (std::is_move_constructible_v<T> || std::is_trivially_copyable_v<T>) ? &Move : nullptr,
                    .destroy = &Destroy,
                    .size = sizeof(T),
                    .alignment = alignof(T),
            };
        };

        /// \brief Storage for the callable: either inline buffer or heap pointer.
        alignas(ALIGNMENT) union Storage
        {
            std::byte buffer[BUFFER_SIZE];///< Inline buffer for SBO
            void* heapPtr;                ///< Heap pointer for large objects
        } m_storage {};

        /// \brief True if the callable is stored on the heap, false if inline.
        bool m_usingHeap = false;

        const VTable* m_vtable = nullptr;

        /// \brief Returns a pointer to the stored object (inline or heap).
        /// @return Pointer to the stored callable object
        void* GetPtr() noexcept
        {
            return m_usingHeap ? m_storage.heapPtr : static_cast<void*>(m_storage.buffer);
        }

        /// \brief Returns a const pointer to the stored object (inline or heap).
        /// @return Const pointer to the stored callable object
        [[nodiscard]] const void* GetPtr() const noexcept
        {
            return m_usingHeap ? m_storage.heapPtr : static_cast<const void*>(m_storage.buffer);
        }

        /// \brief Initializes the Callable with a new callable object.
        ///
        /// Decays F, chooses inline or heap storage, and sets up type-erased operations.
        /// @tparam F Callable type
        /// @param f Callable object to store
        template<typename F>
        void Init(F&& f)
        {
            using DecayedF = std::decay_t<F>;

            constexpr bool fitsInline =
                    (sizeof(DecayedF) <= BUFFER_SIZE) &&
                    (alignof(DecayedF) <= ALIGNMENT) &&
                    std::is_nothrow_move_constructible_v<DecayedF>;

            if constexpr (fitsInline)
            {
                new (m_storage.buffer) DecayedF(std::forward<F>(f));
                m_usingHeap = false;
                m_vtable    = &VTableFor<DecayedF, false>::value;
            }
            else
            {
                /// Heap storage path (for large or non-SBO types)
                void* raw     = ::operator new(sizeof(DecayedF), std::align_val_t {alignof(DecayedF)});
                auto* heapObj = static_cast<DecayedF*>(raw);
                try
                {
                    new (heapObj) DecayedF(std::forward<F>(f));
                } catch (...)
                {
                    ::operator delete(heapObj, sizeof(DecayedF), std::align_val_t {alignof(DecayedF)});
                    throw;
                }
                m_storage.heapPtr = heapObj;
                m_usingHeap       = true;
                m_vtable          = &VTableFor<DecayedF, true>::value;
            }
        }

        /// \brief Copies the contents of another Callable.
        ///
        /// If the source is empty, does nothing. Otherwise, copies the stored object
        /// and all type-erased operations. Throws std::runtime_error if copying is not supported.
        /// @param other Callable to copy from
        void CopyFrom(const Callable& other)
        {
            if (!other.m_vtable)
            {
                // Other is empty → remain empty.
                return;
            }

            if (!other.m_vtable->copy)
            {
                throw std::runtime_error("Callable: copy attempted on non-copyable target");
            }

            const bool otherUsesHeap = other.m_usingHeap;

            if (otherUsesHeap)
            {
                // Allocate exactly the target size and alignment (must match the deleter in the vtable).
                void* raw = ::operator new(other.m_vtable->size, std::align_val_t {other.m_vtable->alignment});
                try
                {
                    other.m_vtable->copy(raw, other.GetPtr());
                    m_storage.heapPtr = raw;
                    m_usingHeap       = true;
                } catch (...)
                {
                    ::operator delete(raw, other.m_vtable->size, std::align_val_t {other.m_vtable->alignment});
                    m_storage.heapPtr = nullptr;
                    m_usingHeap       = false;
                    throw;
                }
            }
            else
            {
                // Inline copy
                other.m_vtable->copy(static_cast<void*>(m_storage.buffer), other.GetPtr());
                m_usingHeap = false;
            }

            m_vtable = other.m_vtable;
        }

        /// \brief Moves the contents of another Callable.
        ///
        /// If the source is empty, does nothing. Otherwise, moves the stored object
        /// and all type-erased operations, leaving the source empty.
        /// @param other Callable to move from
        void MoveFrom(Callable&& other) noexcept
        {
            if (!other.m_vtable)
            {
                // Other is empty → remain empty.
                return;
            }

            m_usingHeap = other.m_usingHeap;
            m_vtable    = other.m_vtable;

            if (other.m_usingHeap)
            {
                // Steal the pointer directly:
                m_storage.heapPtr       = other.m_storage.heapPtr;
                other.m_storage.heapPtr = nullptr;
            }
            else
            {
                // Inline-buffer move:
                if (!other.m_vtable->move)
                {
                    std::terminate();
                }
                other.m_vtable->move(static_cast<void*>(m_storage.buffer), other.GetPtr());
                // Destroy the “source” inline so it isn’t double-destroyed:
                other.m_vtable->destroy(other.GetPtr());
            }

            // Zero-out “other” so it becomes empty:
            other.m_vtable    = nullptr;
            other.m_usingHeap = false;
        }
    };
}// namespace NGIN::Utilities
