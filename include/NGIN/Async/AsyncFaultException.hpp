/// @file AsyncFaultException.hpp
/// @brief Exception wrapper for asynchronous runtime faults.
#pragma once

#include <NGIN/Async/AsyncFault.hpp>

#include <exception>
#include <utility>

#if NGIN_ASYNC_HAS_EXCEPTIONS
namespace NGIN::Async
{
    /// @brief Carries an AsyncFault through an exception-based asynchronous API.
    class AsyncFaultException : public std::exception
    {
    public:
        /// @brief Constructs an exception that owns the supplied runtime fault.
        /// @param fault Runtime fault transferred into the exception.
        explicit AsyncFaultException(AsyncFault fault) noexcept
            : m_fault(std::move(fault))
        {
        }

        /// @brief Returns the runtime fault carried by this exception.
        [[nodiscard]] const AsyncFault& Fault() const noexcept
        {
            return m_fault;
        }

        /// @brief Returns a stable runtime-fault diagnostic.
        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task fault";
        }

    private:
        AsyncFault m_fault;
    };
}// namespace NGIN::Async
#endif
