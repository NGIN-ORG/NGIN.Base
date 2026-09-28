/// @file FirstCompleted.hpp
/// @brief Non-consuming observation of the first published operation completion.
#pragma once

#include <NGIN/Async/OperationRef.hpp>
#include <NGIN/Async/Task.hpp>
#include <span>

namespace NGIN::Async
{
    struct CompletionNotification final
    {
        std::size_t    index {};
        CompletionKind kind {CompletionKind::Value};
    };

    /// Returns the first terminal notification, without taking any result or
    /// stopping any operation. Already-completed inputs use input order. The token
    /// cancels only this wait; all registrations retire before the task returns.
    /// Empty/invalid inputs fail with InvalidTaskUsage. Keep the input span alive
    /// until this cold task starts; pending observation then retains its own refs.
    [[nodiscard]] NGIN_EXECUTION_API Task<CompletionNotification> FirstCompleted(
            std::span<const OperationRef> operations, CancellationToken token = {});
}// namespace NGIN::Async
