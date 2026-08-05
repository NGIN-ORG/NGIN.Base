#pragma once

#include <exception>
#include <optional>
#include <stacktrace>
#include <stdexcept>

#include <NGIN/Text/String.hpp>

namespace NGIN::Exceptions
{
    /// @class Exception
    /// @brief Base class for all exceptions in NGIN.
    ///
    /// @details
    /// `Exception` is the base class for all exceptions in NGIN. It provides a common interface
    /// for exception handling and allows for retriaval of the exception message and stacktrace.
    /// The stacktrace is lazily initialized and only computed when `GetStacktrace` is called.
    class Exception : public std::runtime_error
    {
    public:
        /// @brief Constructs an exception with an empty message.
        Exception()
            : std::runtime_error("")
        {
        }

        /// @brief Constructor.
        explicit Exception(const char* message)
            : std::runtime_error(message ? message : "")
        {
        }

        /// @brief Constructs an exception by copying an NGIN string message.
        explicit Exception(const NGIN::Text::String& message)
            : std::runtime_error(message.CStr())
        {
        }

        /// @brief Destructor.
        virtual ~Exception() noexcept = default;

        /// @brief Returns the exception message.
        /// @return A string containing the exception message.
        [[nodiscard]] const char* GetMessage() const noexcept { return this->what(); }

        /// @brief Returns the stacktrace of the exception.
        /// @details The stacktrace is lazily initialized and only computed when this method is called.
        /// @return A `std::stacktrace` reference containing the stacktrace of the exception.
        [[nodiscard]] const std::stacktrace& GetStacktrace() const
        {
            if (!stacktrace.has_value())
                stacktrace = std::stacktrace::current();
            return stacktrace.value();
        }

    private:
        mutable std::optional<std::stacktrace> stacktrace;///< The stacktrace of the exception.
    };
}// namespace NGIN::Exceptions
