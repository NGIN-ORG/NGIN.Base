#pragma once

#include "NativeFileSystemBackend.hpp"
#include <NGIN/Async/TaskContext.hpp>

#include <atomic>
#include <memory>

namespace NGIN::IO::detail
{
    struct NativeOperationCompletion
    {
        enum class Status : UInt8
        {
            Completed,
            Canceled,
            Fault
        };
        Status                  status {Status::Fault};
        Int64                   value {};
        int                     systemCode {};
        bool                    submitted {false};
        NGIN::Async::AsyncFault fault;
    };

    // Native backends publish after their final buffer access. Both the common
    // runtime completion and the selected continuation path are reserved first.
    class NativeFileAwaiter final
    {
    public:
        NativeFileAwaiter(FileSystemDriver& driver, NativeFileBackend& backend,
                          NGIN::Async::TaskContext& context, NativeFileRequest request)
            : m_driver(driver), m_backend(backend), m_executor(context.GetExecutor()),
              m_request(request), m_state(std::make_shared<State>())
        {
            m_state->driver       = &driver;
            m_state->cancellation = context.GetCancellationToken();
        }
        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            const auto state = m_state;
            if (state->cancellation.IsCancellationRequested() || m_driver.IsStopping())
            {
                state->result.status = NativeOperationCompletion::Status::Canceled;
                return false;
            }
            auto delivery = m_executor.ReserveCompletion(NGIN::Execution::WorkItem(awaiting));
            if (!delivery)
            {
                state->result.fault = SchedulingFault(delivery.error());
                return false;
            }
            state->delivery = std::move(*delivery);
            auto common     = RuntimeAccess::ReserveOperation(m_driver.GetRuntime(),
                                                              NGIN::Execution::WorkItem([state] { state->Deliver(); }));
            if (!common)
            {
                state->delivery.Reset();
                state->result.fault = SchedulingFault(common.error());
                return false;
            }
            state->common        = std::move(*common);
            m_request.userData   = state.get();
            m_request.completion = +[](void* raw, NativeFileCompletion completed) noexcept {
                static_cast<State*>(raw)->Complete(std::move(completed));
            };
            state->result.submitted = true;
            if (!m_backend.Submit(m_request, state->cancellation))
            {
                state->result.submitted = false;
                NativeFileCompletion failed;
                failed.fault = SchedulingFault(NGIN::Execution::ScheduleError::ResourceExhausted);
                state->Complete(std::move(failed));
            }
            return true;
        }
        NativeOperationCompletion await_resume() noexcept { return std::move(m_state->result); }

    private:
        static NGIN::Async::AsyncFault SchedulingFault(NGIN::Execution::ScheduleError error) noexcept
        {
            return NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(error));
        }
        struct State
        {
            FileSystemDriver*                      driver {};
            NGIN::Async::CancellationToken         cancellation;
            NGIN::Execution::CompletionReservation delivery;
            NGIN::Execution::CompletionReservation common;
            std::atomic<bool>                      done {false};
            NativeOperationCompletion              result;

            void Complete(NativeFileCompletion completed) noexcept
            {
                if (done.exchange(true, std::memory_order_acq_rel))
                    return;
                result.status     = completed.status == NativeFileCompletion::Status::Completed
                                            ? NativeOperationCompletion::Status::Completed
                                            : NativeOperationCompletion::Status::Fault;
                result.value      = completed.value;
                result.systemCode = completed.systemCode;
                result.fault      = std::move(completed.fault);
                common.Dispatch();
            }
            void Deliver() noexcept
            {
                if (result.status == NativeOperationCompletion::Status::Completed &&
                    (cancellation.IsCancellationRequested() || driver->IsStopping()))
                    result.status = NativeOperationCompletion::Status::Canceled;
                driver = nullptr;
                delivery.Dispatch();
            }
        };
        FileSystemDriver&            m_driver;
        NativeFileBackend&           m_backend;
        NGIN::Execution::ExecutorRef m_executor;
        NativeFileRequest            m_request;
        std::shared_ptr<State>       m_state;
    };
}// namespace NGIN::IO::detail
