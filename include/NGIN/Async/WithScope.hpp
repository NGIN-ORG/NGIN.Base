/// @file WithScope.hpp
/// @brief Asynchronous scope closure that retains the body callable through child joining.
#pragma once
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <functional>

namespace NGIN::Async
{
    namespace detail
    {
        struct ScopeEnvironment final
        {
            AsyncEnvironment environment;
            bool             await_ready() const noexcept { return false; }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept
            {
                handle.promise().m_environment = environment;
                return false;
            }
            void await_resume() const noexcept {}
        };
    }// namespace detail

    /// @brief Runs an owned body, closes admission, requests stop, and joins before returning.
    /// @details A body error is primary; otherwise the lowest spawn-index child error is primary.
    /// Borrowed child resources must outlive joining. A body-local variable is not extended by this helper.
    template<typename E = NoError, typename Body>
    auto WithScope(AsyncEnvironment environment, std::size_t capacity, Body body)
            -> Task<typename std::invoke_result_t<Body&, TaskScope<E>&>::ValueType, ScopeFailure<E>>
    {
        using BodyTask = std::invoke_result_t<Body&, TaskScope<E>&>;
        using T        = typename BodyTask::ValueType;
        using Out      = Completion<T, ScopeFailure<E>>;
        static_assert(std::is_same_v<typename BodyTask::ErrorType, E>);
        environment.Validate();
        co_await SwitchTo(environment.executor);
        TaskScope<E> scope(environment, capacity);
        co_await detail::ScopeEnvironment {scope.GetEnvironment()};
        std::optional<Completion<T, E>> outcome;
        try
        {
            auto task = std::invoke(body, scope);
            outcome.emplace(co_await task.AsCompletion());
        } catch (...)
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            outcome.emplace(Completion<T, E>::Faulted(std::move(fault)));
        }
        scope.Close();
        scope.RequestStop(StopReason::OwnerClosed);
        auto               report = co_await scope.Join();
        std::optional<Out> result;
        if (outcome->HasError())
            result.emplace(Out::DomainFailure(ScopeFailure<E> {std::move(*outcome).Error(), report, true}));
        else if (report.HasError())
            result.emplace(Out::DomainFailure(ScopeFailure<E> {report.RetainError(), report, false}));
        else if (outcome->IsStopped())
            result.emplace(Out::Stopped(outcome->StopReason()));
        else if (report.IsStopped())
            result.emplace(Out::Stopped(report.StopReason()));
        else if constexpr (std::is_void_v<T>)
            result.emplace(Out::Success());
        else
            result.emplace(Out::Success(std::move(*outcome).Value()));
        if constexpr (std::is_void_v<T>)
        {
            if (result->HasError())
                co_await Failure(std::move(*result).Error());
            else if (result->IsStopped())
                co_await Stopped(result->StopReason());
            co_return;
        }
        else
            co_return std::move(*result);
    }
}// namespace NGIN::Async
