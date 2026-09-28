#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/detail/DeadlineState.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <cmath>
#include <limits>

namespace NGIN::Async::detail
{
    struct DeadlineState::TimerWork final
    {
        explicit TimerWork(DeadlineState* owner) noexcept : state(owner) {}
        TimerWork(TimerWork&& other) noexcept : state(std::exchange(other.state, nullptr)), invoked(other.invoked) {}
        ~TimerWork()
        {
            if (state)
                state->Retire(invoked);
        }
        void operator()() noexcept
        {
            invoked = true;
            state->Expire();
        }
        DeadlineState* state;
        bool           invoked {};
    };

    DeadlineState::DeadlineState(const AsyncEnvironment& environment)
        : m_executor(environment.executor), m_cancellation({environment.cancellation}, environment.resource) {}

    NGIN::Execution::ScheduleResult DeadlineState::Start(NGIN::Time::TimePoint deadline) noexcept
    {
        if (deadline <= NGIN::Time::MonotonicClock::Now())
        {
            Expire();
            Retire(true);
            return {};
        }
        auto timer = m_executor.ScheduleTimer(NGIN::Execution::WorkItem(TimerWork(this)), deadline);
        if (!timer)
            return std::unexpected(timer.error());
        m_timer = std::move(*timer);
        return {};
    }

    CancellationToken DeadlineState::Token() const noexcept
    {
        return m_cancellation.GetToken();
    }

    void DeadlineState::Expire() noexcept
    {
        if (!m_closed.exchange(true, std::memory_order_acq_rel))
            m_cancellation.Cancel(StopReason::Deadline);
    }

    void DeadlineState::Close() noexcept
    {
        m_closed.store(true, std::memory_order_release);
        m_timer.Cancel();
    }

    void DeadlineState::Retire(bool invoked) noexcept
    {
        if (!invoked && !m_closed.exchange(true, std::memory_order_acq_rel))
            m_cancellation.Cancel(StopReason::RuntimeShutdown);
        PromiseRuntimeCommon*     target = nullptr;
        NGIN::Execution::WorkItem resume;
        {
            std::lock_guard lock(m_mutex);
            assert(!m_retired);
            m_retired = true;
            target    = std::exchange(m_target, nullptr);
            resume    = std::move(m_resume);
        }
        // The owner joins this handoff before releasing storage. Nothing may
        // access this state after queueing a continuation that can destroy it.
        if (target)
            target->QueueContinuation(std::move(resume));
    }

    bool DeadlineState::IsRetired() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_retired;
    }

    bool DeadlineState::Install(PromiseRuntimeCommon* target, NGIN::Execution::WorkItem resume) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (m_retired)
            return false;
        assert(!m_target);
        m_target = target;
        m_resume = std::move(resume);
        return true;
    }

    std::expected<NGIN::Time::TimePoint, NGIN::Execution::ScheduleError> DeadlineAfter(double nanoseconds) noexcept
    {
        if (!std::isfinite(nanoseconds))
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        const auto now = NGIN::Time::MonotonicClock::Now();
        if (nanoseconds <= 0)
            return now;
        constexpr auto maximum = (std::numeric_limits<NGIN::UInt64>::max)();
        if (nanoseconds >= static_cast<double>(maximum))
            return NGIN::Time::TimePoint::FromNanoseconds(maximum);
        auto ticks = static_cast<NGIN::UInt64>(nanoseconds);
        if (static_cast<double>(ticks) < nanoseconds)
            ++ticks;
        return NGIN::Time::TimePoint::FromNanoseconds(ticks > maximum - now.ToNanoseconds() ? maximum : now.ToNanoseconds() + ticks);
    }
}// namespace NGIN::Async::detail
