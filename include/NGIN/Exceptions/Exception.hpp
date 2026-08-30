#pragma once

#include <exception>
#include <stacktrace>
#include <stdexcept>

#include <NGIN/Text/String.hpp>

namespace NGIN::Exceptions
{
#ifndef NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE
#define NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE 0
#endif

    /// @class Exception
    /// @brief Base class for all exceptions in NGIN.
    ///
    /// @details
    /// `Exception` is the base class for all exceptions in NGIN. It provides a common interface
    /// for exception handling and allows retrieval of the exception message and construction-site
    /// stack trace. Stack-trace capture is configurable because it can allocate and materially
    /// increase exception-construction cost.
    class Exception : public std::runtime_error
    {
    public:
        /// @brief Constructs an exception with an empty message.
        Exception()
            : std::runtime_error(""), m_stacktrace(CaptureStacktrace())
        {
        }

        /// @brief Constructor.
        explicit Exception(const char* message)
            : std::runtime_error(message ? message : ""), m_stacktrace(CaptureStacktrace())
        {
        }

        /// @brief Constructs an exception by copying an NGIN string message.
        explicit Exception(const NGIN::Text::String& message)
            : std::runtime_error(message.CStr()), m_stacktrace(CaptureStacktrace())
        {
        }

        /// @brief Destructor.
        virtual ~Exception() noexcept = default;

        /// @brief Returns the exception message.
        /// @return A string containing the exception message.
        [[nodiscard]] const char* GetMessage() const noexcept { return this->what(); }

        /// @brief Returns the stacktrace of the exception.
        /// @details Returns the construction-site trace when
        ///          `NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE` is enabled, otherwise an empty trace.
        /// @return A `std::stacktrace` reference containing the stacktrace of the exception.
        [[nodiscard]] const std::stacktrace& GetStacktrace() const noexcept { return m_stacktrace; }

    private:
        [[nodiscard]] static std::stacktrace CaptureStacktrace()
        {
#if NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE
            return std::stacktrace::current();
#else
            return {};
#endif
        }

        std::stacktrace m_stacktrace;///< Eager construction-site trace, or empty when capture is disabled.
    };
}// namespace NGIN::Exceptions
