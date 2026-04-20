/// @file AsyncError.hpp
/// @brief Fault and control-flow support types for async operations.
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Error.hpp>

#include <exception>
#include <string_view>

#ifndef NGIN_ASYNC_CAPTURE_EXCEPTIONS
#define NGIN_ASYNC_CAPTURE_EXCEPTIONS 1
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define NGIN_ASYNC_HAS_EXCEPTIONS 1
#else
#define NGIN_ASYNC_HAS_EXCEPTIONS 0
#endif

#if !NGIN_ASYNC_HAS_EXCEPTIONS
#undef NGIN_ASYNC_CAPTURE_EXCEPTIONS
#define NGIN_ASYNC_CAPTURE_EXCEPTIONS 0
#endif

namespace NGIN::Async
{
    struct NoError final
    {
    };

    enum class AsyncFaultCode : NGIN::UInt8
    {
        None,
        InvalidState,
        InvalidArgument,
        SchedulerFailure,
        ContinuationDispatchFailure,
        UnhandledException,
        Unknown,
    };

    struct AsyncFault final
    {
        AsyncFaultCode          code {AsyncFaultCode::None};
        int                     native {0};
        std::string_view        message {};
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
        std::exception_ptr      capturedException {};
#endif

        constexpr AsyncFault() noexcept = default;

        constexpr explicit AsyncFault(AsyncFaultCode faultCode, int nativeCode = 0, std::string_view faultMessage = {}) noexcept
            : code(faultCode)
            , native(nativeCode)
            , message(faultMessage)
        {
        }

        [[nodiscard]] constexpr bool IsOk() const noexcept
        {
            return code == AsyncFaultCode::None;
        }

        [[nodiscard]] constexpr NGIN::Utilities::ErrorInfo ToErrorInfo() const noexcept
        {
            return {NGIN::Utilities::ErrorDomain::Async, code, native};
        }
    };

    [[nodiscard]] constexpr AsyncFault MakeAsyncFault(
            AsyncFaultCode code, int native = 0, std::string_view message = {}) noexcept
    {
        return AsyncFault {code, native, message};
    }

    struct CanceledTag final
    {
    };

    namespace Sentinels
    {
        inline constexpr CanceledTag Canceled {};
    }

    struct FaultResult final
    {
        AsyncFault fault {};
    };

    [[nodiscard]] constexpr FaultResult Fault(AsyncFault fault) noexcept
    {
        return FaultResult {fault};
    }

    [[nodiscard]] constexpr FaultResult Fault(AsyncFaultCode code, int native = 0) noexcept
    {
        return FaultResult {MakeAsyncFault(code, native)};
    }
}// namespace NGIN::Async
