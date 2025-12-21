#pragma once

#include <stacktrace>
#include <optional>
#include <exception>

#include <NGIN/Containers/String.hpp>

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
        /// @brief Constructor.
        Exception(const char* message) noexcept : std::runtime_error(message) {}

        /// @brief Destructor.
        virtual ~Exception() noexcept = default;

        /// @brief Returns the exception message.
        /// @return A string containing the exception message.
        const char* GetMessage() const noexcept { return this->what(); };

        /// @brief Returns the stacktrace of the exception.
        /// @details The stacktrace is lazily initialized and only computed when this method is called.
        /// @return A `std::stacktrace` reference containing the stacktrace of the exception.
        inline const std::stacktrace& GetStacktrace() const
        {
            if (!stacktrace.has_value())
                stacktrace = std::stacktrace::current();
            return stacktrace.value();
        }

    private:
        mutable std::optional<std::stacktrace> stacktrace;///< The stacktrace of the exception.
    };
}// namespace NGIN::Exceptions
