#pragma once

#include <cstddef>      // std::byte, std::size_t
#include <new>          // ::new, std::launder
#include <type_traits>  // std::is_*, std::enable_if_t
#include <utility>      // std::forward, std::move

namespace NGIN::Memory
{
    /// @brief Raw, properly-aligned inline storage for a value of type `T` without tracking lifetime.
    ///
    /// @details
    /// `StorageFor<T>` provides a buffer that is large and aligned enough to hold a `T`, plus a small
    /// set of helpers to construct, access, and destroy a `T` in that buffer.
    ///
    /// **Important:** `StorageFor<T>` does **not** track whether a `T` is currently alive.
    /// There is no "engaged" flag in this type. The owner (e.g. Optional/Expected/Variant,
    /// container node, ECS pool slot) must track lifetime externally and only call `Ptr()/Ref()`
    /// and `Destroy()` when an object is known to be alive.
    ///
    /// This separation keeps `StorageFor<T>`:
    /// - Zero-overhead: it is essentially just `sizeof(T)` bytes with `alignof(T)` alignment.
    /// - Composable: the owner decides the lifetime rules (one-of, maybe, N-of, etc).
    ///
    /// ## Typical usage pattern
    /// ```cpp
    /// bool hasValue = false;
    /// NGIN::Memory::StorageFor<MyType> storage;
    ///
    /// // Construct
    /// storage.Construct(args...);
    /// hasValue = true;
    ///
    /// // Access
    /// if (hasValue) { storage.Ref().DoThing(); }
    ///
    /// // Destroy
    /// if (hasValue) { storage.Destroy(); hasValue = false; }
    /// ```
    ///
    /// ## Undefined behavior contracts
    /// - Calling `Ptr()` / `Ref()` when no `T` is alive is undefined behavior.
    /// - Calling `Destroy()` when no `T` is alive is undefined behavior.
    /// - Constructing twice without destroying the previous object is undefined behavior.
    ///
    /// @tparam T The stored type. References are not supported (`StorageFor<T&>` is ill-formed).
    template <class T>
    class StorageFor
    {
        static_assert(!std::is_reference_v<T>,
            "StorageFor<T&> is not supported. Use StorageFor<std::remove_reference_t<T>> plus a pointer if needed.");

    public:
        /// @brief The stored value type.
        using ValueType = T;

        /// @brief Constructs an empty storage buffer. Does not construct a `T`.
        ///
        /// @note This does not initialize the bytes. The owner is responsible for constructing
        /// an object via `Construct()` before accessing it.
        constexpr StorageFor() noexcept = default;

        /// @brief `StorageFor` is non-copyable.
        ///
        /// @details
        /// `StorageFor` itself does not model an owning value. Copying raw storage would be
        /// ambiguous (should it copy bytes? copy-construct an object? only if alive?).
        /// The owning wrapper (Optional/Expected/Variant/container) should implement copy/move
        /// semantics explicitly using `CopyConstructFrom` / `MoveConstructFrom` when appropriate.
        StorageFor(const StorageFor&) = delete;

        /// @brief `StorageFor` is non-assignable.
        StorageFor& operator=(const StorageFor&) = delete;

        /// @brief Destructor does nothing.
        ///
        /// @details
        /// `StorageFor` does not know whether a `T` is alive, so it cannot safely destroy anything.
        /// The owner must call `Destroy()` at the appropriate time.
        ~StorageFor() = default;

        /// @brief Returns a pointer to the storage interpreted as `T*`.
        ///
        /// @details
        /// This is only valid if a `T` object is currently alive in this storage (constructed via
        /// `Construct()` or the construct-from helpers).
        ///
        /// `std::launder` is used to comply with C++ object lifetime rules after placement-new.
        ///
        /// @return Pointer to the contained `T`.
        /// @warning Undefined behavior if no `T` is alive.
        constexpr T* Ptr() noexcept
        {
            return std::launder(reinterpret_cast<T*>(m_data));
        }

        /// @brief Returns a pointer to the storage interpreted as `const T*`.
        ///
        /// @return Pointer to the contained `T`.
        /// @warning Undefined behavior if no `T` is alive.
        constexpr const T* Ptr() const noexcept
        {
            return std::launder(reinterpret_cast<const T*>(m_data));
        }

        /// @brief Returns a reference to the contained `T`.
        ///
        /// @return Reference to the contained `T`.
        /// @warning Undefined behavior if no `T` is alive.
        constexpr T& Ref() noexcept
        {
            return *Ptr();
        }

        /// @brief Returns a reference to the contained `T` (const).
        ///
        /// @return Reference to the contained `T`.
        /// @warning Undefined behavior if no `T` is alive.
        constexpr const T& Ref() const noexcept
        {
            return *Ptr();
        }

        /// @brief Constructs a `T` in-place within this storage using perfect forwarding.
        ///
        /// @tparam Args Constructor argument types.
        /// @param args Constructor arguments forwarded to `T`'s constructor.
        ///
        /// @return Reference to the newly constructed `T`.
        ///
        /// @warning Undefined behavior if a `T` is already alive in this storage.
        ///
        /// @note The owner should set its "engaged"/state flag *after* successful construction
        /// (or according to its chosen exception/failure policy).
        template <class... Args>
        constexpr T& Construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            ::new (static_cast<void*>(m_data)) T(std::forward<Args>(args)...);
            return Ref();
        }

        /// @brief Destroys the contained `T` if it is non-trivially destructible.
        ///
        /// @details
        /// For trivially destructible `T`, this becomes a no-op (compile-time).
        ///
        /// @warning Undefined behavior if no `T` is alive.
        constexpr void Destroy() noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                Ref().~T();
            }
        }

        /// @brief Convenience helper: destroys the contained `T` if `isAlive` is true.
        ///
        /// @param isAlive Whether the owner believes a `T` is alive in this storage.
        ///
        /// @note This is a convenience to keep owner code tidy. If `isAlive` is wrong,
        /// behavior follows the same rules as `Destroy()`.
        constexpr void DestroyIf(bool isAlive) noexcept
        {
            if (isAlive)
            {
                Destroy();
            }
        }

        /// @brief Copy-constructs a `T` in this storage from another alive `T`.
        ///
        /// @param other Storage containing an alive `T` to copy from.
        ///
        /// @return Reference to the newly constructed `T`.
        ///
        /// @warning Undefined behavior if:
        /// - `other` does not contain an alive `T`, or
        /// - this storage already contains an alive `T`.
        constexpr T& CopyConstructFrom(const StorageFor& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            return Construct(other.Ref());
        }

        /// @brief Move-constructs a `T` in this storage from another alive `T`.
        ///
        /// @param other Storage containing an alive `T` to move from.
        ///
        /// @return Reference to the newly constructed `T`.
        ///
        /// @warning Undefined behavior if:
        /// - `other` does not contain an alive `T`, or
        /// - this storage already contains an alive `T`.
        constexpr T& MoveConstructFrom(StorageFor& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            return Construct(std::move(other.Ref()));
        }

        /// @brief Reconstructs this storage by destroying the current object (if `isAlive`) and
        /// then constructing a new `T` in-place.
        ///
        /// @tparam Args Constructor argument types.
        /// @param isAlive Whether a `T` is currently alive in this storage.
        /// @param args Constructor arguments forwarded to `T`'s constructor.
        ///
        /// @return Reference to the newly constructed `T`.
        ///
        /// @note This is useful for wrappers that track lifetime externally and want a single call
        /// that safely "replaces" the object based on their state.
        template <class... Args>
        constexpr T& Reconstruct(bool isAlive, Args&&... args)
            noexcept(std::is_nothrow_constructible_v<T, Args...> && std::is_nothrow_destructible_v<T>)
        {
            DestroyIf(isAlive);
            return Construct(std::forward<Args>(args)...);
        }

        /// @brief Returns the size (in bytes) of the storage buffer.
        static constexpr std::size_t Size() noexcept { return sizeof(T); }

        /// @brief Returns the alignment (in bytes) of the storage buffer.
        static constexpr std::size_t Alignment() noexcept { return alignof(T); }

    private:
        /// @brief Raw inline bytes used to hold a `T` with correct alignment.
        alignas(T) std::byte m_data[sizeof(T)];
    };
} // namespace ngin
