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

        Status                  status {Status::Fault};
        std::optional<TResult>  result {};
        std::optional<NGIN::Async::AsyncFault> fault {};
    };

    template<typename TResult, typename TOperation>
    class DriverDispatchAwaiter
    {
    public:
        DriverDispatchAwaiter(FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation) noexcept
            : m_driver(driver)
            , m_resumeExecutor(ctx.GetExecutor())
            , m_cancellation(ctx.GetCancellationToken())
            , m_operation(std::move(operation))
            , m_state(std::make_shared<State>())
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
                CompleteWithFault(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidState));
                return;
            }

            if (m_cancellation.IsCancellationRequested())
            {
                CompleteCanceled();
                return;
            }

            m_cancellation.Register(
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

            auto state     = m_state;
            auto operation = std::move(m_operation);
            m_driver.GetExecutor().Execute([state, operation = std::move(operation)]() mutable noexcept {
                if (state->done.load(std::memory_order_acquire))
                {
                    return;
                }
                state->CompleteResult(operation());
            });
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
                        resumeExecutor.Execute(awaiting);
                    }
                    else
                    {
                        awaiting.resume();
                    }
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

        FileSystemDriver&                  m_driver;
        NGIN::Execution::ExecutorRef       m_resumeExecutor {};
        NGIN::Async::CancellationToken     m_cancellation {};
        TOperation                         m_operation;
        std::shared_ptr<State>             m_state {};
    };

    template<typename TOperation>
    auto DispatchToDriver(FileSystemDriver& driver, NGIN::Async::TaskContext& ctx, TOperation operation) noexcept
    {
        using ResultType = std::invoke_result_t<TOperation&>;
        return DriverDispatchAwaiter<ResultType, TOperation>(driver, ctx, std::move(operation));
    }
}// namespace NGIN::IO::detail
