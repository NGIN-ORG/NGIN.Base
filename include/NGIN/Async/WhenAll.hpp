/// @file WhenAll.hpp
/// @brief Task combinator that completes when all owned child tasks complete.
#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Task.hpp>

namespace NGIN::Async
{
    namespace detail::when_all
    {
        template<typename T, typename E>
        inline void CaptureFailure(Completion<T, E> completion, std::optional<Completion<void, E>>& failure)
        {
            if (completion.Succeeded() || failure)
            {
                return;
            }
            if (completion.IsDomainError())
            {
                failure.emplace(Completion<void, E>::DomainFailure(std::move(completion).DomainError()));
            }
            else if (completion.IsCanceled())
            {
                failure.emplace(Completion<void, E>::Canceled());
            }
            else
            {
                failure.emplace(Completion<void, E>::Faulted(std::move(completion).Fault()));
            }
        }

        template<std::size_t Index, typename E, typename... TOperations>
        inline Task<void, E> AwaitVoidOperations(
                TaskContext&                        ctx,
                std::tuple<TOperations...>&         operations,
                std::optional<Completion<void, E>>& failure)
        {
            if constexpr (Index < sizeof...(TOperations))
            {
                auto completion = co_await std::get<Index>(operations);
                CaptureFailure(std::move(completion), failure);
                co_await AwaitVoidOperations<Index + 1, E>(ctx, operations, failure);
            }
            co_return;
        }

        template<std::size_t Index, typename E, typename Operations, typename Values>
        inline Task<void, E> AwaitValueOperations(
                TaskContext&                        ctx,
                Operations&                         operations,
                Values&                             values,
                std::optional<Completion<void, E>>& failure)
        {
            if constexpr (Index < std::tuple_size_v<Operations>)
            {
                auto completion = co_await std::get<Index>(operations);
                if (completion.Succeeded())
                {
                    std::get<Index>(values).emplace(std::move(completion).Value());
                }
                else
                {
                    CaptureFailure(std::move(completion), failure);
                }
                co_await AwaitValueOperations<Index + 1, E>(ctx, operations, values, failure);
            }
            co_return;
        }

        template<typename Out, typename E>
        [[nodiscard]] inline Completion<Out, E> ConvertFailure(Completion<void, E> failure)
        {
            if (failure.IsDomainError())
            {
                return Completion<Out, E>::DomainFailure(std::move(failure).DomainError());
            }
            if (failure.IsCanceled())
            {
                return Completion<Out, E>::Canceled();
            }
            return Completion<Out, E>::Faulted(std::move(failure).Fault());
        }

        template<typename... T, std::size_t... Indices>
        [[nodiscard]] inline std::tuple<T...> TakeValues(
                std::tuple<std::optional<T>...>& values,
                std::index_sequence<Indices...>)
        {
            return std::tuple<T...> {std::move(*std::get<Indices>(values))...};
        }
    }// namespace detail::when_all

    /// @brief Awaits a non-empty set of `Task<void, E>` operations and propagates the first failure.
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

        auto                                                                                                operations = std::tuple {Spawn(ctx, std::move(tasks))...};
        std::optional<Completion<void, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType>> failure;
        co_await detail::when_all::AwaitVoidOperations<0, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType>(
                ctx, operations, failure);
        if (failure)
        {
            if (failure->IsDomainError())
            {
                co_await DomainFailure(std::move(*failure).DomainError());
            }
            else if (failure->IsCanceled())
            {
                co_await Canceled();
            }
            else
            {
                co_await Faulted(std::move(*failure).Fault());
            }
        }
        co_return;
    }

    /// @brief Awaits non-void tasks with one error type and returns their values in argument order.
    template<typename E, typename... T>
        requires(sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
    [[nodiscard]] inline Task<std::tuple<T...>, E> WhenAll(TaskContext& ctx, Task<T, E>... tasks)
    {
        using OutCompletion = Completion<std::tuple<T...>, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        auto                               operations = std::tuple {Spawn(ctx, std::move(tasks))...};
        std::tuple<std::optional<T>...>    values;
        std::optional<Completion<void, E>> failure;
        co_await detail::when_all::AwaitValueOperations<0, E>(ctx, operations, values, failure);
        if (failure)
        {
            co_return detail::when_all::ConvertFailure<std::tuple<T...>>(std::move(*failure));
        }
        co_return detail::when_all::TakeValues<T...>(values, std::index_sequence_for<T...> {});
    }
}// namespace NGIN::Async
