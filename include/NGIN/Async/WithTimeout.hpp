/// @file WithTimeout.hpp
/// @brief Converts a relative timeout once when the owned deadline boundary starts.
#pragma once
#include <NGIN/Async/WithDeadline.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    namespace detail
    {
        template<typename Duration, typename T, typename E>
        Task<T, E> RunTimeout(Duration duration, Task<T, E> task)
        {
            auto                            deadline = DeadlineAfter(NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue());
            std::optional<Completion<T, E>> result;
            if (!deadline)
                result.emplace(Completion<T, E>::Faulted(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, deadline.error())));
            else
                result.emplace(co_await WithDeadline(*deadline, std::move(task)).AsCompletion());
            if constexpr (std::is_void_v<T>)
                co_await StoreVoidCompletion<E> {std::move(*result)};
            else
                co_return std::move(*result);
        }
    }// namespace detail
    /// The monotonic deadline is computed when execution starts, not when this
    /// cold task is constructed. Zero/negative duration requests immediate stop;
    /// non-finite duration fails admission. Cleanup can outlast the timeout.
    template<typename Duration, typename T, typename E>
    [[nodiscard]] Task<T, E> WithTimeout(Duration duration, Task<T, E> task)
    {
        if (task.IsCompleted())
            return std::move(task);
        return detail::RunTimeout(duration, std::move(task));
    }
}// namespace NGIN::Async
