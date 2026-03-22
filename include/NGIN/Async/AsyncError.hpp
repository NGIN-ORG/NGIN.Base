/// @file AsyncError.hpp
/// @brief Fault and control-flow support types for async operations.
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

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
        AsyncFaultCode code {AsyncFaultCode::None};
        int            native {0};

        [[nodiscard]] constexpr bool IsOk() const noexcept
        {
            return code == AsyncFaultCode::None;
        }
    };

    [[nodiscard]] constexpr AsyncFault MakeAsyncFault(AsyncFaultCode code, int native = 0) noexcept
    {
        return AsyncFault {code, native};
    }

    // Legacy compatibility surface used by unmigrated internal helpers while the async stack is being retargeted.
    enum class AsyncErrorCode : NGIN::UInt8
    {
        Ok,
        Canceled,
        Fault,
        TimedOut,
        InvalidState,
        InvalidArgument,
        Unknown,
    };

    struct AsyncError final
    {
        AsyncErrorCode code {AsyncErrorCode::Ok};
        int            native {0};

        [[nodiscard]] constexpr bool IsOk() const noexcept
        {
            return code == AsyncErrorCode::Ok;
        }
    };

    template<typename T>
    using AsyncExpected = NGIN::Utilities::Expected<T, AsyncError>;

    [[nodiscard]] constexpr AsyncError MakeAsyncError(AsyncErrorCode code, int native = 0) noexcept
    {
        return AsyncError {code, native};
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
