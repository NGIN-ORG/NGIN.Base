/// @file AsyncCanceledException.hpp
/// @brief Exception used to surface asynchronous cancellation through exception-based adapters.
#pragma once

#include <NGIN/Async/AsyncConfig.hpp>

#include <exception>

#if NGIN_ASYNC_HAS_EXCEPTIONS
namespace NGIN::Async
{
    /// @brief Indicates that an awaited asynchronous operation was canceled.
    class AsyncCanceledException : public std::exception
    {
    public:
        /// @brief Returns a stable cancellation diagnostic.
        [[nodiscard]] const char* what() const noexcept override
        {
            return "async task canceled";
        }
    };
}// namespace NGIN::Async
#endif
