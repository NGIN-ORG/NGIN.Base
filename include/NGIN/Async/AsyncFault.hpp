/// @file AsyncFault.hpp
/// @brief Runtime and infrastructure failure information for asynchronous operations.
#pragma once

#include <NGIN/Async/AsyncConfig.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Error.hpp>

#include <exception>
#include <string>
#include <string_view>

namespace NGIN::Async
{
    /// @brief Categorizes runtime failures that are outside a task's typed error domain.
    enum class AsyncFaultCode : NGIN::UInt16
    {
        None = 0,                  ///< No runtime failure occurred.
        InvalidTaskUsage,          ///< A task or operation was consumed in an invalid state.
        InvalidContinuationState,  ///< A coroutine continuation violated task ownership rules.
        SchedulerDispatchFailed,   ///< The scheduler could not dispatch task work.
        ContinuationDispatchFailed,///< A completed task could not dispatch its continuation.
        RuntimeInvariantViolation, ///< An internal asynchronous runtime invariant was violated.
        UnhandledException,        ///< User code allowed an exception to escape a coroutine.
        UnknownRuntimeFailure,     ///< A runtime failure could not be classified more precisely.
    };

    /// @brief Describes an asynchronous runtime failure independently of domain errors and cancellation.
    struct AsyncFault final
    {
        AsyncFaultCode code {AsyncFaultCode::None};///< Portable runtime failure category.
        int            native {0};                 ///< Optional platform- or provider-specific error code.
        std::string    message {};                 ///< Optional human-readable diagnostic message.
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        std::exception_ptr capturedException {};///< Original exception when exception capture is enabled.
#endif

        /// @brief Constructs an empty fault.
        AsyncFault() noexcept = default;

        /// @brief Constructs a categorized asynchronous fault.
        /// @param faultCode Portable fault category.
        /// @param nativeCode Optional platform- or provider-specific code.
        /// @param faultMessage Optional diagnostic message copied into the fault.
        explicit AsyncFault(AsyncFaultCode faultCode, int nativeCode = 0, std::string_view faultMessage = {})
            : code(faultCode), native(nativeCode), message(faultMessage)
        {
        }

        /// @brief Returns whether this value represents no runtime failure.
        [[nodiscard]] constexpr bool IsOk() const noexcept
        {
            return code == AsyncFaultCode::None;
        }

        /// @brief Converts this fault to the common error-domain representation.
        [[nodiscard]] constexpr NGIN::Utilities::ErrorInfo ToErrorInfo() const noexcept
        {
            return {NGIN::Utilities::ErrorDomain::Async, code, native};
        }
    };

    /// @brief Creates an asynchronous runtime fault from its portable and optional native details.
    /// @param code Portable fault category.
    /// @param native Optional platform- or provider-specific code.
    /// @param message Optional diagnostic message copied into the fault.
    [[nodiscard]] inline AsyncFault MakeAsyncFault(
            AsyncFaultCode   code,
            int              native  = 0,
            std::string_view message = {})
    {
        return AsyncFault {code, native, message};
    }
}// namespace NGIN::Async
