#include <NGIN/Async/FirstCompleted.hpp>
#include <atomic>
#include <vector>

namespace NGIN::Async::detail
{
    struct FirstCompletedState final
    {
        struct Watch final
        {
            OperationRef       operation;
            CompletionObserver observer;
        };

        FirstCompletedState(std::span<const OperationRef> operations, CancellationToken waitToken,
                            std::pmr::memory_resource* resource)
            : watches(resource), token(std::move(waitToken))
        {
            watches.reserve(operations.size());
            for (std::size_t index = 0; index != operations.size(); ++index)
                watches.push_back(Watch {operations[index], CompletionObserver {&Notify, this, index}});
        }

        static void Notify(CompletionObserver& observer, CompletionKind kind) noexcept
        {
            auto& state = *static_cast<FirstCompletedState*>(observer.context);
            if (!state.claimed.exchange(true, std::memory_order_acq_rel))
            {
                state.notification = CompletionNotification {observer.index, kind};
                state.Publish(2);
            }
        }

        static bool Cancel(void* context) noexcept
        {
            auto& state = *static_cast<FirstCompletedState*>(context);
            if (!state.claimed.exchange(true, std::memory_order_acq_rel))
            {
                state.reason = state.token.GetStopReason();
                state.Publish(2);
            }
            return false;
        }

        void Publish(unsigned flag) noexcept
        {
            // Setup and winner publication may race. Only the second publisher
            // queues delivery, and neither publisher accesses state afterward.
            if ((published.fetch_or(flag, std::memory_order_acq_rel) | flag) == 3)
                target->QueueContinuation(NGIN::Execution::WorkItem([this] { Deliver(); }));
        }

        void Start() noexcept
        {
            for (auto& watch: watches)
            {
                if (claimed.load(std::memory_order_acquire))
                    break;
                watch.operation.m_state->Register(watch.observer);
            }
            if (!claimed.load(std::memory_order_acquire))
            {
                const auto registered = token.Register(cancellation, {}, {}, &Cancel, this);
                if (!registered && !claimed.exchange(true, std::memory_order_acq_rel))
                {
                    fault = MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                NGIN::Execution::ScheduleError::ResourceExhausted);
                    Publish(2);
                }
            }
            Publish(1);
        }

        void Deliver() noexcept
        {
            cancellation.Reset();
            for (auto& watch: watches)
                watch.operation.m_state->Unregister(watch.observer);
            watches.clear();
            token = {};
            // Drop all source leases before user resumption. The parent frame
            // owns this state; invoking its retained continuation may destroy us.
            auto continuation = std::move(resume);
            continuation.Invoke();
        }

        std::pmr::vector<Watch>               watches;
        CancellationToken                     token;
        CancellationRegistration              cancellation;
        std::atomic<bool>                     claimed {false};
        std::atomic<unsigned>                 published {0};
        std::optional<CompletionNotification> notification;
        std::optional<AsyncFault>             fault;
        StopReason                            reason {StopReason::Requested};
        PromiseRuntimeCommon*                 target {};
        NGIN::Execution::WorkItem             resume;
    };

    struct FirstCompletedAwaiter final
    {
        std::shared_ptr<FirstCompletedState> state;
        bool                                 await_ready() const noexcept { return false; }
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
        {
            if constexpr (std::derived_from<Promise, PromiseRuntimeCommon>)
            {
                if (awaiting.promise().HasContinuation())
                {
                    awaiting.promise().RetainFrameReference();
                    state->target = &awaiting.promise();
                    state->resume = NGIN::Execution::WorkItem([awaiting] {
                        PromiseRuntimeCommon::ResumeRetained(awaiting);
                    });
                    state->Start();
                    return true;
                }
            }
            state->fault = MakeAsyncFault(AsyncFaultCode::InvalidContinuationState);
            return false;
        }
        Completion<CompletionNotification, NoError> await_resume() noexcept
        {
            (void) state->published.load(std::memory_order_acquire);
            if (state->fault)
                return Completion<CompletionNotification, NoError>::Faulted(std::move(*state->fault));
            if (state->notification)
                return Completion<CompletionNotification, NoError>::Success(*state->notification);
            return Completion<CompletionNotification, NoError>::Stopped(state->reason);
        }
    };
}// namespace NGIN::Async::detail

namespace NGIN::Async
{
    Task<CompletionNotification> FirstCompleted(std::span<const OperationRef> operations, CancellationToken token)
    {
        using Result = Completion<CompletionNotification, NoError>;
        if (operations.empty())
            co_return Result::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
        for (const auto& operation: operations)
            if (!operation.IsValid())
                co_return Result::Faulted(MakeAsyncFault(AsyncFaultCode::InvalidTaskUsage));
        for (std::size_t index = 0; index != operations.size(); ++index)
            if (const auto kind = operations[index].Kind())
                co_return Result::Success(CompletionNotification {index, *kind});
        if (token.IsCancellationRequested())
            co_return Result::Stopped(token.GetStopReason());

        const auto                                   environment = co_await CurrentEnvironment();
        std::shared_ptr<detail::FirstCompletedState> state;
        try
        {
            state = std::allocate_shared<detail::FirstCompletedState>(
                    std::pmr::polymorphic_allocator<detail::FirstCompletedState> {environment.resource},
                    operations, std::move(token), environment.resource);
        } catch (const std::bad_alloc&)
        {
            co_return Result::Faulted(detail::MakeSchedulingFault(
                    AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::ResourceExhausted));
        }
        co_return co_await detail::FirstCompletedAwaiter {std::move(state)};
    }
}// namespace NGIN::Async
