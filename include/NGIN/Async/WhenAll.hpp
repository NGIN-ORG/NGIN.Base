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
        template<typename E, typename... Tasks, std::size_t... Indices>
        inline Task<void, E> RunVoid(TaskContext& ctx, std::tuple<Tasks...> tasks, std::index_sequence<Indices...>)
        {
            if (ctx.IsCancellationRequested())
            {
                co_await Canceled();
                co_return;
            }
            auto                               operations = std::tuple {Spawn(ctx, std::move(std::get<Indices>(tasks)))...};
            std::optional<Completion<void, E>> failure;
            // Each await uses this task's existing reservation. No new helper
            // task is admitted between joins, including during shutdown.
            (CaptureFailure(co_await std::get<Indices>(operations), failure), ...);
            if (failure)
            {
                if (failure->IsDomainError())
                    co_await DomainFailure(std::move(*failure).DomainError());
                else if (failure->IsCanceled())
                    co_await Canceled();
                else
                    co_await Faulted(std::move(*failure).Fault());
            }
            co_return;
        }

        template<std::size_t Index, typename T, typename E, typename Values>
        void CaptureValue(Completion<T, E> result, Values& values, std::optional<Completion<void, E>>& failure)
        {
            if (result.Succeeded())
                std::get<Index>(values).emplace(std::move(result).Value());
            else
                CaptureFailure(std::move(result), failure);
        }

        template<typename E, typename... T, std::size_t... Indices>
        inline Task<std::tuple<T...>, E> RunValues(TaskContext& ctx, std::tuple<Task<T, E>...> tasks,
                                                   std::index_sequence<Indices...>)
        {
            if (ctx.IsCancellationRequested())
                co_return Completion<std::tuple<T...>, E>::Canceled();
            auto                               operations = std::tuple {Spawn(ctx, std::move(std::get<Indices>(tasks)))...};
            std::tuple<std::optional<T>...>    values;
            std::optional<Completion<void, E>> failure;
            (CaptureValue<Indices>(co_await std::get<Indices>(operations), values, failure), ...);
            if (failure)
                co_return ConvertFailure<std::tuple<T...>>(std::move(*failure));
            co_return TakeValues<T...>(values, std::index_sequence_for<T...> {});
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
        using E = typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType;
        return detail::when_all::RunVoid<E>(ctx, std::tuple<TTasks...>(std::move(tasks)...),
                                            std::index_sequence_for<TTasks...> {});
    }

    /// @brief Awaits non-void tasks with one error type and returns their values in argument order.
    template<typename E, typename... T>
        requires(sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
    [[nodiscard]] inline Task<std::tuple<T...>, E> WhenAll(TaskContext& ctx, Task<T, E>... tasks)
    {
        return detail::when_all::RunValues<E>(ctx, std::tuple<Task<T, E>...>(std::move(tasks)...),
                                              std::index_sequence_for<T...> {});
    }
}// namespace NGIN::Async
