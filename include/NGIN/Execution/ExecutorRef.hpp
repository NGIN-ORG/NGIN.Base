/// @file ExecutorRef.hpp
/// @brief Lightweight type-erased reference to an executor/scheduler.
#pragma once

#include <coroutine>

#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Execution
{
    /// @brief Type-erased, non-owning executor reference.
    ///
    /// This is intended to replace a wide virtual scheduler interface for hot-path scheduling.
    class ExecutorRef final
    {
    public:
        using ScheduleFn   = void (*)(void*, std::coroutine_handle<>) noexcept;
        using ScheduleAtFn = void (*)(void*, std::coroutine_handle<>, NGIN::Time::TimePoint);

        constexpr ExecutorRef() noexcept = default;

        constexpr ExecutorRef(void* self, ScheduleFn schedule, ScheduleAtFn scheduleAt) noexcept
            : m_self(self)
            , m_schedule(schedule)
            , m_scheduleAt(scheduleAt)
        {
        }

        template<typename TScheduler>
        static constexpr ExecutorRef From(TScheduler& scheduler) noexcept
        {
            return ExecutorRef(
                    &scheduler,
                    +[](void* s, std::coroutine_handle<> h) noexcept {
                        static_cast<TScheduler*>(s)->Schedule(h);
                    },
                    +[](void* s, std::coroutine_handle<> h, NGIN::Time::TimePoint tp) {
                        static_cast<TScheduler*>(s)->ScheduleAt(h, tp);
                    });
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_self != nullptr && m_schedule != nullptr && m_scheduleAt != nullptr;
        }

        void Schedule(std::coroutine_handle<> coro) const noexcept
        {
            m_schedule(m_self, coro);
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) const
        {
            m_scheduleAt(m_self, coro, resumeAt);
        }

    private:
        void*        m_self {nullptr};
        ScheduleFn   m_schedule {nullptr};
        ScheduleAtFn m_scheduleAt {nullptr};
    };
}// namespace NGIN::Execution

