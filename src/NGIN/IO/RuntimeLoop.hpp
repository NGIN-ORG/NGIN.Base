#pragma once

#include "RuntimePoller.hpp"

#include <NGIN/Execution/ExecutorRef.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace NGIN::IO::detail
{
    // Implementation of Runtime's single-consumer execution and ownership policy.
    // Services borrow it; registrations and completion tickets must be retired
    // before Stopped. No application coroutine pointers enter the platform poller.
    class NGIN_IORUNTIME_API RuntimeLoop final
    {
    public:
        struct Options
        {
            std::size_t submissionCapacity {4096};
            std::size_t timerCapacity {4096};
            std::size_t completionCapacity {8192};
            std::size_t operationCapacity {4096};
            std::size_t registrationCapacity {4096};
            std::size_t batchSize {64};
        };
        enum class State
        {
            Running,
            Stopping,
            Stopped
        };
        struct TimerId
        {
            NGIN::Time::TimePoint deadline;
            std::uint64_t         sequence {};
        };
        class Handler
        {
        public:
            virtual ~Handler()                                             = default;
            virtual void Ready(const RuntimePoller::Event& event) noexcept = 0;
            // Invoked once on the loop after stop. Retire the registration when
            // backend access ends, after transferring all reserved completions.
            virtual void Stop() noexcept = 0;
        };

        explicit RuntimeLoop(Options options, NGIN::Execution::WorkItem stopHandler = {});
        ~RuntimeLoop();
        RuntimeLoop(const RuntimeLoop&)            = delete;
        RuntimeLoop& operator=(const RuntimeLoop&) = delete;

        NGIN::Execution::ExecutorRef    GetExecutor() noexcept;
        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem work) noexcept;
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint deadline) noexcept;
        std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
        ReserveCompletion(NGIN::Execution::WorkItem work) noexcept;
        std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
             ReserveOperation(NGIN::Execution::WorkItem work) noexcept;
        bool IsCurrent() const noexcept;

        std::expected<NGIN::Execution::TimerRegistration, NGIN::Execution::ScheduleError>
        ScheduleTimer(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint deadline) noexcept;
        // Removes queued storage immediately. False means already dispatched or
        // removed. Destruction may publish a previously reserved terminal result.
        bool CancelTimer(TimerId timer) noexcept;

        // Reserves bounded ownership before OS work; Modify arms readiness only
        // if the first nonblocking attempt needs to wait. Stop covers both states.
        std::expected<std::uint64_t, std::error_code>
        ReserveWatch(std::uintptr_t handle, std::shared_ptr<Handler> handler) noexcept;
        std::expected<std::uint64_t, std::error_code>
        Watch(std::uintptr_t handle, unsigned interests, std::shared_ptr<Handler> handler) noexcept;
        std::expected<std::uint64_t, std::error_code>
                        WatchCompletion(std::uintptr_t handle, void* operation, std::shared_ptr<Handler> handler) noexcept;
        std::error_code Modify(std::uint64_t identifier, unsigned interests) noexcept;
        void            Unwatch(std::uint64_t identifier) noexcept;

        // Fixed ownership, nonreentrant. PollOnce never blocks and reports ready
        // backlog. Run waits without polling until stop and complete drainage.
        bool  PollOnce();
        void  Run(NGIN::Execution::WorkItem entered = {});
        void  RequestStop() noexcept;
        void  Shutdown();
        State GetState() const noexcept;

        // Host-loop snapshots. Notifications remain readable until PollOnce;
        // ready backlog must be pumped before the host waits again.
        std::optional<NGIN::Time::TimePoint> NextDeadline() const noexcept;
        std::intptr_t                        NativeHandle() const noexcept;
        std::expected<std::size_t, std::error_code>
        CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept;

    private:
        std::expected<std::uint64_t, std::error_code> Register(
                std::uintptr_t handle, unsigned interests, void* operation, std::shared_ptr<Handler> handler, bool arm) noexcept;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::IO::detail
