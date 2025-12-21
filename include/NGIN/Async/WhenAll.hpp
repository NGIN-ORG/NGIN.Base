/// @file WhenAll.hpp
/// @brief Task combinator that completes when all tasks complete.
#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Task.hpp>

namespace NGIN::Async
{
    /// @brief Await multiple `Task<void>` and complete when all are finished.
    template<typename... TTasks>
        requires(sizeof...(TTasks) > 0) && (std::is_same_v<std::remove_reference_t<TTasks>, Task<void>> && ...)
    [[nodiscard]] inline Task<void> WhenAll(TaskContext& ctx, TTasks&... tasks)
    {
        ctx.ThrowIfCancellationRequested();
        (tasks.Start(ctx), ...);
        (co_await tasks, ...);
        co_return;
    }

    /// @brief Await multiple `Task<T>` and complete when all are finished, returning a tuple of results.
    template<typename... T>
        requires(sizeof...(T) > 0) && ((!std::is_void_v<T>) && ...)
    [[nodiscard]] inline Task<std::tuple<T...>> WhenAll(TaskContext& ctx, Task<T>&... tasks)
    {
        ctx.ThrowIfCancellationRequested();
        (tasks.Start(ctx), ...);
        co_return std::tuple<T...> {co_await tasks...};
    }
}// namespace NGIN::Async

