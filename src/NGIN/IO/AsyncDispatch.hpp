#pragma once

#include "FileSystemDriver.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace NGIN::IO::detail
{
    template<typename TResult>
    struct DriverCompletion
    {
        enum class Status : UInt8
        {
            Result,
            Canceled,
            Fault,
        };

        [[nodiscard]] bool IsResult() const noexcept { return status == Status::Result; }
        [[nodiscard]] bool IsCanceled() const noexcept { return status == Status::Canceled; }
        [[nodiscard]] bool IsFault() const noexcept { return status == Status::Fault; }

        Status                                 status {Status::Fault};
        std::optional<TResult>                 result {};
        std::optional<NGIN::Async::AsyncFault> fault {};
    };

    template<typename TResult, typename TOperation>
    class DriverDispatchAwaiter
    {
        static_assert(std::is_nothrow_move_constructible_v<TResult>,
                      "driver dispatch results must be movable into shared completion state without throwing");

    public:
        DriverDispatchAwaiter(NGIN::IO::detail::FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation)
            : m_driver(driver), m_resumeExecutor(ctx.GetExecutor()), m_cancellation(ctx.GetCancellationToken()), m_operation(std::move(operation)), m_state(std::make_shared<State>())
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        bool await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            // Keep setup state alive even if an executor dispatches completion
            // before await_suspend returns and destroys the awaiter.
            std::shared_ptr<State> state = m_state;

            if (!m_driver.HasBackend())
            {
                CompleteWithFault(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
                return false;
            }

            if (m_cancellation.IsCancellationRequested())
            {
                CompleteCanceled();
                return false;
            }

            auto reservation = m_resumeExecutor.ReserveCompletion(NGIN::Execution::WorkItem(awaiting));
            if (!reservation)
            {
                state->CompleteFault(NGIN::Async::MakeAsyncFault(
                        NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                        static_cast<int>(reservation.error())));
                return false;
            }
            state->delivery = std::move(*reservation);
            auto runtimeCompletion = RuntimeAccess::ReserveOperation(m_driver.GetRuntime(),
                    NGIN::Execution::WorkItem([state] { state->Deliver(); }));
            if (!runtimeCompletion)
            {
                state->delivery.Reset();
                state->CompleteFault(NGIN::Async::MakeAsyncFault(
                        NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                        static_cast<int>(runtimeCompletion.error())));
                return false;
            }
            state->runtimeCompletion = std::move(*runtimeCompletion);
            state->driver = &m_driver;

            const NGIN::Async::CancellationRegistrationResult registrationResult = m_cancellation.Register(
                    state->registration,
                    {},
                    {},
                    +[](void* rawState) noexcept -> bool {
                        State* callbackState = static_cast<State*>(rawState);
                        if (!callbackState)
                        {
                            return false;
                        }
                        // A running blocking call cannot be interrupted safely.
                        // Its worker owns terminal publication after all buffer access.
                        callbackState->cancellationRequested.store(true, std::memory_order_release);
                        return false;
                    },
                    state.get());
            if (!registrationResult)
            {
                NGIN::Async::AsyncFault fault =
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed);
                fault.native = static_cast<int>(registrationResult.error());
                CompleteWithFault(std::move(fault));
                return true;
            }

            TOperation                            operation      = std::move(m_operation);
            const NGIN::Execution::ScheduleResult scheduleResult = m_driver.GetExecutor().Execute(
                    [state, operation = std::move(operation)]() mutable noexcept {
                        if (state->cancellationRequested.load(std::memory_order_acquire) || state->driver->IsStopping())
                        {
                            state->CompleteCanceled();
                            return;
                        }
                        try
                        {
                            state->CompleteResult(operation());
                        } catch (...)
                        {
                            state->CompleteFault(NGIN::Async::MakeAsyncFault(
                                    NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
                        }
                    });
            if (!scheduleResult)
            {
                NGIN::Async::AsyncFault fault =
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed);
                fault.native = static_cast<int>(scheduleResult.error());
                state->CompleteFault(std::move(fault));
            }
            return true;
        }

        DriverCompletion<TResult> await_resume() noexcept
        {
            return std::move(m_state->completion);
        }

    private:
        struct State
        {
            std::atomic<bool>                      done {false};
            std::atomic<bool>                      cancellationRequested {false};
            NGIN::Execution::CompletionReservation delivery {};
            NGIN::Execution::CompletionReservation runtimeCompletion {};
            FileSystemDriver* driver {};
            NGIN::Async::CancellationRegistration  registration {};
            DriverCompletion<TResult>              completion {};

            void Resume() noexcept
            {
                if (runtimeCompletion.IsValid())
                    runtimeCompletion.Dispatch();
                else
                    delivery.Dispatch();
            }

            void Deliver() noexcept
            {
                registration.Reset();
                if (completion.IsResult() && (cancellationRequested.load(std::memory_order_acquire) || driver->IsStopping()))
                {
                    completion.status = DriverCompletion<TResult>::Status::Canceled;
                    completion.result.reset();
                }
                driver = nullptr;
                delivery.Dispatch();
            }

            void CompleteCanceled() noexcept
            {
                bool expected = false;
                if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return;
                }

                completion.status = DriverCompletion<TResult>::Status::Canceled;
                Resume();
            }

            void CompleteFault(NGIN::Async::AsyncFault fault) noexcept
            {
                bool expected = false;
                if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return;
                }

                completion.status = DriverCompletion<TResult>::Status::Fault;
                completion.fault.emplace(std::move(fault));
                Resume();
            }

            void CompleteResult(TResult result) noexcept
            {
                if (cancellationRequested.load(std::memory_order_acquire))
                {
                    CompleteCanceled();
                    return;
                }
                bool expected = false;
                if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    return;
                }

                completion.status = DriverCompletion<TResult>::Status::Result;
                completion.result.emplace(std::move(result));
                Resume();
            }
        };

        void CompleteCanceled() noexcept
        {
            m_state->CompleteCanceled();
        }

        void CompleteWithFault(NGIN::Async::AsyncFault fault) noexcept
        {
            m_state->CompleteFault(std::move(fault));
        }

        NGIN::IO::detail::FileSystemDriver& m_driver;
        NGIN::Execution::ExecutorRef        m_resumeExecutor {};
        NGIN::Async::CancellationToken      m_cancellation {};
        TOperation                          m_operation;
        std::shared_ptr<State>              m_state {};
    };

    template<typename TOperation>
    auto DispatchToDriver(NGIN::IO::detail::FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation)
    {
        using ResultType = std::invoke_result_t<TOperation&>;
        return DriverDispatchAwaiter<ResultType, TOperation>(driver, ctx, std::move(operation));
    }
}// namespace NGIN::IO::detail
