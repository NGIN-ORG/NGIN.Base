/// @file DeadlineState.hpp
/// @brief Owned deadline timer and asynchronous callback retirement.
#pragma once
#include <NGIN/Async/AsyncEnvironment.hpp>
#include <NGIN/Execution/TimerRegistration.hpp>
#include <mutex>

namespace NGIN::Async::detail
{
    struct PromiseRuntimeCommon;
    class NGIN_EXECUTION_API DeadlineState final
    {
    public:
        explicit DeadlineState(const AsyncEnvironment& environment);
        [[nodiscard]] NGIN::Execution::ScheduleResult Start(NGIN::Time::TimePoint deadline) noexcept;
        [[nodiscard]] CancellationToken               Token() const noexcept;
        void                                          Close() noexcept;
        bool                                          IsRetired() const noexcept;
        bool                                          Install(PromiseRuntimeCommon* target, NGIN::Execution::WorkItem resume) noexcept;

    private:
        struct TimerWork;
        void                               Expire() noexcept;
        void                               Retire(bool invoked) noexcept;
        NGIN::Execution::ExecutorRef       m_executor;
        LinkedCancellationSource           m_cancellation;
        NGIN::Execution::TimerRegistration m_timer;
        std::atomic<bool>                  m_closed {false};
        mutable std::mutex                 m_mutex;
        bool                               m_retired {};
        PromiseRuntimeCommon*              m_target {};
        NGIN::Execution::WorkItem          m_resume;
    };

    [[nodiscard]] NGIN_EXECUTION_API std::expected<NGIN::Time::TimePoint, NGIN::Execution::ScheduleError>
                                     DeadlineAfter(double nanoseconds) noexcept;
}// namespace NGIN::Async::detail
