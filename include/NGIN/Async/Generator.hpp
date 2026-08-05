/// @file Generator.hpp
/// @brief Synchronous pull generator (stackless coroutine) based on `co_yield`.
#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

#include <NGIN/Async/AsyncConfig.hpp>
#include <NGIN/Meta/TypeTraits.hpp>

namespace NGIN::Async
{
    /// @brief A synchronous pull generator that yields values via `co_yield`.
    ///
    /// This is intentionally distinct from `Task<T>` (single-result). Use `Generator<T>` when you want a sequence of
    /// values produced lazily by a coroutine.
    template<typename T>
    class Generator final
    {
    public:
        struct promise_type final
        {
            std::optional<T>   current {};
            std::exception_ptr error {};

            /// @brief Returns the generator that owns this coroutine frame.
            Generator get_return_object() noexcept
            {
                return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            /// @brief Suspends before the first value so iteration controls execution.
            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            /// @brief Suspends after completion so the generator can destroy the frame.
            std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            /// @brief Stores a yielded value and suspends the coroutine.
            std::suspend_always yield_value(T value) noexcept(Meta::TypeTraits<T>::IsNothrowMoveConstructible())
            {
                current = std::move(value);
                return {};
            }

            /// @brief Completes a generator that reaches `co_return`.
            void return_void() noexcept {}

            /// @brief Captures an escaping exception, or terminates when exceptions are disabled.
            void unhandled_exception() noexcept
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                error = std::current_exception();
#else
                std::terminate();
#endif
            }
        };

        /// @brief Coroutine handle type owned by the generator.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty generator.
        Generator() noexcept = default;

        /// @brief Takes ownership of a generator coroutine handle.
        explicit Generator(handle_type handle) noexcept
            : m_handle(handle)
        {
        }

        /// @brief Transfers ownership of a coroutine frame.
        Generator(Generator&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.m_handle = {};
        }

        /// @brief Destroys the current frame and transfers ownership of another frame.
        Generator& operator=(Generator&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_handle       = other.m_handle;
                other.m_handle = {};
            }
            return *this;
        }

        /// @brief Generators are non-copyable because they uniquely own a coroutine frame.
        Generator(const Generator&) = delete;
        /// @brief Generators are non-copy-assignable because they uniquely own a coroutine frame.
        Generator& operator=(const Generator&) = delete;

        /// @brief Destroys the owned coroutine frame.
        ~Generator()
        {
            Reset();
        }

        /// @brief Single-pass iterator that resumes the generator on increment.
        class Iterator final
        {
        public:
            using value_type = T;
            using reference  = const T&;

            /// @brief Constructs an end iterator.
            Iterator() noexcept = default;

            /// @brief Constructs an iterator over a generator coroutine.
            explicit Iterator(handle_type handle) noexcept
                : m_handle(handle)
            {
            }

            /// @brief Returns the current yielded value and rethrows a captured exception.
            reference operator*() const
            {
                promise_type& promise = m_handle.promise();
                if (promise.error)
                {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    std::rethrow_exception(promise.error);
#else
                    std::terminate();
#endif
                }
                return *promise.current;
            }

            /// @brief Resumes the generator until its next yield or completion.
            Iterator& operator++()
            {
                Resume();
                return *this;
            }

            /// @brief Returns whether iteration has reached completion.
            friend bool operator==(const Iterator& it, std::default_sentinel_t) noexcept
            {
                return !it.m_handle || it.m_handle.done();
            }

        private:
            void Resume()
            {
                if (!m_handle || m_handle.done())
                {
                    return;
                }

                m_handle.resume();
                promise_type& promise = m_handle.promise();
                if (promise.error)
                {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                    std::rethrow_exception(promise.error);
#else
                    std::terminate();
#endif
                }
            }

            handle_type m_handle {};
        };

        /// @brief Starts or resumes generation and returns the first iterator.
        /// @details Exceptions raised before the first yield are rethrown here when enabled.
        [[nodiscard]] Iterator begin()
        {
            if (!m_handle)
            {
                return Iterator {};
            }

            if (!m_handle.done())
            {
                m_handle.resume();
            }

            promise_type& promise = m_handle.promise();
            if (promise.error)
            {
#if NGIN_ASYNC_HAS_EXCEPTIONS
                std::rethrow_exception(promise.error);
#else
                std::terminate();
#endif
            }

            if (m_handle.done())
            {
                return Iterator {};
            }

            return Iterator {m_handle};
        }

        /// @brief Returns the default end sentinel.
        [[nodiscard]] std::default_sentinel_t end() const noexcept
        {
            return {};
        }

    private:
        void Reset() noexcept
        {
            if (m_handle)
            {
                m_handle.destroy();
                m_handle = {};
            }
        }

        handle_type m_handle {};
    };
}// namespace NGIN::Async
