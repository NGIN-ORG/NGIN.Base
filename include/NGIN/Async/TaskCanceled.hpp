/// @file TaskCanceled.hpp
/// @brief Legacy exception used to report task cancellation.
#pragma once

#include <exception>

namespace NGIN::Async
{
    /// @brief Indicates that an asynchronous task observed cancellation.
    class TaskCanceled : public std::exception
    {
    public:
        /// @brief Returns a stable cancellation diagnostic.
        [[nodiscard]] const char* what() const noexcept override
        {
            return "Task was canceled";
        }
    };
}// namespace NGIN::Async
