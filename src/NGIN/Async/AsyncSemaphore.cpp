#include <NGIN/Async/AsyncSemaphore.hpp>

#include <atomic>
#include <cassert>
#include <memory>
#include <memory_resource>
#include <utility>

namespace NGIN::Async
{
    struct AsyncSemaphore::Waiter final : std::enable_shared_from_this<Waiter>
    {
        using Handle = std::coroutine_handle<Task<void>::promise_type>;
        enum class Outcome
        {
            Pending,
            Acquired,
            Stopped,
            Fault
        };

        Waiter(AsyncSemaphore& owner, Handle handle) noexcept
            : semaphore(owner), awaiting(handle), cancellation(handle.promise().m_environment.cancellation)
        {
            awaiting.promise().RetainFrameReference();
        }

        ~Waiter() { ReleaseFrame(); }

        void ReleaseFrame() noexcept
        {
            if (leased)
            {
                leased = false;
                awaiting.promise().ReleaseFrameReference(awaiting);
            }
        }

        // Setup and terminal publication must both finish before delivery can run.
        // Only the second publisher touches the ticket; Dispatch never runs inline.
        void Publish(unsigned flag) noexcept
        {
            if ((publication.fetch_or(flag, std::memory_order_acq_rel) | flag) == 3U)
            {
                auto ticket = std::move(delivery);
                ticket.Dispatch();
            }
        }

        void Cancel() noexcept
        {
            {
                std::lock_guard lock(semaphore.m_mutex);
                if (outcome != Outcome::Pending)
                    return;
                semaphore.Remove(*this);
                outcome = Outcome::Stopped;
            }
            Publish(2U);
        }

        void Resume() noexcept
        {
            // Synchronize with cancellation before returning control to a caller
            // that may immediately destroy the semaphore or its borrowed resources.
            registration.Reset();
            auto handle  = awaiting;
            auto result  = outcome;
            auto failure = std::move(fault);
            auto reason  = cancellation.GetStopReason();
            ReleaseFrame();
            if (result == Outcome::Acquired)
                handle.resume();
            else
            {
                if (result == Outcome::Stopped)
                    handle.promise().SetStopped(reason);
                else
                    handle.promise().SetFault(std::move(failure));
                handle.promise().MarkFinishedAndResume(handle);
            }
        }

        AsyncSemaphore&                        semaphore;
        Handle                                 awaiting;
        CancellationToken                      cancellation;
        CancellationRegistration               registration;
        NGIN::Execution::CompletionReservation delivery;
        std::atomic<unsigned>                  publication {0};
        Outcome                                outcome {Outcome::Pending};// Protected by semaphore.m_mutex until publication.
        AsyncFault                             fault;
        Waiter*                                previous {};
        Waiter*                                next {};
        bool                                   queued {false};
        bool                                   leased {true};
    };

    struct AsyncSemaphore::WaitAwaiter final
    {
        AsyncSemaphore& semaphore;
        bool            await_ready() const noexcept { return false; }
        bool            await_suspend(Waiter::Handle handle) const noexcept
        {
            auto&      promise     = handle.promise();
            const auto environment = promise.m_environment;
            if (environment.cancellation.IsCancellationRequested())
            {
                promise.SetStopped(environment.cancellation.GetStopReason());
                promise.MarkFinishedAndResume(handle);
                return true;
            }
            if (semaphore.TryAcquire())
                return false;

            std::shared_ptr<Waiter> state;
            try
            {
                state = std::allocate_shared<Waiter>(std::pmr::polymorphic_allocator<Waiter>(environment.resource), semaphore, handle);
            } catch (const std::bad_alloc&)
            {
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted)));
                promise.MarkFinishedAndResume(handle);
                return true;
            }
            auto reserved = environment.executor.ReserveCompletion(NGIN::Execution::WorkItem([state] { state->Resume(); }));
            if (!reserved)
            {
                state->ReleaseFrame();
                promise.SetFault(MakeAsyncFault(AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(reserved.error())));
                promise.MarkFinishedAndResume(handle);
                return true;
            }
            state->delivery       = std::move(*reserved);
            const auto registered = environment.cancellation.Register(state->registration, {}, {}, +[](void* raw) noexcept {
                static_cast<Waiter*>(raw)->Cancel();
                return false; }, state.get());

            bool completed = false;
            {
                std::lock_guard lock(semaphore.m_mutex);
                // Cancellation may have run synchronously during registration.
                if (state->outcome == Waiter::Outcome::Pending)
                {
                    if (!registered)
                    {
                        state->fault   = MakeAsyncFault(AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(registered.error()));
                        state->outcome = Waiter::Outcome::Fault;
                        completed      = true;
                    }
                    else if (semaphore.m_count != 0)
                    {
                        --semaphore.m_count;
                        state->outcome = Waiter::Outcome::Acquired;
                        completed      = true;
                    }
                    else
                    {
                        state->previous = semaphore.m_tail;
                        if (semaphore.m_tail)
                            semaphore.m_tail->next = state.get();
                        else
                            semaphore.m_head = state.get();
                        semaphore.m_tail = state.get();
                        state->queued    = true;
                    }
                }
            }
            if (completed)
                state->Publish(2U);
            state->Publish(1U);
            return true;
        }
        void await_resume() const noexcept {}
    };

    AsyncSemaphore::AsyncSemaphore(UIntSize initialCount, UIntSize maximumCount) noexcept
        : m_count(initialCount), m_maximumCount(maximumCount)
    {
        assert(maximumCount > 0 && initialCount <= maximumCount);
    }

    AsyncSemaphore::~AsyncSemaphore()
    {
        assert(m_head == nullptr && "Cancel and join semaphore waiters before destruction");
    }

    Task<void> AsyncSemaphore::WaitAsync()
    {
        co_await WaitAwaiter {*this};
    }

    Task<AsyncSemaphore::Permit> AsyncSemaphore::AcquireAsync()
    {
        co_await WaitAsync();
        co_return Permit(*this);
    }

    bool AsyncSemaphore::TryAcquire() noexcept
    {
        std::lock_guard lock(m_mutex);
        if (m_count == 0)
            return false;
        --m_count;
        return true;
    }

    UIntSize AsyncSemaphore::CurrentCount() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_count;
    }

    void AsyncSemaphore::Remove(Waiter& waiter) noexcept
    {
        if (!waiter.queued)
            return;
        if (waiter.previous)
            waiter.previous->next = waiter.next;
        else
            m_head = waiter.next;
        if (waiter.next)
            waiter.next->previous = waiter.previous;
        else
            m_tail = waiter.previous;
        waiter.queued   = false;
        waiter.previous = waiter.next = nullptr;
    }

    void AsyncSemaphore::Release() noexcept
    {
        std::shared_ptr<Waiter> waiter;
        {
            std::lock_guard lock(m_mutex);
            if (!m_head)
            {
                assert(m_count < m_maximumCount && "AsyncSemaphore released beyond maximumCount");
                ++m_count;
                return;
            }
            waiter = m_head->shared_from_this();
            Remove(*waiter);
            waiter->outcome = Waiter::Outcome::Acquired;
        }
        waiter->Publish(2U);
    }

    AsyncSemaphore::Permit::Permit(Permit&& other) noexcept
        : m_semaphore(std::exchange(other.m_semaphore, nullptr)) {}

    AsyncSemaphore::Permit& AsyncSemaphore::Permit::operator=(Permit&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_semaphore = std::exchange(other.m_semaphore, nullptr);
        }
        return *this;
    }

    AsyncSemaphore::Permit::~Permit()
    {
        Release();
    }

    void AsyncSemaphore::Permit::Release() noexcept
    {
        if (auto* semaphore = std::exchange(m_semaphore, nullptr))
            semaphore->Release();
    }
}// namespace NGIN::Async
