/// @file AsyncError.hpp
/// @brief Error codes and expected type for async operations.
#pragma once

#include <expected>

#include <NGIN/Primitives.hpp>

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
    /// @brief Async error codes for coroutine operations.
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

    /// @brief Async error value with optional native error code.
    struct AsyncError final
    {
        AsyncErrorCode code {AsyncErrorCode::Ok};
        int            native {0};

        [[nodiscard]] constexpr bool IsOk() const noexcept { return code == AsyncErrorCode::Ok; }
    };

    template<typename T>
    using AsyncExpected = std::expected<T, AsyncError>;

    [[nodiscard]] constexpr AsyncError MakeAsyncError(AsyncErrorCode code, int native = 0) noexcept
    {
        return AsyncError {code, native};
    }
}// namespace NGIN::Async
