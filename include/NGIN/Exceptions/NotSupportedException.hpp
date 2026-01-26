#pragma once

/// @file NotSupportedException.hpp
/// @brief Declares the NotSupportedException class.

#include <NGIN/Exceptions/Exception.hpp>

namespace NGIN::Exceptions
{
    /// @class NotSupportedException
    /// @brief Exception thrown when an unsupported feature or operation is attempted.
    ///
    /// @details
    /// `NotSupportedException` is used to indicate that the code attempted an operation
    /// that is not implemented or not allowed in the current context.
    class NotSupportedException : public Exception
    {
    public:
        /// @brief Default constructor.
        NotSupportedException() noexcept = default;

        /// @brief Constructor with a C-style string message.
        /// @param message The exception message.
        NotSupportedException(const char* message) noexcept
            : Exception(message)
        {
        }

        /// @brief Constructor with a String message.
        /// @param message The exception message.
        NotSupportedException(const NGIN::Text::String& message) noexcept
            : Exception(message)
        {
        }

        /// @brief Constructor with an rvalue-ref String message.
        /// @param message The exception message.
        NotSupportedException(NGIN::Text::String&& message) noexcept
            : Exception(std::move(message))
        {
        }

        /// @brief Copy constructor.
        /// @param other Another NotSupportedException to copy from.
        NotSupportedException(const NotSupportedException& other) noexcept = default;

        /// @brief Move constructor.
        /// @param other Another NotSupportedException to move from.
        NotSupportedException(NotSupportedException&& other) noexcept = default;

        /// @brief Copy assignment operator.
        /// @param other Another NotSupportedException to copy assign from.
        /// @return A reference to this.
        NotSupportedException& operator=(const NotSupportedException& other) noexcept = default;

        /// @brief Move assignment operator.
        /// @param other Another NotSupportedException to move assign from.
        /// @return A reference to this.
        NotSupportedException& operator=(NotSupportedException&& other) noexcept = default;

        /// @brief Destructor.
        virtual ~NotSupportedException() noexcept = default;
    };
}// namespace NGIN::Exceptions
