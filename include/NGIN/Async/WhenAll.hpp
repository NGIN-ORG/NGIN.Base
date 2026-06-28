/// @file WhenAll.hpp
/// @brief Task combinator that completes when all owned child tasks complete.
#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Task.hpp>

namespace NGIN::Async
{
    template<typename... TTasks>
        requires(sizeof...(TTasks) > 0) && (detail::IsTaskTypeV<TTasks> && ...) &&
                (std::is_same_v<typename TTasks::ErrorType, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType> &&
                 ...) &&
                (std::is_void_v<typename TTasks::ValueType> && ...)
    [[nodiscard]] inline Task<void, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType> WhenAll(TaskContext& ctx, TTasks... tasks)
    {
        if (ctx.IsCancellationRequested())
        {
            co_await Canceled();
            co_return;
        }

        (co_await std::move(tasks), ...);
        co_return;
    }

    template<typename E, typename... T>
        requires(sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
    [[nodiscard]] inline Task<std::tuple<T...>, E> WhenAll(TaskContext& ctx, Task<T, E>... tasks)
    {
        using OutCompletion = Completion<std::tuple<T...>, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        co_return std::tuple<T...> {(co_await std::move(tasks))...};
    }
}// namespace NGIN::Async
