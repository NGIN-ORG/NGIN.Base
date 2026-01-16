/// @file Optional.hpp
/// @brief `NGIN::Utilities::Optional<T>`: a minimal, engine-friendly optional value type.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/StorageFor.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Utilities/Tags.hpp>
#include <NGIN/Primitives.hpp>

#include <cstdint>
#include <utility>

namespace NGIN::Utilities
{
    /// @brief Inline "maybe a T" with explicit lifetime and zero allocations.
    ///
    /// @details
    /// - Empty state is represented by an internal flag.
    /// - Construction/destruction happens in-place using `NGIN::Memory::StorageFor<T>`.
    /// - Checked accessors (`Value()`) follow a contract-fatal policy: assert in debug, abort in release.
    /// - Unsafe accessors (`ValueUnsafe()`, `operator*`, `operator->`) are undefined behavior if empty.
    ///
    /// @tparam T Stored value type. References are not supported.
    template <class T>
    class Optional
    {
        static_assert(!NGIN::Meta::TypeTraits<T>::IsReference(), "Optional<T&> is not supported.");

    public:
        /// @brief The contained value type.
        using ValueType = T;

        /// @brief Constructs an empty optional.
        constexpr Optional() noexcept = default;

        /// @brief Constructs an empty optional from `nullopt`.
        constexpr Optional(nullopt_t) noexcept
            : m_value {}
            , m_hasValue {false}
        {
        }

        /// @brief Constructs an engaged optional by copying `value`.
        ///
        /// @note This constructor is `explicit` to avoid accidental implicit conversions into `Optional<T>`.
        constexpr explicit Optional(const T& value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsCopyConstructible())
        {
            Emplace(value);
        }

        /// @brief Constructs an engaged optional by moving `value`.
        ///
        /// @note This constructor is `explicit` to avoid accidental implicit conversions into `Optional<T>`.
        constexpr explicit Optional(T&& value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
        {
            Emplace(std::move(value));
        }

        /// @brief Constructs an engaged optional in-place from `args...`.
        ///
        /// @param args Forwarded constructor arguments for `T`.
        template <class... Args>
        constexpr explicit Optional(InPlaceType<T>, Args&&... args)
            noexcept(NGIN::Meta::TypeTraits<T>::template IsNothrowConstructible<Args...>())
            requires(NGIN::Meta::TypeTraits<T>::template IsConstructible<Args...>())
        {
            Emplace(std::forward<Args>(args)...);
        }

        /// @brief Copy-constructs.
        ///
        /// @note This overload is used when `T` is trivially copyable so `Optional<T>` can remain trivially copyable.
        constexpr Optional(const Optional& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable())
        = default;

        constexpr Optional(const Optional& other) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible())
            requires(!NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<T>::IsCopyConstructible())
            : m_value {}
            , m_hasValue {false}
        {
            if (other.m_hasValue)
            {
                m_value.Construct(other.m_value.Ref());
                m_hasValue = true;
            }
        }

        /// @brief Move-constructs.
        ///
        /// @note This overload is used when `T` is trivially copyable so `Optional<T>` can remain trivially copyable.
        constexpr Optional(Optional&& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable())
        = default;

        constexpr Optional(Optional&& other) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            requires(!NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
            : m_value {}
            , m_hasValue {false}
        {
            if (other.m_hasValue)
            {
                m_value.Construct(std::move(other.m_value.Ref()));
                m_hasValue = true;
            }
        }

        /// @brief Copy-assigns.
        ///
        /// @note This overload is used when `T` is trivially copyable so `Optional<T>` can remain trivially copyable.
        constexpr Optional& operator=(const Optional& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable())
        = default;

        constexpr Optional& operator=(const Optional& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<T>::IsCopyAssignable() || NGIN::Meta::TypeTraits<T>::IsNothrowCopyAssignable()))
            requires(!NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<T>::IsCopyConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                return *this;
            }

            if (m_hasValue && !other.m_hasValue)
            {
                Reset();
                return *this;
            }

            if (!m_hasValue && other.m_hasValue)
            {
                Engage(other.m_value.Ref());
                return *this;
            }

            if constexpr (NGIN::Meta::TypeTraits<T>::IsCopyAssignable())
            {
                m_value.Ref() = other.m_value.Ref();
            }
            else
            {
                if constexpr (!NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
                {
                    m_value.Destroy();
                }
                m_value.Construct(other.m_value.Ref());
            }

            return *this;
        }

        /// @brief Move-assigns.
        ///
        /// @note This overload is used when `T` is trivially copyable so `Optional<T>` can remain trivially copyable.
        constexpr Optional& operator=(Optional&& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable())
        = default;

        constexpr Optional& operator=(Optional&& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<T>::IsMoveAssignable() || NGIN::Meta::TypeTraits<T>::IsNothrowMoveAssignable()))
            requires(!NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                return *this;
            }

            if (m_hasValue && !other.m_hasValue)
            {
                Reset();
                return *this;
            }

            if (!m_hasValue && other.m_hasValue)
            {
                Engage(std::move(other.m_value.Ref()));
                return *this;
            }

            if constexpr (NGIN::Meta::TypeTraits<T>::IsMoveAssignable())
            {
                m_value.Ref() = std::move(other.m_value.Ref());
            }
            else
            {
                if constexpr (!NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
                {
                    m_value.Destroy();
                }
                m_value.Construct(std::move(other.m_value.Ref()));
            }

            return *this;
        }

        /// @brief Destroys the contained value if needed.
        ///
        /// @note This overload is used when `T` is trivially destructible so `Optional<T>` can remain trivially destructible.
        constexpr ~Optional() noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
        = default;

        /// @brief Destroys the contained value if engaged.
        constexpr ~Optional() noexcept
            requires(!NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
        {
            Reset();
        }

        /// @brief Returns true if a value is currently stored.
        [[nodiscard]] constexpr bool HasValue() const noexcept { return m_hasValue; }

        /// @brief Returns true if a value is currently stored.
        constexpr explicit operator bool() const noexcept { return m_hasValue; }

        /// @brief Returns a pointer to the contained value, or `nullptr` if empty.
        [[nodiscard]] constexpr T* Ptr() noexcept { return m_hasValue ? m_value.Ptr() : nullptr; }

        /// @brief Returns a pointer to the contained value, or `nullptr` if empty.
        [[nodiscard]] constexpr const T* Ptr() const noexcept { return m_hasValue ? m_value.Ptr() : nullptr; }

        /// @brief Returns a pointer to the contained value, or `nullptr` if empty.
        ///
        /// @details Convenience alias for `Ptr()` to encourage a single-check pattern.
        [[nodiscard]] constexpr T* TryGet() noexcept { return Ptr(); }

        /// @brief Returns a pointer to the contained value, or `nullptr` if empty.
        ///
        /// @details Convenience alias for `Ptr()` to encourage a single-check pattern.
        [[nodiscard]] constexpr const T* TryGet() const noexcept { return Ptr(); }

        /// @brief Returns the contained value.
        ///
        /// @details Checked accessor: if empty, triggers the contract policy (assert in debug, abort in release).
        /// @return Reference to the contained `T`.
        constexpr T& Value() & noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                FailEmpty();
            }
            return m_value.Ref();
        }

        /// @brief Returns the contained value (const).
        ///
        /// @details Checked accessor: if empty, triggers the contract policy (assert in debug, abort in release).
        /// @return Reference to the contained `T`.
        constexpr const T& Value() const& noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                FailEmpty();
            }
            return m_value.Ref();
        }

        /// @brief Returns the contained value without checking.
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr T& ValueUnsafe() & noexcept { return m_value.Ref(); }

        /// @brief Returns the contained value without checking (const).
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr const T& ValueUnsafe() const& noexcept { return m_value.Ref(); }

        /// @brief Unsafe dereference operator.
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr T& operator*() noexcept { return ValueUnsafe(); }

        /// @brief Unsafe dereference operator (const).
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr const T& operator*() const noexcept { return ValueUnsafe(); }

        /// @brief Unsafe member access operator.
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr T* operator->() noexcept { return m_value.Ptr(); }

        /// @brief Unsafe member access operator (const).
        ///
        /// @warning Undefined behavior if the optional is empty.
        constexpr const T* operator->() const noexcept { return m_value.Ptr(); }

        /// @brief Returns the contained value if present, otherwise returns `fallback`.
        ///
        /// @details This is a zero-copy fallback path: the lifetime of `fallback` is owned by the caller.
        [[nodiscard]] constexpr const T& ValueOr(const T& fallback) const& noexcept
        {
            return m_hasValue ? ValueUnsafe() : fallback;
        }

        /// @brief Returns the contained value if present, otherwise returns `fallback`.
        ///
        /// @details Move-friendly overload for rvalues.
        [[nodiscard]] constexpr T ValueOr(T fallback) && noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
        {
            return m_hasValue ? std::move(ValueUnsafe()) : std::move(fallback);
        }

        /// @brief Destroys the contained value (if any) and makes the optional empty.
        constexpr void Reset() noexcept
        {
            if (m_hasValue)
            {
                if constexpr (!NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
                {
                    m_value.Destroy();
                }
                m_hasValue = false;
            }
        }

        /// @brief Destroys any existing value and constructs a new value in-place.
        ///
        /// @param args Forwarded constructor arguments for `T`.
        /// @return Reference to the newly constructed `T`.
        template <class... Args>
        constexpr T& Emplace(Args&&... args)
            noexcept(NGIN::Meta::TypeTraits<T>::template IsNothrowConstructible<Args...>() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<T>::template IsConstructible<Args...>())
        {
            Reset();
            return Engage(std::forward<Args>(args)...);
        }

        /// @brief Swaps this optional with another.
        ///
        /// @details
        /// If both are engaged, swaps their contained values.
        /// If only one is engaged, moves the engaged value into the empty one.
        constexpr void Swap(Optional& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveAssignable() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<T>::IsMoveConstructible() && NGIN::Meta::TypeTraits<T>::IsMoveAssignable())
        {
            if (m_hasValue && other.m_hasValue)
            {
                using std::swap;
                swap(m_value.Ref(), other.m_value.Ref());
                return;
            }

            if (m_hasValue && !other.m_hasValue)
            {
                other.Engage(std::move(m_value.Ref()));
                Reset();
                return;
            }

            if (!m_hasValue && other.m_hasValue)
            {
                Engage(std::move(other.m_value.Ref()));
                other.Reset();
                return;
            }
        }

    private:
        [[noreturn]] static void FailEmpty() noexcept
        {
            NGIN_ASSERT(false && "NGIN::Utilities::Optional::Value called when empty");
            NGIN_ABORT("NGIN::Utilities::Optional::Value called when empty");
        }

        template <class... Args>
        constexpr T& Engage(Args&&... args) noexcept(NGIN::Meta::TypeTraits<T>::template IsNothrowConstructible<Args...>())
            requires(NGIN::Meta::TypeTraits<T>::template IsConstructible<Args...>())
        {
            T& ref = m_value.Construct(std::forward<Args>(args)...);
            m_hasValue = 1;
            return ref;
        }

        [[no_unique_address]] NGIN::Memory::StorageFor<T> m_value {};
        NGIN::UInt8 m_hasValue {0};
    };
}
