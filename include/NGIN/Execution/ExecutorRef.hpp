/// @file ExecutorRef.hpp
/// @brief Lightweight type-erased reference to an executor/scheduler.
#pragma once

#include <coroutine>
#include <stdexcept>

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Execution
{
    /// @brief Type-erased, non-owning executor reference.
    ///
    /// This is intended to replace a wide virtual scheduler interface for hot-path scheduling.
    class ExecutorRef final
    {
    public:
        using ExecuteFn   = void (*)(void*, WorkItem) noexcept;
        using ExecuteAtFn = void (*)(void*, WorkItem, NGIN::Time::TimePoint);

        constexpr ExecutorRef() noexcept = default;

        constexpr ExecutorRef(void* self, ExecuteFn execute, ExecuteAtFn executeAt) noexcept
            : m_self(self)
            , m_execute(execute)
            , m_executeAt(executeAt)
        {
        }

        template<typename TScheduler>
        static constexpr ExecutorRef From(TScheduler& scheduler) noexcept
        {
            return ExecutorRef(
                    &scheduler,
                    +[](void* s, WorkItem item) noexcept {
                        auto* sched = static_cast<TScheduler*>(s);
                        if constexpr (requires(TScheduler& t, WorkItem w) { t.Execute(std::move(w)); })
                        {
                            sched->Execute(std::move(item));
                        }
                        else
                        {
                            if (item.IsCoroutine())
                            {
                                sched->Schedule(item.GetCoroutine());
                            }
                            else
                            {
                                item.Invoke();
                            }
                        }
                    },
                    +[](void* s, WorkItem item, NGIN::Time::TimePoint tp) {
                        auto* sched = static_cast<TScheduler*>(s);
                        if constexpr (requires(TScheduler& t, WorkItem w, NGIN::Time::TimePoint p) { t.ExecuteAt(std::move(w), p); })
                        {
                            sched->ExecuteAt(std::move(item), tp);
                        }
                        else
                        {
                            if (item.IsCoroutine())
                            {
                                sched->ScheduleAt(item.GetCoroutine(), tp);
                            }
                            else
                            {
                                throw std::runtime_error("NGIN::Execution::ExecutorRef: scheduler lacks ExecuteAt(job)");
                            }
                        }
                    });
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_self != nullptr && m_execute != nullptr && m_executeAt != nullptr;
        }

        void Execute(WorkItem item) const noexcept
        {
            m_execute(m_self, std::move(item));
        }

        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) const
        {
            m_executeAt(m_self, std::move(item), resumeAt);
        }

        void Execute(std::coroutine_handle<> coro) const noexcept
        {
            Execute(WorkItem(coro));
        }

        void Execute(NGIN::Utilities::Callable<void()> job) const
        {
            Execute(WorkItem(std::move(job)));
        }

        void ExecuteAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(coro), resumeAt);
        }

        void ExecuteAt(NGIN::Utilities::Callable<void()> job, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(std::move(job)), resumeAt);
        }

        // Compatibility shims for existing coroutine-only call sites.
        void Schedule(std::coroutine_handle<> coro) const noexcept
        {
            Execute(coro);
        }

        void ScheduleAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(coro, resumeAt);
        }

    private:
        void*      m_self {nullptr};
        ExecuteFn  m_execute {nullptr};
        ExecuteAtFn m_executeAt {nullptr};
    };
}// namespace NGIN::Execution
