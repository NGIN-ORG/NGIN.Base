/// @file WhenAll.hpp
/// @brief Task combinator that completes when all owned child tasks complete.
#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/TaskScope.hpp>

namespace NGIN::Async
{
    namespace detail::when_all
    {
        template<typename T, typename E>
        auto Admit(TaskScope<E>& scope, Task<T, E> task,
                   std::optional<NGIN::Execution::ScheduleError>& rejected)
                -> std::expected<Operation<T, E>, NGIN::Execution::ScheduleError>
        {
            if (rejected)
                return std::unexpected(*rejected);
            auto operation = scope.Spawn(std::move(task));
            if (!operation)
            {
                rejected = operation.error();
                scope.Close();
                scope.RequestStop(StopReason::OwnerClosed);
            }
            return operation;
        }

        inline AsyncFault CurrentFault() noexcept
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            return fault;
        }

        template<typename E>
        std::optional<Completion<void, E>> FinalFailure(
                const ScopeReport<E>& report, const std::optional<NGIN::Execution::ScheduleError>& rejected,
                std::optional<AsyncFault> exception, CancellationToken cancellation)
        {
            using Result = Completion<void, E>;
            if (exception)
                return Result::Faulted(std::move(*exception));
            if (rejected && *rejected != NGIN::Execution::ScheduleError::Stopped)
                return Result::Faulted(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, *rejected));
            if (report.HasError())
                return Result::Failure(report.RetainError());
            if (report.IsStopped())
                return Result::Stopped(report.StopReason());
            if (rejected)
                return Result::Stopped(cancellation.GetStopReason());
            return {};
        }

        template<typename T, typename E>
        inline void CaptureFailure(Completion<T, E> completion, std::optional<Completion<void, E>>& failure)
        {
            if (completion.Succeeded() || failure)
            {
                return;
            }
            if (completion.IsStopped())
                failure.emplace(Completion<void, E>::Stopped(completion.StopReason()));
            else
                failure.emplace(Completion<void, E>::Failure(std::move(completion).Error()));
        }

        template<typename Out, typename E>
        [[nodiscard]] inline Completion<Out, E> ConvertFailure(Completion<void, E> failure)
        {
            if (failure.IsStopped())
                return Completion<Out, E>::Stopped(failure.StopReason());
            return Completion<Out, E>::Failure(std::move(failure).Error());
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
                co_await Stopped(ctx.GetCancellationToken().GetStopReason());
                co_return;
            }
            TaskScope<E>                                  scope(ctx.GetEnvironment(), sizeof...(Tasks));
            std::optional<NGIN::Execution::ScheduleError> rejected;
            std::optional<AsyncFault>                     exception;
            try
            {
                ((void) Admit(scope, std::move(std::get<Indices>(tasks)), rejected), ...);
            } catch (...)
            {
                exception = CurrentFault();
                scope.RequestStop(StopReason::OwnerClosed);
            }
            scope.Close();
            const auto report  = co_await scope.Join();
            auto       failure = FinalFailure(report, rejected, std::move(exception), scope.GetCancellationToken());
            if (failure)
            {
                if (failure->IsStopped())
                    co_await Stopped(failure->StopReason());
                else
                    co_await Failure(std::move(*failure).Error());
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
                co_return Completion<std::tuple<T...>, E>::Stopped(ctx.GetCancellationToken().GetStopReason());
            TaskScope<E>                                  scope(ctx.GetEnvironment(), sizeof...(T));
            std::tuple<std::optional<T>...>               values;
            std::optional<Completion<void, E>>            failure;
            std::optional<NGIN::Execution::ScheduleError> rejected;
            std::optional<AsyncFault>                     exception;
            try
            {
                auto operations = std::tuple {Admit(scope, std::move(std::get<Indices>(tasks)), rejected)...};
                ((std::get<Indices>(operations)
                          ? CaptureValue<Indices>(co_await *std::get<Indices>(operations), values, failure)
                          : void()),
                 ...);
            } catch (...)
            {
                exception = CurrentFault();
                scope.RequestStop(StopReason::OwnerClosed);
            }
            scope.Close();
            const auto report = co_await scope.Join();
            if (auto final = FinalFailure(report, rejected, std::move(exception), scope.GetCancellationToken()))
                failure.emplace(std::move(*final));
            if (failure)
                co_return ConvertFailure<std::tuple<T...>>(std::move(*failure));
            co_return TakeValues<T...>(values, std::index_sequence_for<T...> {});
        }
    }// namespace detail::when_all

    /// @brief Awaits a non-empty set of `Task<void, E>` operations and joins all admitted children and reports errors in argument order.
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
