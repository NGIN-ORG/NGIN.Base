#include "RuntimeLoop.hpp"

#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <NGIN/Time/MonotonicClock.hpp>

#include <array>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace NGIN::IO::detail
{
    namespace
    {
        thread_local const RuntimeLoop* currentLoop {};

        struct DispatchGuard final
        {
            explicit DispatchGuard(const RuntimeLoop* loop) noexcept : previous(currentLoop) { currentLoop = loop; }
            ~DispatchGuard() { currentLoop = previous; }
            const RuntimeLoop* previous;
        };
    }// namespace

    struct RuntimeLoop::Impl
    {
        explicit Impl(RuntimeLoop* runtime, Options configured, NGIN::Execution::WorkItem onStop)
            : loop(runtime), options(configured), poller(configured.registrationCapacity),
              submissions(configured.submissionCapacity, &Wake, this),
              completions(configured.completionCapacity, &Wake, this),
              operations(configured.operationCapacity, &Wake, this), stopHandler(std::move(onStop))
        {
            if (options.timerCapacity == 0 || options.batchSize == 0)
                throw std::invalid_argument("Runtime requires positive timer capacity and batch size");
        }

        static void Wake(void* state) noexcept { static_cast<Impl*>(state)->poller.Wake(); }

        struct DriveGuard final
        {
            DriveGuard(Impl& implementation, bool run) : impl(implementation)
            {
                std::lock_guard lock(impl.mutex);
                const auto      thread = std::this_thread::get_id();
                if (impl.driving)
                    throw std::logic_error("Runtime is already being driven; loop calls cannot be concurrent or reentrant");
                if (impl.owner != std::thread::id {} && impl.owner != thread)
                    throw std::logic_error("Runtime must be driven on the thread that first drove it");
                impl.owner     = thread;
                impl.driving   = true;
                impl.committed = run;
            }
            ~DriveGuard()
            {
                std::lock_guard lock(impl.mutex);
                impl.driving   = false;
                impl.committed = false;
                impl.changed.notify_all();
            }
            Impl& impl;
        };

        struct Registration
        {
            std::uintptr_t           handle;
            std::shared_ptr<Handler> handler;
            bool                     completion {false};
            bool                     armed {false};
        };
        using TimerKey = std::pair<NGIN::Time::TimePoint, std::uint64_t>;

        bool TimerOne()
        {
            NGIN::Execution::WorkItem work;
            bool                      discard;
            {
                std::lock_guard lock(mutex);
                discard = state != State::Running;
                if (timers.empty() || (!discard && timers.begin()->first.first > NGIN::Time::MonotonicClock::Now()))
                    return false;
                auto first = timers.begin();
                work       = std::move(first->second);
                timers.erase(first);
            }
            if (!discard)
                work.Invoke();
            // Stopped timers release their storage on the owner. Cancellation-
            // aware work publishes its terminal outcome through a reserved path.
            return true;
        }

        bool EventOne()
        {
            if (eventCursor == eventCount)
                return false;
            const RuntimePoller::Event event = events[eventCursor++];
            std::shared_ptr<Handler>   handler;
            {
                std::lock_guard lock(mutex);
                const auto      found = registrations.find(event.identifier);
                if (found != registrations.end())
                    handler = found->second.handler;
            }
            if (handler)
                handler->Ready(event);
            return true;// Stale generation still consumes bounded dispatch work.
        }

        bool StopOne()
        {
            std::shared_ptr<Handler>  handler;
            NGIN::Execution::WorkItem stopping;
            {
                std::lock_guard lock(mutex);
                if (state != State::Stopping)
                    return false;
                if (!stopHandler.IsEmpty())
                    stopping = std::move(stopHandler);
                else
                {
                    const auto found = registrations.upper_bound(stopCursor);
                    if (found == registrations.end())
                        return false;
                    stopCursor = found->first;
                    handler    = found->second.handler;
                }
            }
            if (handler)
                handler->Stop();
            else
                stopping.Invoke();
            return true;
        }

        bool HasReady() const noexcept
        {
            std::lock_guard lock(mutex);
            // Becoming Stopped is runnable work too. RequestStop or the last
            // reservation release may race Batch's terminal check, with their
            // wake consumed by its final nonblocking platform wait.
            return eventCursor != eventCount || submissions.HasReady() || completions.HasReady() || operations.HasReady() ||
                   (!timers.empty() && (state != State::Running || timers.begin()->first.first <= NGIN::Time::MonotonicClock::Now())) ||
                   (state == State::Stopping &&
                    (!stopHandler.IsEmpty() || registrations.upper_bound(stopCursor) != registrations.end() ||
                     (submissions.Outstanding() == 0 && completions.Outstanding() == 0 && operations.Outstanding() == 0 && timers.empty() && registrations.empty())));
        }

        bool Batch()
        {
            if (eventCursor == eventCount)
            {
                eventCount  = poller.Wait(events, NGIN::Time::MonotonicClock::Now());
                eventCursor = 0;
            }
            DispatchGuard context(loop);
            std::size_t   idleSources = 0;
            std::size_t   dispatched  = 0;
            // Persist the cursor between batches, including batchSize == 1.
            // A busy source cannot starve any other source across PollOnce calls.
            while (dispatched != options.batchSize && idleSources != 6)
            {
                bool              progress = false;
                const std::size_t source   = sourceCursor;
                sourceCursor               = (sourceCursor + 1) % 6;
                switch (source)
                {
                    case 0:
                        progress = submissions.RunOne();
                        break;
                    case 1:
                        progress = completions.RunOne();
                        break;
                    case 2:
                        progress = TimerOne();
                        break;
                    case 3:
                        progress = EventOne();
                        break;
                    case 4:
                        progress = StopOne();
                        break;
                    case 5:
                        progress = operations.RunOne();
                        break;
                }
                if (progress)
                {
                    ++dispatched;
                    idleSources = 0;
                }
                else
                    ++idleSources;
            }
            {
                std::lock_guard lock(mutex);
                if (state == State::Stopping && submissions.Outstanding() == 0 &&
                    completions.Outstanding() == 0 && operations.Outstanding() == 0 && timers.empty() && registrations.empty() && stopHandler.IsEmpty())
                {
                    state       = State::Stopped;
                    eventCursor = eventCount;// Only stale generations can remain.
                    changed.notify_all();
                }
            }
            // A full OS batch is not evidence that the readiness source is
            // drained. Preserve the next batch before reporting host readiness.
            if (eventCursor == eventCount)
            {
                eventCount  = poller.Wait(events, NGIN::Time::MonotonicClock::Now());
                eventCursor = 0;
            }
            return HasReady();
        }

        void Drive()
        {
            for (;;)
            {
                if (Batch())
                    continue;
                std::optional<NGIN::Time::TimePoint> deadline;
                {
                    std::lock_guard lock(mutex);
                    if (state == State::Stopped)
                        return;
                    if (!timers.empty())
                        deadline = timers.begin()->first.first;
                }
                // Submission after the readiness/deadline snapshots posts a
                // persistent wake. After draining it Batch always rescans queues.
                eventCount  = poller.Wait(events, deadline);
                eventCursor = 0;
            }
        }

        RuntimeLoop*                                  loop;
        const Options                                 options;
        RuntimePoller                                 poller;
        NGIN::Execution::detail::CompletionQueue      submissions;
        NGIN::Execution::detail::CompletionQueue      completions;
        NGIN::Execution::detail::CompletionQueue      operations;
        NGIN::Execution::WorkItem                     stopHandler;
        mutable std::mutex                            mutex;
        std::condition_variable                       changed;
        State                                         state {State::Running};
        std::thread::id                               owner;
        bool                                          driving {false};
        bool                                          committed {false};
        std::map<TimerKey, NGIN::Execution::WorkItem> timers;
        std::map<std::uint64_t, Registration>         registrations;
        std::map<std::uintptr_t, std::uint64_t>       handles;
        std::uint64_t                                 timerSequence {};
        std::uint64_t                                 registrationSequence {};
        std::uint64_t                                 stopCursor {};
        std::size_t                                   sourceCursor {};
        std::array<RuntimePoller::Event, 64>          events;
        std::size_t                                   eventCursor {};
        std::size_t                                   eventCount {};
    };

    RuntimeLoop::RuntimeLoop(Options options, NGIN::Execution::WorkItem stopHandler)
        : m_impl(std::make_unique<Impl>(this, options, std::move(stopHandler))) {}
    RuntimeLoop::~RuntimeLoop()
    {
        try
        {
            Shutdown();
        } catch (...)
        {
            std::terminate();
        }// Destruction cannot transfer fixed callback ownership.
    }

    std::expected<std::size_t, std::error_code>
    RuntimeLoop::CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept
    {
        return m_impl->poller.CopyNativeWaitSources(destination);
    }

    NGIN::Execution::ExecutorRef RuntimeLoop::GetExecutor() noexcept
    {
        return NGIN::Execution::ExecutorRef::From(*this);
    }

    NGIN::Execution::ScheduleResult RuntimeLoop::Execute(NGIN::Execution::WorkItem work) noexcept
    {
        auto ticket = m_impl->submissions.Reserve(std::move(work));
        if (!ticket)
            return std::unexpected(ticket.error());
        ticket->Dispatch();
        return {};
    }

    NGIN::Execution::ScheduleResult RuntimeLoop::ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint deadline) noexcept
    {
        auto timer = ScheduleTimer(std::move(work), deadline);
        if (!timer)
            return std::unexpected(timer.error());
        timer->Detach();
        return {};
    }

    std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
    RuntimeLoop::ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
    {
        return m_impl->completions.Reserve(std::move(work));
    }

    std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
    RuntimeLoop::ReserveOperation(NGIN::Execution::WorkItem work) noexcept
    {
        return m_impl->operations.Reserve(std::move(work));
    }

    bool RuntimeLoop::IsCurrent() const noexcept
    {
        return currentLoop == this;
    }

    std::expected<NGIN::Execution::TimerRegistration, NGIN::Execution::ScheduleError>
    RuntimeLoop::ScheduleTimer(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint deadline) noexcept
    {
        if (work.IsEmpty())
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->state != State::Running)
            return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
        if (m_impl->timers.size() == m_impl->options.timerCapacity ||
            m_impl->timerSequence == (std::numeric_limits<std::uint64_t>::max)())
            return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
        const TimerId timer {deadline, ++m_impl->timerSequence};
        try
        {
            auto [entry, inserted] = m_impl->timers.try_emplace(Impl::TimerKey {deadline, timer.sequence});
            entry->second          = std::move(work);
        } catch (const std::bad_alloc&)
        {
            return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
        }
        m_impl->poller.Wake();
        return NGIN::Execution::TimerRegistration(this, deadline.ToNanoseconds(), timer.sequence, +[](void* state, std::uint64_t time, std::uint64_t identifier) noexcept { return static_cast<RuntimeLoop*>(state)->CancelTimer({NGIN::Time::TimePoint::FromNanoseconds(time), identifier}); });
    }

    bool RuntimeLoop::CancelTimer(TimerId timer) noexcept
    {
        NGIN::Execution::WorkItem discarded;
        {
            std::lock_guard lock(m_impl->mutex);
            const auto      found = m_impl->timers.find({timer.deadline, timer.sequence});
            if (found == m_impl->timers.end())
                return false;
            discarded = std::move(found->second);
            m_impl->timers.erase(found);
            m_impl->poller.Wake();
        }
        return true;
    }

    std::expected<std::uint64_t, std::error_code>
    RuntimeLoop::ReserveWatch(std::uintptr_t handle, std::shared_ptr<Handler> handler) noexcept
    {
        return Register(handle, 0, nullptr, std::move(handler), false);
    }

    std::expected<std::uint64_t, std::error_code>
    RuntimeLoop::Watch(std::uintptr_t handle, unsigned interests, std::shared_ptr<Handler> handler) noexcept
    {
        return Register(handle, interests, nullptr, std::move(handler), true);
    }

    std::expected<std::uint64_t, std::error_code>
    RuntimeLoop::WatchCompletion(std::uintptr_t handle, void* operation, std::shared_ptr<Handler> handler) noexcept
    {
        if (!operation)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return Register(handle, RuntimePoller::Read | RuntimePoller::Write, operation, std::move(handler), true);
    }

    std::expected<std::uint64_t, std::error_code>
    RuntimeLoop::Register(std::uintptr_t handle, unsigned interests, void* operation, std::shared_ptr<Handler> handler, bool arm) noexcept
    {
        const std::uintptr_t key = operation ? reinterpret_cast<std::uintptr_t>(operation) : handle;
        if (!handler)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->state != State::Running)
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (m_impl->handles.contains(key))
            return std::unexpected(std::make_error_code(std::errc::device_or_resource_busy));
        if (m_impl->registrations.size() == m_impl->options.registrationCapacity ||
            m_impl->registrationSequence == (std::numeric_limits<std::uint64_t>::max)())
            return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        const std::uint64_t identifier = ++m_impl->registrationSequence;
        try
        {
            m_impl->handles.emplace(key, identifier);
            m_impl->registrations.emplace(identifier, Impl::Registration {key, handler, operation != nullptr, arm});
        } catch (const std::bad_alloc&)
        {
            m_impl->handles.erase(key);
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        }
        if (!arm)
            return identifier;
        const std::error_code error = operation ? m_impl->poller.WatchCompletion(handle, operation, identifier)
                                                : m_impl->poller.Watch(handle, identifier, interests);
        if (error)
        {
            if (operation)
                m_impl->poller.UnwatchCompletion(operation);
            else
                m_impl->poller.Unwatch(handle);
            m_impl->handles.erase(key);
            auto failed = m_impl->registrations.extract(identifier);
            // Handler destruction may reenter; keep it alive until the lock exits.
            handler = std::move(failed.mapped().handler);
            return std::unexpected(error);
        }
        return identifier;
    }

    std::error_code RuntimeLoop::Modify(std::uint64_t identifier, unsigned interests) noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        const auto      found = m_impl->registrations.find(identifier);
        if (found == m_impl->registrations.end())
            return std::make_error_code(std::errc::no_such_file_or_directory);
        if (found->second.completion)
            return std::make_error_code(std::errc::operation_not_supported);
        // Even a failed platform update can require cleanup of partial filters.
        found->second.armed = true;
        return m_impl->poller.Watch(found->second.handle, identifier, interests);
    }

    void RuntimeLoop::Unwatch(std::uint64_t identifier) noexcept
    {
        std::shared_ptr<Handler> retired;
        {
            std::lock_guard lock(m_impl->mutex);
            const auto      found = m_impl->registrations.find(identifier);
            if (found == m_impl->registrations.end())
                return;
            if (found->second.completion)
                m_impl->poller.UnwatchCompletion(reinterpret_cast<void*>(found->second.handle));
            else if (found->second.armed)
                m_impl->poller.Unwatch(found->second.handle);
            else if (m_impl->state != State::Running)
                m_impl->poller.Wake();
            m_impl->handles.erase(found->second.handle);
            retired = std::move(found->second.handler);
            m_impl->registrations.erase(found);
        }
    }

    bool RuntimeLoop::PollOnce()
    {
        Impl::DriveGuard driving(*m_impl, false);
        return m_impl->Batch();
    }

    void RuntimeLoop::Run(NGIN::Execution::WorkItem entered)
    {
        Impl::DriveGuard driving(*m_impl, true);
        if (!entered.IsEmpty())
            entered.Invoke();
        m_impl->Drive();
    }

    void RuntimeLoop::RequestStop() noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->state != State::Running)
            return;
        m_impl->submissions.Close();
        m_impl->completions.Close();
        m_impl->operations.Close();
        m_impl->state = State::Stopping;
        m_impl->poller.Wake();
    }

    void RuntimeLoop::Shutdown()
    {
        {
            std::unique_lock lock(m_impl->mutex);
            if (m_impl->state == State::Stopped)
            {
                m_impl->changed.wait(lock, [this] { return !m_impl->driving; });
                return;
            }
            if (IsCurrent())
                throw std::logic_error("Runtime::Shutdown cannot block a runtime callback; use RequestStop");
            if (m_impl->owner != std::thread::id {} && m_impl->owner != std::this_thread::get_id())
            {
                if (!m_impl->committed)
                    throw std::logic_error("Shutdown from another thread requires an active Run or RuntimeRunner; shut down on the owner");
                lock.unlock();
                RequestStop();
                lock.lock();
                m_impl->changed.wait(lock, [this] { return !m_impl->committed; });
                if (m_impl->state != State::Stopped)
                    throw std::runtime_error("Runtime driver exited before shutdown completed");
                return;
            }
        }
        Impl::DriveGuard driving(*m_impl, true);
        RequestStop();
        m_impl->Drive();
    }

    RuntimeLoop::State RuntimeLoop::GetState() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->state;
    }

    std::optional<NGIN::Time::TimePoint> RuntimeLoop::NextDeadline() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->timers.empty())
            return std::nullopt;
        return m_impl->timers.begin()->first.first;
    }

    std::intptr_t RuntimeLoop::NativeHandle() const noexcept
    {
        return m_impl->poller.NativeHandle();
    }
}// namespace NGIN::IO::detail
