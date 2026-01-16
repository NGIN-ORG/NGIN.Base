#pragma once

#include <cstddef>      // std::byte, std::size_t
#include <new>          // ::new, std::launder
#include <type_traits>  // std::is_*, std::enable_if_t
#include <utility>      // std::forward, std::move

namespace NGIN::Memory
{
    namespace detail
    {
        template <class T>
        class StorageForCommon
        {
            static_assert(!std::is_reference_v<T>,
                "StorageFor<T&> is not supported. Use StorageFor<std::remove_reference_t<T>> plus a pointer if needed.");

        public:
            using ValueType = T;

            constexpr StorageForCommon() noexcept = default;
            ~StorageForCommon() = default;

            constexpr T* Ptr() noexcept
            {
                return std::launder(reinterpret_cast<T*>(m_data));
            }

            constexpr const T* Ptr() const noexcept
            {
                return std::launder(reinterpret_cast<const T*>(m_data));
            }

            constexpr T& Ref() noexcept
            {
                return *Ptr();
            }

            constexpr const T& Ref() const noexcept
            {
                return *Ptr();
            }

            template <class... Args>
            constexpr T& Construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
            {
                ::new (static_cast<void*>(m_data)) T(std::forward<Args>(args)...);
                return Ref();
            }

            constexpr void Destroy() noexcept
            {
                if constexpr (!std::is_trivially_destructible_v<T>)
                {
                    Ref().~T();
                }
            }

            constexpr void DestroyIf(bool isAlive) noexcept
            {
                if (isAlive)
                {
                    Destroy();
                }
            }

            constexpr T& CopyConstructFrom(const StorageForCommon& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
            {
                return Construct(other.Ref());
            }

            constexpr T& MoveConstructFrom(StorageForCommon& other) noexcept(std::is_nothrow_move_constructible_v<T>)
            {
                return Construct(std::move(other.Ref()));
            }

            template <class... Args>
            constexpr T& Reconstruct(bool isAlive, Args&&... args)
                noexcept(std::is_nothrow_constructible_v<T, Args...> && std::is_nothrow_destructible_v<T>)
            {
                DestroyIf(isAlive);
                return Construct(std::forward<Args>(args)...);
            }

            static constexpr std::size_t Size() noexcept { return sizeof(T); }
            static constexpr std::size_t Alignment() noexcept { return alignof(T); }

        protected:
            alignas(T) std::byte m_data[sizeof(T)];
        };
    }

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
    template <class T, bool IsTriviallyCopyable = std::is_trivially_copyable_v<T>>
    class StorageFor;

    /// @brief Trivially-copyable storage when `T` is trivially copyable.
    ///
    /// @details
    /// This enables wrapper types that include `StorageFor<T>` + an "alive" flag to remain trivially
    /// copyable/movable when the contained `T` permits it.
    template <class T>
    class StorageFor<T, true> : public detail::StorageForCommon<T>
    {
    public:
        constexpr StorageFor() noexcept = default;
        StorageFor(const StorageFor&) noexcept = default;
        StorageFor(StorageFor&&) noexcept = default;
        StorageFor& operator=(const StorageFor&) noexcept = default;
        StorageFor& operator=(StorageFor&&) noexcept = default;
        ~StorageFor() = default;
    };

    /// @brief Non-copyable/non-movable storage when `T` is not trivially copyable.
    ///
    /// @details
    /// For non-trivial types, byte-copying the storage would be unsafe unless the owning wrapper
    /// carefully controls construction/destruction. The wrapper should implement copy/move semantics
    /// explicitly using `CopyConstructFrom` / `MoveConstructFrom`.
    template <class T>
    class StorageFor<T, false> : public detail::StorageForCommon<T>
    {
    public:
        constexpr StorageFor() noexcept = default;

        StorageFor(const StorageFor&) = delete;
        StorageFor(StorageFor&&) = delete;
        StorageFor& operator=(const StorageFor&) = delete;
        StorageFor& operator=(StorageFor&&) = delete;

        ~StorageFor() = default;
    };
} // namespace NGIN::Memory
