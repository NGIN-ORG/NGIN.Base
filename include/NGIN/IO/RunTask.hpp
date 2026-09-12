/// @file RunTask.hpp
/// @brief Standalone task entry with structured child joining and runtime shutdown.
#pragma once

#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/IO/Runtime.hpp>

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace NGIN::IO
{
    namespace detail
    {
        // Root factories receive a context and an explicitly typed task scope.
        template<typename Factory, typename E>
        using RootTask = std::remove_cvref_t<std::invoke_result_t<Factory&, NGIN::Async::TaskContext&,
                                                                  NGIN::Async::TaskScope<E>&>>;

        inline NGIN::Async::AsyncFault RootException() noexcept
        {
            auto fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            return fault;
        }

        template<typename Factory, typename E>
        NGIN::Async::Task<NGIN::Async::Completion<typename RootTask<Factory, E>::ValueType, E>> RunRoot(
                NGIN::Async::TaskContext&, NGIN::Async::TaskContext& rootContext,
                NGIN::Async::TaskScope<E>& scope, Factory& factory)
        {
            using T       = typename RootTask<Factory, E>::ValueType;
            using Outcome = NGIN::Async::Completion<T, E>;
            std::optional<Outcome>       outcome;
            NGIN::Async::Operation<T, E> root;
            try
            {
                root = NGIN::Async::Spawn(rootContext, std::invoke(factory, rootContext, scope));
                outcome.emplace(co_await root);
            } catch (...)
            {
                outcome.emplace(Outcome::Faulted(RootException()));
            }
            scope.RequestCancel();
            (void) co_await scope.Join();
            // Failure propagation can leave locals in a suspended root frame.
            // Keep them alive while children join, then release before shutdown.
            co_await NGIN::Async::detail::OperationAccess::JoinRetirement(root);
            try
            {
                auto children = scope.TakeResult();
                if (outcome->Succeeded() && !children.Succeeded())
                {
                    if (children.IsDomainError())
                        outcome.emplace(Outcome::DomainFailure(std::move(children).DomainError()));
                    else if (children.IsCanceled())
                        outcome.emplace(Outcome::Canceled());
                    else
                        outcome.emplace(Outcome::Faulted(std::move(children).Fault()));
                }
            } catch (...)
            {
                if (outcome->Succeeded())
                    outcome.emplace(Outcome::Faulted(RootException()));
            }
            co_return std::move(*outcome);
        }
    }// namespace detail

    /// @brief Runs a root factory, cancels and joins its scope, and shuts down runtime.
    /// @tparam E Root and child domain-error type, default NoError.
    /// @param root Factory `(TaskContext&, TaskScope<E>&) -> Task<T, E>`.
    /// @param cancellation Cancellation observed by the root and every scoped child.
    /// @param childCapacity Maximum children retained by the scope before joining.
    /// @return Root failure takes precedence; otherwise the first child failure or root success.
    /// @details The caller becomes the fixed runtime owner. Every terminal root outcome
    /// cancels unfinished children, joins them, and drains shutdown before returning.
    /// The factory remains alive throughout execution. It may join its scope itself;
    /// RunTask consumes the scope outcome after its final join. New I/O remains rejected
    /// during stopping. External child executors must be driven independently.
    /// @throws std::logic_error if called from a runtime callback or another owner thread.
    template<typename E = NGIN::Async::NoError, typename Factory>
    [[nodiscard]] auto RunTask(Runtime& runtime, Factory&& root, NGIN::Async::CancellationToken cancellation = {},
                               std::size_t childCapacity = 256)
            -> NGIN::Async::Completion<typename detail::RootTask<std::decay_t<Factory>, E>::ValueType, E>
    {
        using OwnedFactory = std::decay_t<Factory>;
        using Task         = detail::RootTask<OwnedFactory, E>;
        using T            = typename Task::ValueType;
        using Outcome      = NGIN::Async::Completion<T, E>;
        static_assert(std::is_same_v<typename Task::ErrorType, E>, "RunTask root and scope must have the same error type");
        // Validate/bind ownership before publishing a root that borrows stack state.
        (void) runtime.PollOnce();
        if (runtime.GetState() != Runtime::State::Running)
        {
            runtime.Shutdown();
            return Outcome::Faulted(NGIN::Async::detail::MakeSchedulingFault(
                    NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Stopped));
        }
        OwnedFactory              factory(std::forward<Factory>(root));
        NGIN::Async::TaskScope<E> scope(runtime.GetExecutor(), cancellation, childCapacity);
        NGIN::Async::TaskContext  rootContext(runtime.GetExecutor(), std::move(cancellation));
        NGIN::Async::TaskContext  supervisorContext(runtime.GetExecutor());
        auto                      notification = runtime.GetExecutor().ReserveCompletion(NGIN::Execution::WorkItem([&runtime] {
            runtime.RequestStop();
        }));
        if (!notification)
        {
            runtime.Shutdown();
            return Outcome::Faulted(NGIN::Async::detail::MakeSchedulingFault(
                    NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, notification.error()));
        }
        auto operation = NGIN::Async::Spawn(supervisorContext,
                                            detail::RunRoot(supervisorContext, rootContext, scope, factory));
        if (!NGIN::Async::detail::OperationAccess::Observe(operation, std::move(*notification)))
            std::terminate();
        runtime.Run();
        auto result = operation.TakeResult();
        if (result.Succeeded())
            return std::move(result).Value();
        if (result.IsCanceled())
            return Outcome::Canceled();
        return Outcome::Faulted(std::move(result).Fault());
    }
}// namespace NGIN::IO
