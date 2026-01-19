/// @file Expected.hpp
/// @brief `NGIN::Utilities::Expected<T, E>`: a minimal, engine-friendly expected value type.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/StorageFor.hpp>
#include <NGIN/Memory/UnionStorageFor.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Tags.hpp>

#include <utility>

namespace NGIN::Utilities
{
    /// @brief Wrapper used to explicitly construct an error value for `Expected<T, E>`.
    ///
    /// @tparam E Error type.
    template <class E>
    class Unexpected
    {
    public:
        /// @brief Error type.
        using ErrorType = E;

        /// @brief Constructs by copying an error.
        constexpr explicit Unexpected(const E& error) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowCopyConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsCopyConstructible())
            : m_error {error}
        {
        }

        /// @brief Constructs by moving an error.
        constexpr explicit Unexpected(E&& error) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
            : m_error {std::move(error)}
        {
        }

        /// @brief Access the contained error.
        [[nodiscard]] constexpr E& Error() & noexcept { return m_error; }

        /// @brief Access the contained error (const).
        [[nodiscard]] constexpr const E& Error() const& noexcept { return m_error; }

        /// @brief Move-access the contained error.
        [[nodiscard]] constexpr E&& Error() && noexcept { return std::move(m_error); }

    private:
        E m_error;
    };

    namespace detail
    {
        [[noreturn]] inline void ExpectedFailNoValue() noexcept
        {
            NGIN_ASSERT(false && "NGIN::Utilities::Expected::Value called when holding error");
            NGIN_ABORT("NGIN::Utilities::Expected::Value called when holding error");
        }

        [[noreturn]] inline void ExpectedFailNoError() noexcept
        {
            NGIN_ASSERT(false && "NGIN::Utilities::Expected::Error called when holding value");
            NGIN_ABORT("NGIN::Utilities::Expected::Error called when holding value");
        }

        template <class F>
        constexpr void WithExceptionsAbortOnThrow(F&& f) noexcept
        {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
            try
            {
                std::forward<F>(f)();
            }
            catch (...)
            {
                NGIN_ABORT("NGIN::Utilities::Expected operation threw; policy is abort");
            }
#else
            std::forward<F>(f)();
#endif
        }
    }

    /// @brief Inline "value or error" return type with explicit lifetime and zero allocations.
    ///
    /// @tparam T Value type.
    /// @tparam E Error type.
    template <class T, class E>
    class [[nodiscard]] Expected
    {
        static_assert(!NGIN::Meta::TypeTraits<T>::IsReference(), "Expected<T&,...> is not supported.");
        static_assert(!NGIN::Meta::TypeTraits<E>::IsReference(), "Expected<...,E&> is not supported.");


    public:
        /// @brief Value type.
        using ValueType = T;

        /// @brief Error type.
        using ErrorType = E;

        /// @brief Constructs an `Expected` holding a value (in-place).
        ///
        /// @param args Forwarded constructor arguments for `T`.
        template <class... Args>
        constexpr explicit Expected(InPlaceType<T>, Args&&... args)
            noexcept(NGIN::Meta::TypeTraits<T>::template IsNothrowConstructible<Args...>())
            requires(NGIN::Meta::TypeTraits<T>::template IsConstructible<Args...>())
            : m_storage {}
            , m_hasValue {1}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::forward<Args>(args)...); });
        }

        /// @brief Constructs an `Expected` holding a value by copying `value`.
        constexpr explicit Expected(const T& value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsCopyConstructible())
            : m_storage {}
            , m_hasValue {1}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(value); });
        }

        /// @brief Constructs an `Expected` holding a value by moving `value`.
        constexpr explicit Expected(T&& value) noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
            : m_storage {}
            , m_hasValue {1}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(value)); });
        }

        /// @brief Constructs an `Expected` holding an error from `Unexpected<E>`.
        constexpr explicit Expected(Unexpected<E>&& unexpected) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
            : m_storage {}
            , m_hasValue {0}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(unexpected).Error()); });
        }

        /// @brief Constructs an `Expected` holding an error in-place.
        ///
        /// @param args Forwarded constructor arguments for `E`.
        template <class... Args>
        constexpr explicit Expected(InPlaceType<E>, Args&&... args)
            noexcept(NGIN::Meta::TypeTraits<E>::template IsNothrowConstructible<Args...>())
            requires(
                !NGIN::Meta::TypeTraits<T>::template IsSame<E>() &&
                NGIN::Meta::TypeTraits<E>::template IsConstructible<Args...>())
            : m_storage {}
            , m_hasValue {0}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::forward<Args>(args)...); });
        }

        /// @brief Copy-constructs.
        ///
        /// @note Defaulted when both `T` and `E` are trivially copyable.
        constexpr Expected(const Expected& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Copy-constructs (non-trivial path).
        constexpr Expected(const Expected& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowCopyConstructible())
            requires(
                !(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable()) &&
                NGIN::Meta::TypeTraits<T>::IsCopyConstructible() && NGIN::Meta::TypeTraits<E>::IsCopyConstructible())
            : m_storage {}
            , m_hasValue {0}
        {
            if (other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(other.ValueRef()); });
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(other.ErrorRef()); });
                m_hasValue = 0;
            }
        }

        /// @brief Move-constructs.
        ///
        /// @note Defaulted when both `T` and `E` are trivially copyable.
        ///       (This implies trivial move operations and keeps `Expected` trivially copyable.)
        constexpr Expected(Expected&& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Move-constructs (non-trivial path).
        constexpr Expected(Expected&& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(
                !(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable()) &&
                NGIN::Meta::TypeTraits<T>::IsMoveConstructible() && NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
            : m_storage {}
            , m_hasValue {0}
        {
            if (other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(other.ValueRef())); });
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(other.ErrorRef())); });
                m_hasValue = 0;
            }
        }

        /// @brief Copy-assigns.
        ///
        /// @note Defaulted when both `T` and `E` are trivially copyable.
        constexpr Expected& operator=(const Expected& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Copy-assigns (non-trivial path).
        constexpr Expected& operator=(const Expected& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowCopyConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowCopyConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<T>::IsCopyAssignable() || NGIN::Meta::TypeTraits<T>::IsNothrowCopyAssignable()) &&
                (!NGIN::Meta::TypeTraits<E>::IsCopyAssignable() || NGIN::Meta::TypeTraits<E>::IsNothrowCopyAssignable()))
            requires(
                !(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable()) &&
                NGIN::Meta::TypeTraits<T>::IsCopyConstructible() && NGIN::Meta::TypeTraits<E>::IsCopyConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (m_hasValue && other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<T>::IsCopyAssignable())
                {
                    ValueRef() = other.ValueRef();
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(other.ValueRef()); });
                    m_hasValue = 1;
                }
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<E>::IsCopyAssignable())
                {
                    ErrorRef() = other.ErrorRef();
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(other.ErrorRef()); });
                    m_hasValue = 0;
                }
                return *this;
            }

            // State change
            DestroyActive();
            if (other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(other.ValueRef()); });
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(other.ErrorRef()); });
                m_hasValue = 0;
            }

            return *this;
        }

        /// @brief Move-assigns.
        ///
        /// @note Defaulted when both `T` and `E` are trivially copyable.
        ///       (This implies trivial move operations and keeps `Expected` trivially copyable.)
        constexpr Expected& operator=(Expected&& other) noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Move-assigns (non-trivial path).
        constexpr Expected& operator=(Expected&& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<T>::IsMoveAssignable() || NGIN::Meta::TypeTraits<T>::IsNothrowMoveAssignable()) &&
                (!NGIN::Meta::TypeTraits<E>::IsMoveAssignable() || NGIN::Meta::TypeTraits<E>::IsNothrowMoveAssignable()))
            requires(
                !(NGIN::Meta::TypeTraits<T>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable()) &&
                NGIN::Meta::TypeTraits<T>::IsMoveConstructible() && NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (m_hasValue && other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<T>::IsMoveAssignable())
                {
                    ValueRef() = std::move(other.ValueRef());
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(other.ValueRef())); });
                    m_hasValue = 1;
                }
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<E>::IsMoveAssignable())
                {
                    ErrorRef() = std::move(other.ErrorRef());
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(other.ErrorRef())); });
                    m_hasValue = 0;
                }
                return *this;
            }

            // State change
            DestroyActive();
            if (other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(other.ValueRef())); });
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(other.ErrorRef())); });
                m_hasValue = 0;
            }

            return *this;
        }

        /// @brief Destroys the active member.
        ///
        /// @note Defaulted when both `T` and `E` are trivially destructible.
        constexpr ~Expected() noexcept
            requires(NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible() && NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible())
        = default;

        /// @brief Destroys the active member (non-trivial path).
        constexpr ~Expected() noexcept
            requires(!(NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible() && NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible()))
        {
            DestroyActive();
        }

        /// @brief Returns true if this object currently holds a value.
        [[nodiscard]] constexpr bool HasValue() const noexcept { return m_hasValue != 0; }

        /// @brief Returns true if this object currently holds a value.
        constexpr explicit operator bool() const noexcept { return HasValue(); }

        /// @brief Returns the contained value.
        ///
        /// @details Checked accessor: if holding an error, triggers the contract policy.
        [[nodiscard]] constexpr T& Value() & noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                detail::ExpectedFailNoValue();
            }
            return ValueRef();
        }

        /// @brief Returns the contained value (const).
        ///
        /// @details Checked accessor: if holding an error, triggers the contract policy.
        [[nodiscard]] constexpr const T& Value() const& noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                detail::ExpectedFailNoValue();
            }
            return ValueRef();
        }

        /// @brief Returns the contained value (rvalue).
        ///
        /// @details Checked accessor: if holding an error, triggers the contract policy.
        [[nodiscard]] constexpr T&& Value() && noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                detail::ExpectedFailNoValue();
            }
            return std::move(ValueRef());
        }

        /// @brief Returns the contained value (const rvalue).
        ///
        /// @details Checked accessor: if holding an error, triggers the contract policy.
        [[nodiscard]] constexpr const T&& Value() const&& noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                detail::ExpectedFailNoValue();
            }
            return std::move(ValueRef());
        }

        /// @brief Returns the contained error.
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr E& Error() & noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return ErrorRef();
        }

        /// @brief Returns the contained error (const).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr const E& Error() const& noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return ErrorRef();
        }

        /// @brief Returns the contained error (rvalue).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr E&& Error() && noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return std::move(ErrorRef());
        }

        /// @brief Returns the contained error (const rvalue).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr const E&& Error() const&& noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return std::move(ErrorRef());
        }

        /// @brief Returns the contained value without checking.
        ///
        /// @warning Undefined behavior if holding an error.
        [[nodiscard]] constexpr T& ValueUnsafe() & noexcept { return ValueRef(); }

        /// @brief Returns the contained value without checking (const).
        ///
        /// @warning Undefined behavior if holding an error.
        [[nodiscard]] constexpr const T& ValueUnsafe() const& noexcept { return ValueRef(); }

        /// @brief Returns the contained value without checking (rvalue).
        ///
        /// @warning Undefined behavior if holding an error.
        [[nodiscard]] constexpr T&& ValueUnsafe() && noexcept { return std::move(ValueRef()); }

        /// @brief Returns the contained value without checking (const rvalue).
        ///
        /// @warning Undefined behavior if holding an error.
        [[nodiscard]] constexpr const T&& ValueUnsafe() const&& noexcept { return std::move(ValueRef()); }

        /// @brief Returns the contained error without checking.
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr E& ErrorUnsafe() & noexcept { return ErrorRef(); }

        /// @brief Returns the contained error without checking (const).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr const E& ErrorUnsafe() const& noexcept { return ErrorRef(); }

        /// @brief Returns the contained error without checking (rvalue).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr E&& ErrorUnsafe() && noexcept { return std::move(ErrorRef()); }

        /// @brief Returns the contained error without checking (const rvalue).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr const E&& ErrorUnsafe() const&& noexcept { return std::move(ErrorRef()); }

        /// @brief Returns the contained value if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr const T& ValueOr(const T& fallback) const& noexcept
        {
            return m_hasValue ? ValueUnsafe() : fallback;
        }

        /// @brief Returns the contained value if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr T ValueOr(T fallback) && noexcept(NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<T>::IsMoveConstructible())
        {
            return m_hasValue ? std::move(ValueUnsafe()) : std::move(fallback);
        }

        /// @brief Returns the contained error if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr const E& ErrorOr(const E& fallback) const& noexcept
        {
            return m_hasValue ? fallback : ErrorUnsafe();
        }

        /// @brief Returns the contained error if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr E ErrorOr(E fallback) && noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
        {
            return m_hasValue ? std::move(fallback) : std::move(ErrorUnsafe());
        }

        /// @brief Swaps two `Expected` values.
        constexpr void Swap(Expected& other)
            noexcept(
                NGIN::Meta::TypeTraits<T>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible())
            requires(
                NGIN::Meta::TypeTraits<T>::IsMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsMoveConstructible() &&
                NGIN::Meta::TypeTraits<T>::IsDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsDestructible())
        {
            if (this == &other)
            {
                return;
            }

            if (m_hasValue && other.m_hasValue)
            {
                auto tempValue = std::move(ValueRef());

                m_storage.template Destroy<T>();
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(other.ValueRef())); });

                other.m_storage.template Destroy<T>();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_storage.template Construct<T>(std::move(tempValue)); });
                return;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                auto tempError = std::move(ErrorRef());

                m_storage.template Destroy<E>();
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(other.ErrorRef())); });

                other.m_storage.template Destroy<E>();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_storage.template Construct<E>(std::move(tempError)); });
                return;
            }

            // One holds value and one holds error.
            if (m_hasValue)
            {
                // this: value, other: error
                auto tempValue = std::move(ValueRef());
                DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::move(other.ErrorRef())); });
                m_hasValue = 0;

                other.DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_storage.template Construct<T>(std::move(tempValue)); });
                other.m_hasValue = 1;
            }
            else
            {
                // this: error, other: value
                auto tempError = std::move(ErrorRef());
                DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::move(other.ValueRef())); });
                m_hasValue = 1;

                other.DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_storage.template Construct<E>(std::move(tempError)); });
                other.m_hasValue = 0;
            }
        }

        /// @brief Constructs/replaces the contained value.
        template <class... Args>
        constexpr T& EmplaceValue(Args&&... args)
            noexcept(
                NGIN::Meta::TypeTraits<T>::template IsNothrowConstructible<Args...>() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<T>::template IsConstructible<Args...>())
        {
            DestroyActive();
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<T>(std::forward<Args>(args)...); });
            m_hasValue = 1;
            return ValueRef();
        }

        /// @brief Constructs/replaces the contained error.
        template <class... Args>
        constexpr E& EmplaceError(Args&&... args)
            noexcept(
                NGIN::Meta::TypeTraits<E>::template IsNothrowConstructible<Args...>() &&
                NGIN::Meta::TypeTraits<T>::IsNothrowDestructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<E>::template IsConstructible<Args...>())
        {
            DestroyActive();
            detail::WithExceptionsAbortOnThrow([&]() { m_storage.template Construct<E>(std::forward<Args>(args)...); });
            m_hasValue = 0;
            return ErrorRef();
        }

    private:
        [[nodiscard]] constexpr T& ValueRef() noexcept { return m_storage.template Ref<T>(); }
        [[nodiscard]] constexpr const T& ValueRef() const noexcept { return m_storage.template Ref<T>(); }
        [[nodiscard]] constexpr E& ErrorRef() noexcept { return m_storage.template Ref<E>(); }
        [[nodiscard]] constexpr const E& ErrorRef() const noexcept { return m_storage.template Ref<E>(); }

        constexpr void DestroyActive() noexcept
        {
            if (m_hasValue)
            {
                if constexpr (!NGIN::Meta::TypeTraits<T>::IsTriviallyDestructible())
                {
                    m_storage.template Destroy<T>();
                }
            }
            else
            {
                if constexpr (!NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible())
                {
                    m_storage.template Destroy<E>();
                }
            }
        }

        [[no_unique_address]] NGIN::Memory::UnionStorageFor<T, E> m_storage {};
        NGIN::UInt8 m_hasValue {0};
    };

    /// @brief Specialization for "success or error" without a value payload.
    ///
    /// @details
    /// The value state contains no payload; only the error is stored when `HasValue() == false`.
    template <class E>
    class [[nodiscard]] Expected<void, E>
    {
        static_assert(!NGIN::Meta::TypeTraits<E>::IsReference(), "Expected<void,E&> is not supported.");

    public:
        using ValueType = void;
        using ErrorType = E;

        /// @brief Constructs a success state.
        constexpr Expected() noexcept
            : m_error {}
            , m_hasValue {1}
        {
        }

        /// @brief Constructs an error state from `Unexpected<E>`.
        constexpr explicit Expected(Unexpected<E>&& unexpected) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
            : m_error {}
            , m_hasValue {0}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(unexpected).Error()); });
        }

        /// @brief Constructs an error state in-place.
        template <class... Args>
        constexpr explicit Expected(InPlaceType<E>, Args&&... args)
            noexcept(NGIN::Meta::TypeTraits<E>::template IsNothrowConstructible<Args...>())
            requires(NGIN::Meta::TypeTraits<E>::template IsConstructible<Args...>())
            : m_error {}
            , m_hasValue {0}
        {
            detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::forward<Args>(args)...); });
        }

        /// @brief Copy-constructs.
        constexpr Expected(const Expected& other) noexcept
            requires(NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Copy-constructs (non-trivial path).
        constexpr Expected(const Expected& other) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowCopyConstructible())
            requires(!NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsCopyConstructible())
            : m_error {}
            , m_hasValue {other.m_hasValue}
        {
            if (!other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(other.m_error.Ref()); });
            }
        }

        /// @brief Move-constructs.
        constexpr Expected(Expected&& other) noexcept
            requires(NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Move-constructs (non-trivial path).
        constexpr Expected(Expected&& other) noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(!NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
            : m_error {}
            , m_hasValue {other.m_hasValue}
        {
            if (!other.m_hasValue)
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(other.m_error.Ref())); });
            }
        }

        /// @brief Copy-assigns.
        constexpr Expected& operator=(const Expected& other) noexcept
            requires(NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Copy-assigns (non-trivial path).
        constexpr Expected& operator=(const Expected& other)
            noexcept(
                NGIN::Meta::TypeTraits<E>::IsNothrowCopyConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<E>::IsCopyAssignable() || NGIN::Meta::TypeTraits<E>::IsNothrowCopyAssignable()))
            requires(!NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsCopyConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (m_hasValue && other.m_hasValue)
            {
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<E>::IsCopyAssignable())
                {
                    m_error.Ref() = other.m_error.Ref();
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(other.m_error.Ref()); });
                }
                return *this;
            }

            // State change
            DestroyActive();
            if (other.m_hasValue)
            {
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(other.m_error.Ref()); });
                m_hasValue = 0;
            }

            return *this;
        }

        /// @brief Move-assigns.
        constexpr Expected& operator=(Expected&& other) noexcept
            requires(NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable())
        = default;

        /// @brief Move-assigns (non-trivial path).
        constexpr Expected& operator=(Expected&& other)
            noexcept(
                NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible() &&
                (!NGIN::Meta::TypeTraits<E>::IsMoveAssignable() || NGIN::Meta::TypeTraits<E>::IsNothrowMoveAssignable()))
            requires(!NGIN::Meta::TypeTraits<E>::IsTriviallyCopyable() && NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
        {
            if (this == &other)
            {
                return *this;
            }

            if (m_hasValue && other.m_hasValue)
            {
                return *this;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                if constexpr (NGIN::Meta::TypeTraits<E>::IsMoveAssignable())
                {
                    m_error.Ref() = std::move(other.m_error.Ref());
                }
                else
                {
                    DestroyActive();
                    detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(other.m_error.Ref())); });
                }
                return *this;
            }

            // State change
            DestroyActive();
            if (other.m_hasValue)
            {
                m_hasValue = 1;
            }
            else
            {
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(other.m_error.Ref())); });
                m_hasValue = 0;
            }

            return *this;
        }

        /// @brief Destructor.
        constexpr ~Expected() noexcept
            requires(NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible())
        = default;

        /// @brief Destructor (non-trivial).
        constexpr ~Expected() noexcept
            requires(!NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible())
        {
            DestroyActive();
        }

        /// @brief Returns true if holding a value (success).
        [[nodiscard]] constexpr bool HasValue() const noexcept { return m_hasValue != 0; }

        /// @brief Returns true if holding a value (success).
        constexpr explicit operator bool() const noexcept { return HasValue(); }

        /// @brief Checks that a value is present.
        ///
        /// @details Checked accessor: if holding an error, triggers the contract policy.
        constexpr void Value() const noexcept
        {
            if (NGIN_UNLIKELY(!m_hasValue))
            {
                detail::ExpectedFailNoValue();
            }
        }

        /// @brief Returns the contained error.
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr E& Error() & noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return m_error.Ref();
        }

        /// @brief Returns the contained error (const).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr const E& Error() const& noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return m_error.Ref();
        }

        /// @brief Returns the contained error (rvalue).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr E&& Error() && noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return std::move(m_error.Ref());
        }

        /// @brief Returns the contained error (const rvalue).
        ///
        /// @details Checked accessor: if holding a value, triggers the contract policy.
        [[nodiscard]] constexpr const E&& Error() const&& noexcept
        {
            if (NGIN_UNLIKELY(m_hasValue))
            {
                detail::ExpectedFailNoError();
            }
            return std::move(m_error.Ref());
        }

        /// @brief Returns the contained error without checking.
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr E& ErrorUnsafe() & noexcept { return m_error.Ref(); }

        /// @brief Returns the contained error without checking (const).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr const E& ErrorUnsafe() const& noexcept { return m_error.Ref(); }

        /// @brief Returns the contained error without checking (rvalue).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr E&& ErrorUnsafe() && noexcept { return std::move(m_error.Ref()); }

        /// @brief Returns the contained error without checking (const rvalue).
        ///
        /// @warning Undefined behavior if holding a value.
        [[nodiscard]] constexpr const E&& ErrorUnsafe() const&& noexcept { return std::move(m_error.Ref()); }

        /// @brief Returns the contained error if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr const E& ErrorOr(const E& fallback) const& noexcept
        {
            return m_hasValue ? fallback : ErrorUnsafe();
        }

        /// @brief Returns the contained error if present, otherwise returns `fallback`.
        [[nodiscard]] constexpr E ErrorOr(E fallback) && noexcept(NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
        {
            return m_hasValue ? std::move(fallback) : std::move(ErrorUnsafe());
        }

        /// @brief Swaps two `Expected<void, E>` values.
        constexpr void Swap(Expected& other)
            noexcept(
                NGIN::Meta::TypeTraits<E>::IsNothrowMoveConstructible() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<E>::IsMoveConstructible())
        {
            if (this == &other)
            {
                return;
            }

            if (m_hasValue && other.m_hasValue)
            {
                return;
            }

            if (!m_hasValue && !other.m_hasValue)
            {
                auto tempError = std::move(m_error.Ref());

                m_error.Destroy();
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(other.m_error.Ref())); });

                other.m_error.Destroy();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_error.Construct(std::move(tempError)); });
                return;
            }

            if (!m_hasValue)
            {
                // this: error, other: value
                auto tempError = std::move(m_error.Ref());
                DestroyActive();
                m_hasValue = 1;

                other.DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { other.m_error.Construct(std::move(tempError)); });
                other.m_hasValue = 0;
            }
            else
            {
                // this: value, other: error
                auto tempError = std::move(other.m_error.Ref());
                other.DestroyActive();
                other.m_hasValue = 1;

                DestroyActive();
                detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::move(tempError)); });
                m_hasValue = 0;
            }
        }

        /// @brief Constructs/replaces the value state.
        constexpr void EmplaceValue() noexcept
        {
            DestroyActive();
            m_hasValue = 1;
        }

        /// @brief Constructs/replaces the error state.
        template <class... Args>
        constexpr E& EmplaceError(Args&&... args)
            noexcept(
                NGIN::Meta::TypeTraits<E>::template IsNothrowConstructible<Args...>() &&
                NGIN::Meta::TypeTraits<E>::IsNothrowDestructible())
            requires(NGIN::Meta::TypeTraits<E>::template IsConstructible<Args...>())
        {
            DestroyActive();
            detail::WithExceptionsAbortOnThrow([&]() { m_error.Construct(std::forward<Args>(args)...); });
            m_hasValue = 0;
            return m_error.Ref();
        }

    private:
        constexpr void DestroyActive() noexcept
        {
            if (!m_hasValue)
            {
                if constexpr (!NGIN::Meta::TypeTraits<E>::IsTriviallyDestructible())
                {
                    m_error.Destroy();
                }
            }
        }

        [[no_unique_address]] NGIN::Memory::StorageFor<E> m_error {};
        NGIN::UInt8 m_hasValue {1};
    };
 }
