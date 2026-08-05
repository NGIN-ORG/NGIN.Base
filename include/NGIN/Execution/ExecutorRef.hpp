/// @file ExecutorRef.hpp
/// @brief Lightweight type-erased reference to an executor/scheduler.
#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Execution
{
    /// @brief Type-erased, non-owning executor reference.
    ///
    /// This is intended to replace a wide virtual scheduler interface for hot-path scheduling.
    class ExecutorRef final
    {
    public:
        /// @brief Type-erased immediate-execution callback.
        using ExecuteFn = void (*)(void*, WorkItem) noexcept;
        /// @brief Type-erased timed-execution callback.
        using ExecuteAtFn = void (*)(void*, WorkItem, NGIN::Time::TimePoint);

        /// @brief Constructs an invalid executor reference.
        constexpr ExecutorRef() noexcept = default;

        /// @brief Constructs a reference from borrowed state and dispatch callbacks.
        constexpr ExecutorRef(void* self, ExecuteFn execute, ExecuteAtFn executeAt) noexcept
            : m_self(self), m_execute(execute), m_executeAt(executeAt)
        {
        }

        /// @brief Creates a non-owning reference to a compatible scheduler.
        /// @warning The scheduler must outlive this reference and all dispatches through it.
        template<typename TScheduler>
            requires requires(TScheduler& t, WorkItem item, NGIN::Time::TimePoint tp) {
                t.Execute(std::move(item));
                t.ExecuteAt(std::move(item), tp);
            }
        static constexpr ExecutorRef From(TScheduler& scheduler) noexcept
        {
            return ExecutorRef(
                    &scheduler,
                    +[](void* s, WorkItem item) noexcept {
                        TScheduler* sched = static_cast<TScheduler*>(s);
                        sched->Execute(std::move(item));
                    },
                    +[](void* s, WorkItem item, NGIN::Time::TimePoint tp) {
                        TScheduler* sched = static_cast<TScheduler*>(s);
                        sched->ExecuteAt(std::move(item), tp);
                    });
        }

        /// @brief Returns whether state and both dispatch callbacks are present.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_self != nullptr && m_execute != nullptr && m_executeAt != nullptr;
        }

        /// @brief Submits a work item for immediate execution.
        /// @pre `IsValid()` is `true`.
        void Execute(WorkItem item) const noexcept
        {
            m_execute(m_self, std::move(item));
        }

        /// @brief Wraps an invocable object and submits it for immediate execution.
        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        void Execute(F&& job) const noexcept
        {
            Execute(WorkItem(std::forward<F>(job)));
        }

        /// @brief Submits a work item for execution at a monotonic time point.
        /// @pre `IsValid()` is `true`.
        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) const
        {
            m_executeAt(m_self, std::move(item), resumeAt);
        }

        /// @brief Wraps an invocable object and submits it for timed execution.
        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        void ExecuteAt(F&& job, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(std::forward<F>(job)), resumeAt);
        }

        /// @brief Submits a work item after a time-quantity delay.
        /// @details Non-positive delays are submitted immediately; positive fractional nanoseconds round up.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(WorkItem item, const TUnit& delay) const
        {
            const double nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(delay).GetValue();
            if (nsDouble <= 0.0)
            {
                Execute(std::move(item));
                return;
            }

            const NGIN::UInt64 now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            NGIN::UInt64       add = static_cast<NGIN::UInt64>(nsDouble);
            if (static_cast<double>(add) < nsDouble)
            {
                ++add;
            }

            ExecuteAt(std::move(item), NGIN::Time::TimePoint::FromNanoseconds(now + add));
        }

        /// @brief Wraps an invocable object and submits it after a delay.
        template<typename F, typename TUnit>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void> &&
                    NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(F&& job, const TUnit& delay) const
        {
            ExecuteAfter(WorkItem(std::forward<F>(job)), delay);
        }

        /// @brief Submits a coroutine continuation for immediate execution.
        void Execute(std::coroutine_handle<> coro) const noexcept
        {
            Execute(WorkItem(coro));
        }

        /// @brief Submits a type-erased job for immediate execution.
        void Execute(NGIN::Utilities::Callable<void()> job) const
        {
            Execute(WorkItem(std::move(job)));
        }

        /// @brief Submits a coroutine continuation for timed execution.
        void ExecuteAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(coro), resumeAt);
        }

        /// @brief Submits a type-erased job for timed execution.
        void ExecuteAt(NGIN::Utilities::Callable<void()> job, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(std::move(job)), resumeAt);
        }

        /// @brief Submits a coroutine continuation after a delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(std::coroutine_handle<> coro, const TUnit& delay) const
        {
            ExecuteAfter(WorkItem(coro), delay);
        }

        /// @brief Submits a type-erased job after a delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(NGIN::Utilities::Callable<void()> job, const TUnit& delay) const
        {
            ExecuteAfter(WorkItem(std::move(job)), delay);
        }

    private:
        void*       m_self {nullptr};
        ExecuteFn   m_execute {nullptr};
        ExecuteAtFn m_executeAt {nullptr};
    };
}// namespace NGIN::Execution
