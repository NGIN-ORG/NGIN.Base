#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/IO/FileSystemDriver.hpp>

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
        DriverDispatchAwaiter(FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation)
            : m_driver(driver), m_resumeExecutor(ctx.GetExecutor()), m_cancellation(ctx.GetCancellationToken()), m_operation(std::move(operation)), m_state(std::make_shared<State>())
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            m_state->resumeExecutor = m_resumeExecutor;
            m_state->awaiting       = awaiting;

            if (!m_driver.HasBackend())
            {
                CompleteWithFault(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
                return;
            }

            if (m_cancellation.IsCancellationRequested())
            {
                CompleteCanceled();
                return;
            }

            const NGIN::Async::CancellationRegistrationResult registrationResult = m_cancellation.Register(
                    m_state->registration,
                    {},
                    {},
                    +[](void* rawState) noexcept -> bool {
                        auto* state = static_cast<State*>(rawState);
                        if (!state)
                        {
                            return false;
                        }
                        state->CompleteCanceled();
                        return false;
                    },
                    m_state.get());
            if (!registrationResult)
            {
                NGIN::Async::AsyncFault fault =
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed);
                fault.native = static_cast<int>(registrationResult.error());
                CompleteWithFault(std::move(fault));
                return;
            }

            std::shared_ptr<State>                state          = m_state;
            TOperation                            operation      = std::move(m_operation);
            const NGIN::Execution::ScheduleResult scheduleResult = m_driver.GetExecutor().Execute(
                    [state, operation = std::move(operation)]() mutable noexcept {
                        if (state->done.load(std::memory_order_acquire))
                        {
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
                CompleteWithFault(std::move(fault));
            }
        }

        DriverCompletion<TResult> await_resume() noexcept
        {
            return std::move(m_state->completion);
        }

    private:
        struct State
        {
            std::atomic<bool>                     done {false};
            NGIN::Execution::ExecutorRef          resumeExecutor {};
            std::coroutine_handle<>               awaiting {};
            NGIN::Async::CancellationRegistration registration {};
            DriverCompletion<TResult>             completion {};

            void Resume() noexcept
            {
                registration.Reset();
                if (awaiting)
                {
                    if (resumeExecutor.IsValid())
                    {
                        const NGIN::Execution::ScheduleResult result = resumeExecutor.Execute(awaiting);
                        if (result)
                        {
                            return;
                        }
                    }
                    awaiting.resume();
                }
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

        FileSystemDriver&              m_driver;
        NGIN::Execution::ExecutorRef   m_resumeExecutor {};
        NGIN::Async::CancellationToken m_cancellation {};
        TOperation                     m_operation;
        std::shared_ptr<State>         m_state {};
    };

    template<typename TOperation>
    auto DispatchToDriver(FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation)
    {
        using ResultType = std::invoke_result_t<TOperation&>;
        return DriverDispatchAwaiter<ResultType, TOperation>(driver, ctx, std::move(operation));
    }
}// namespace NGIN::IO::detail
