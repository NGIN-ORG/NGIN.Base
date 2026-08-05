/// @file AsyncDomainErrorException.hpp
/// @brief Exception wrapper for typed asynchronous domain errors.
#pragma once

#include <NGIN/Async/AsyncConfig.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#if NGIN_ASYNC_HAS_EXCEPTIONS
namespace NGIN::Async
{
    /// @brief Carries a typed domain error through an exception-based asynchronous API.
    /// @tparam E Domain error type owned by the exception.
    template<typename E>
    class AsyncDomainErrorException : public std::exception
    {
    public:
        /// @brief Constructs an exception that owns the supplied domain error.
        /// @param error Domain error transferred into the exception.
        explicit AsyncDomainErrorException(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
            : m_error(std::move(error))
        {
        }

        /// @brief Returns the typed domain error carried by this exception.
        [[nodiscard]] const E& Error() const noexcept
        {
            return m_error;
        }

        /// @brief Returns a stable domain-error diagnostic.
        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task domain error";
        }

    private:
        E m_error;
    };
}// namespace NGIN::Async
#endif
