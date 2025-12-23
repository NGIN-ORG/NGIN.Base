/// @file Generator.hpp
/// @brief Synchronous pull generator (stackless coroutine) based on `co_yield`.
#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

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
            std::optional<T>  current {};
            std::exception_ptr error {};

            Generator get_return_object() noexcept
            {
                return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            std::suspend_always yield_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
            {
                current = std::move(value);
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
                error = std::current_exception();
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        Generator() noexcept = default;

        explicit Generator(handle_type handle) noexcept
            : m_handle(handle)
        {
        }

        Generator(Generator&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.m_handle = {};
        }

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

        Generator(const Generator&)            = delete;
        Generator& operator=(const Generator&) = delete;

        ~Generator()
        {
            Reset();
        }

        class Iterator final
        {
        public:
            using value_type = T;
            using reference  = const T&;

            Iterator() noexcept = default;

            explicit Iterator(handle_type handle) noexcept
                : m_handle(handle)
            {
            }

            reference operator*() const
            {
                auto& promise = m_handle.promise();
                if (promise.error)
                {
                    std::rethrow_exception(promise.error);
                }
                return *promise.current;
            }

            Iterator& operator++()
            {
                Resume();
                return *this;
            }

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
                auto& promise = m_handle.promise();
                if (promise.error)
                {
                    std::rethrow_exception(promise.error);
                }
            }

            handle_type m_handle {};
        };

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

            auto& promise = m_handle.promise();
            if (promise.error)
            {
                std::rethrow_exception(promise.error);
            }

            if (m_handle.done())
            {
                return Iterator {};
            }

            return Iterator {m_handle};
        }

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
