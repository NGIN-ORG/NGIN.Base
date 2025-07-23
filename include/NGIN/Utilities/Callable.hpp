#pragma once

#include <cstddef>    // std::byte, std::size_t
#include <type_traits>// std::decay_t, std::enable_if_t, std::is_nothrow_move_constructible_v,
                      // std::is_copy_constructible_v, std::is_move_constructible_v, std::is_invocable_r_v
#include <utility>    // std::forward, std::move, std::swap
#include <new>        // operator new, operator delete
#include <stdexcept>  // std::bad_function_call, std::runtime_error
#include <cstring>    // std::memcpy
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
            init(std::forward<F>(f));
        }

        /// \brief Copy-constructs a Callable from another.
        ///
        /// Copies the stored callable, using heap or inline buffer as appropriate.
        /// Throws std::runtime_error if the target is not copyable.
        /// @param other Callable to copy from
        Callable(const Callable& other)
        {
            copy_from(other);
        }

        /// \brief Move-constructs a Callable from another.
        ///
        /// Moves the stored callable, leaving the source empty.
        /// @param other Callable to move from
        Callable(Callable&& other) noexcept
        {
            move_from(std::move(other));
        }

        /// \brief Copy-assigns from another Callable.
        ///
        /// Destroys any existing callable, then copies from the source.
        /// Throws std::runtime_error if the target is not copyable.
        /// @param other Callable to copy from
        Callable& operator=(const Callable& other)
        {
            if (this != &other)
            {
                reset();
                copy_from(other);
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
                reset();
                move_from(std::move(other));
            }
            return *this;
        }

        /// \brief Assigns nullptr, making this Callable empty.
        Callable& operator=(std::nullptr_t) noexcept
        {
            reset();
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
            reset();
            init(std::forward<F>(f));
            return *this;
        }

        /// \brief Destructor. Destroys any stored callable.
        ~Callable()
        {
            reset();
        }

        /// \brief Invokes the stored callable.
        ///
        /// Throws std::bad_function_call if empty.
        /// @param args Arguments to pass to the callable
        /// @return Result of invoking the callable
        R operator()(Args... args) const
        {
            if (!m_invoke)
            {
                throw std::bad_function_call();
            }
            return m_invoke(const_cast<void*>(get_ptr()), std::forward<Args>(args)...);
        }

        /// \brief Checks if the Callable is non-empty.
        /// @return True if a callable is stored, false otherwise
        explicit operator bool() const noexcept
        {
            return m_invoke != nullptr;
        }

        /// \brief Destroys any stored callable and makes this empty.
        void reset() noexcept
        {
            if (m_destroy)
            {
                m_destroy(get_ptr());
            }
            m_invoke    = nullptr;
            m_copy      = nullptr;
            m_move      = nullptr;
            m_destroy   = nullptr;
            m_size      = 0;
            m_usingHeap = false;
        }

        /// \brief Swaps the contents of two Callables in O(1) time.
        ///
        /// Both inline and heap-stored cases are handled.
        /// @param other Callable to swap with
        void swap(Callable& other) noexcept
        {
            using std::swap;
            if (!m_invoke && !other.m_invoke)
            {
                return;
            }
            swap(m_storage, other.m_storage);
            swap(m_size, other.m_size);
            swap(m_usingHeap, other.m_usingHeap);
            swap(m_invoke, other.m_invoke);
            swap(m_copy, other.m_copy);
            swap(m_move, other.m_move);
            swap(m_destroy, other.m_destroy);
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
        using DestroyFn = void (*)(void* storagePtr);

        /// \brief Storage for the callable: either inline buffer or heap pointer.
        union alignas(ALIGNMENT) Storage
        {
            std::byte buffer[BUFFER_SIZE];///< Inline buffer for SBO
            void* heapPtr;                ///< Heap pointer for large objects

            Storage() noexcept {}
            ~Storage() noexcept {}
        } m_storage {};

        /// \brief Size in bytes of the stored object (0 if empty).
        std::size_t m_size = 0;

        /// \brief True if the callable is stored on the heap, false if inline.
        bool m_usingHeap = false;

        /// \brief Type-erased function pointers for operations.
        InvokeFn m_invoke   = nullptr;
        CopyFn m_copy       = nullptr;
        MoveFn m_move       = nullptr;
        DestroyFn m_destroy = nullptr;

        /// \brief Returns a pointer to the stored object (inline or heap).
        /// @return Pointer to the stored callable object
        void* get_ptr() noexcept
        {
            return m_usingHeap ? m_storage.heapPtr : static_cast<void*>(m_storage.buffer);
        }

        /// \brief Returns a const pointer to the stored object (inline or heap).
        /// @return Const pointer to the stored callable object
        const void* get_ptr() const noexcept
        {
            return m_usingHeap ? m_storage.heapPtr : static_cast<const void*>(m_storage.buffer);
        }

        /// \brief Initializes the Callable with a new callable object.
        ///
        /// Decays F, chooses inline or heap storage, and sets up type-erased operations.
        /// @tparam F Callable type
        /// @param f Callable object to store
        template<typename F>
        void init(F&& f)
        {
            using DecayedF = std::decay_t<F>;

            constexpr bool fits_inline =
                    (sizeof(DecayedF) <= BUFFER_SIZE) &&
                    (alignof(DecayedF) <= ALIGNMENT) &&
                    std::is_nothrow_move_constructible_v<DecayedF>;

            // Record the exact size of the object we’re storing:
            m_size = sizeof(DecayedF);

            if constexpr (fits_inline)
            {
                /// Inline (SBO) storage path
                // Fast path for function pointers
                if constexpr (std::is_pointer_v<DecayedF> && std::is_function_v<std::remove_pointer_t<DecayedF>>)
                {
                    new (m_storage.buffer) DecayedF(f);
                    m_usingHeap = false;
                    m_size      = sizeof(DecayedF);
                    m_invoke    = [](void* ptr, Args&&... args) -> R {
                        auto fn = *reinterpret_cast<DecayedF*>(ptr);
                        return fn(std::forward<Args>(args)...);
                    };
                    m_copy = [](void* dest, const void* src) {
                        std::memcpy(dest, src, sizeof(DecayedF));
                    };
                    m_move = [](void* dest, void* src) {
                        std::memcpy(dest, src, sizeof(DecayedF));
                    };
                    m_destroy = nullptr;
                    return;
                }

                // Fast path for stateless lambdas (no captures)
                if constexpr (std::is_empty_v<DecayedF> && std::is_trivially_copyable_v<DecayedF>)
                {
                    new (m_storage.buffer) DecayedF(std::forward<F>(f));
                    m_usingHeap = false;
                    m_size      = sizeof(DecayedF);
                    m_invoke    = [](void* ptr, Args&&... args) -> R {
                        auto& obj = *reinterpret_cast<DecayedF*>(ptr);
                        return obj(std::forward<Args>(args)...);
                    };
                    m_copy = [](void* dest, const void* src) {
                        std::memcpy(dest, src, sizeof(DecayedF));
                    };
                    m_move = [](void* dest, void* src) {
                        std::memcpy(dest, src, sizeof(DecayedF));
                    };
                    m_destroy = nullptr;
                    return;
                }
                void* dest = static_cast<void*>(m_storage.buffer);
                new (dest) DecayedF(std::forward<F>(f));
                m_usingHeap = false;

                // Destroy only calls the destructor; no delete
                if constexpr (std::is_trivially_destructible_v<DecayedF>)
                {
                    m_destroy = nullptr;
                }
                else if constexpr (std::is_destructible_v<DecayedF>)
                {
                    m_destroy = [](void* ptr) {
                        auto* obj = static_cast<DecayedF*>(ptr);
                        obj->~DecayedF();
                    };
                }
                else
                {
                    m_destroy = nullptr;
                }

                m_invoke = [](void* ptr, Args&&... args) -> R {
                    auto* obj = static_cast<DecayedF*>(ptr);
                    return (*obj)(std::forward<Args>(args)...);
                };

                if constexpr (std::is_copy_constructible_v<DecayedF>)
                {
                    m_copy = [](void* dest, const void* src) {
                        new (dest) DecayedF(*static_cast<const DecayedF*>(src));
                    };
                }
                else
                {
                    m_copy = nullptr;
                }

                if constexpr (std::is_move_constructible_v<DecayedF>)
                {
                    m_move = [](void* dest, void* src) {
                        new (dest) DecayedF(std::move(*static_cast<DecayedF*>(src)));
                    };
                }
                else
                {
                    m_move = nullptr;
                }
                return;
            }
            else
            {
                /// Heap storage path (for large or non-SBO types)
                void* raw         = ::operator new(sizeof(DecayedF), std::align_val_t {alignof(DecayedF)});
                DecayedF* heapObj = static_cast<DecayedF*>(raw);
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

                m_invoke = [](void* ptr, Args&&... args) -> R {
                    auto* obj = static_cast<DecayedF*>(ptr);
                    return (*obj)(std::forward<Args>(args)...);
                };

                if constexpr (std::is_copy_constructible_v<DecayedF>)
                {
                    m_copy = [](void* dest, const void* src) {
                        new (dest) DecayedF(*static_cast<const DecayedF*>(src));
                    };
                }
                else
                {
                    m_copy = nullptr;
                }

                if constexpr (std::is_move_constructible_v<DecayedF>)
                {
                    m_move = [](void* dest, void* src) {
                        new (dest) DecayedF(std::move(*static_cast<DecayedF*>(src)));
                    };
                }
                else
                {
                    m_move = nullptr;
                }

                if constexpr (std::is_trivially_destructible_v<DecayedF>)
                {
                    m_destroy = [](void* ptr) {
                        ::operator delete(ptr, sizeof(DecayedF), std::align_val_t {alignof(DecayedF)});
                    };
                }
                else if constexpr (std::is_destructible_v<DecayedF>)
                {
                    m_destroy = [](void* ptr) {
                        auto* obj = static_cast<DecayedF*>(ptr);
                        obj->~DecayedF();
                        ::operator delete(obj, sizeof(DecayedF), std::align_val_t {alignof(DecayedF)});
                    };
                }
                else
                {
                    m_destroy = [](void* ptr) {
                        ::operator delete(ptr, std::align_val_t {alignof(DecayedF)});
                    };
                }
            }
        }

        /// \brief Copies the contents of another Callable.
        ///
        /// If the source is empty, does nothing. Otherwise, copies the stored object
        /// and all type-erased operations. Throws std::runtime_error if copying is not supported.
        /// @param other Callable to copy from
        void copy_from(const Callable& other)
        {
            if (!other.m_invoke)
            {
                // Other is empty → remain empty.
                return;
            }

            if (!other.m_copy)
            {
                throw std::runtime_error("Callable: copy attempted on non-copyable target");
            }

            m_size      = other.m_size;
            m_usingHeap = other.m_usingHeap;

            if (other.m_usingHeap)
            {
                // Allocate exactly other.m_size bytes with max alignment ALIGNMENT.
                void* raw = ::operator new(other.m_size, std::align_val_t {ALIGNMENT});
                other.m_copy(raw, other.get_ptr());
                m_storage.heapPtr = raw;
            }
            else
            {
                // Inline copy
                other.m_copy(static_cast<void*>(m_storage.buffer), other.get_ptr());
            }

            // Copy all function-pointers:
            m_invoke  = other.m_invoke;
            m_copy    = other.m_copy;
            m_move    = other.m_move;
            m_destroy = other.m_destroy;
        }

        /// \brief Moves the contents of another Callable.
        ///
        /// If the source is empty, does nothing. Otherwise, moves the stored object
        /// and all type-erased operations, leaving the source empty.
        /// @param other Callable to move from
        void move_from(Callable&& other) noexcept
        {
            if (!other.m_invoke)
            {
                // Other is empty → remain empty.
                return;
            }

            m_size      = other.m_size;
            m_usingHeap = other.m_usingHeap;

            if (other.m_usingHeap)
            {
                // Steal the pointer directly:
                m_storage.heapPtr       = other.m_storage.heapPtr;
                other.m_storage.heapPtr = nullptr;
            }
            else
            {
                // Inline-buffer move:
                if (other.m_move)
                {
                    other.m_move(static_cast<void*>(m_storage.buffer), other.get_ptr());
                    // Destroy the “source” inline so it isn’t double-destroyed:
                    if (other.m_destroy)
                    {
                        other.m_destroy(other.get_ptr());
                        std::memset(other.m_storage.buffer, 0, BUFFER_SIZE);
                    }
                }
                else
                {
                    std::memcpy(m_storage.buffer, other.m_storage.buffer, BUFFER_SIZE);
                    std::memset(other.m_storage.buffer, 0, BUFFER_SIZE);
                }
            }

            // Move function-pointers:
            m_invoke  = other.m_invoke;
            m_copy    = other.m_copy;
            m_move    = other.m_move;
            m_destroy = other.m_destroy;

            // Zero-out “other” so it becomes empty:
            other.m_invoke    = nullptr;
            other.m_copy      = nullptr;
            other.m_move      = nullptr;
            other.m_destroy   = nullptr;
            other.m_size      = 0;
            other.m_usingHeap = false;
        }
    };
}// namespace NGIN::Utilities
