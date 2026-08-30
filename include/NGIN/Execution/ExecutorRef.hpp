/// @file ExecutorRef.hpp
/// @brief Lightweight type-erased reference to an executor/scheduler.
#pragma once

#include <concepts>
#include <coroutine>
#include <limits>
#include <new>
#include <type_traits>

#include <NGIN/Execution/ScheduleResult.hpp>
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
        using ExecuteFn = ScheduleResult (*)(void*, WorkItem) noexcept;
        /// @brief Type-erased timed-execution callback.
        using ExecuteAtFn = ScheduleResult (*)(void*, WorkItem, NGIN::Time::TimePoint) noexcept;

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
                    +[](void* state, WorkItem item) noexcept -> ScheduleResult {
                        TScheduler* schedulerPointer = static_cast<TScheduler*>(state);
                        return schedulerPointer->Execute(std::move(item));
                    },
                    +[](void* state, WorkItem item, NGIN::Time::TimePoint timePoint) noexcept -> ScheduleResult {
                        TScheduler* schedulerPointer = static_cast<TScheduler*>(state);
                        return schedulerPointer->ExecuteAt(std::move(item), timePoint);
                    });
        }

        /// @brief Returns whether state and both dispatch callbacks are present.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_self != nullptr && m_execute != nullptr && m_executeAt != nullptr;
        }

        /// @brief Submits a work item for immediate execution.
        /// @return Success, or `InvalidExecutor` when this reference is unbound.
        [[nodiscard]] ScheduleResult Execute(WorkItem item) const noexcept
        {
            if (!IsValid())
                return std::unexpected(ScheduleError::InvalidExecutor);
            return m_execute(m_self, std::move(item));
        }

        /// @brief Wraps an invocable object and submits it for immediate execution.
        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        [[nodiscard]] ScheduleResult Execute(F&& job) const
        {
            try
            {
                return Execute(WorkItem(std::forward<F>(job)));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

        /// @brief Submits a work item for execution at a monotonic time point.
        /// @return Success, or `InvalidExecutor` when this reference is unbound.
        [[nodiscard]] ScheduleResult ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) const noexcept
        {
            if (!IsValid())
                return std::unexpected(ScheduleError::InvalidExecutor);
            return m_executeAt(m_self, std::move(item), resumeAt);
        }

        /// @brief Wraps an invocable object and submits it for timed execution.
        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        [[nodiscard]] ScheduleResult ExecuteAt(F&& job, NGIN::Time::TimePoint resumeAt) const
        {
            try
            {
                return ExecuteAt(WorkItem(std::forward<F>(job)), resumeAt);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

        /// @brief Submits a work item after a time-quantity delay.
        /// @details Non-positive delays are submitted immediately; positive fractional nanoseconds round up.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] ScheduleResult ExecuteAfter(WorkItem item, const TUnit& delay) const noexcept
        {
            const double nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(delay).GetValue();
            if (nsDouble <= 0.0)
            {
                return Execute(std::move(item));
            }

            const NGIN::UInt64 now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            NGIN::UInt64       add = static_cast<NGIN::UInt64>(nsDouble);
            if (static_cast<double>(add) < nsDouble)
            {
                ++add;
            }

            const NGIN::UInt64 maximum = (std::numeric_limits<NGIN::UInt64>::max)();
            const NGIN::UInt64 target  = add > maximum - now ? maximum : now + add;
            return ExecuteAt(std::move(item), NGIN::Time::TimePoint::FromNanoseconds(target));
        }

        /// @brief Wraps an invocable object and submits it after a delay.
        template<typename F, typename TUnit>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void> &&
                    NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] ScheduleResult ExecuteAfter(F&& job, const TUnit& delay) const
        {
            try
            {
                return ExecuteAfter(WorkItem(std::forward<F>(job)), delay);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

        /// @brief Submits a coroutine continuation for immediate execution.
        [[nodiscard]] ScheduleResult Execute(std::coroutine_handle<> coro) const noexcept
        {
            return Execute(WorkItem(coro));
        }

        /// @brief Submits a typed coroutine continuation without allocating callable storage.
        template<class Promise>
        [[nodiscard]] ScheduleResult Execute(std::coroutine_handle<Promise> coroutine) const noexcept
        {
            return Execute(std::coroutine_handle<> {coroutine});
        }

        /// @brief Submits a type-erased job for immediate execution.
        [[nodiscard]] ScheduleResult Execute(NGIN::Utilities::Callable<void()> job) const
        {
            if (!job)
                return std::unexpected(ScheduleError::Rejected);
            try
            {
                return Execute(WorkItem(std::move(job)));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

        /// @brief Submits a coroutine continuation for timed execution.
        [[nodiscard]] ScheduleResult ExecuteAt(std::coroutine_handle<> coro, NGIN::Time::TimePoint resumeAt) const noexcept
        {
            return ExecuteAt(WorkItem(coro), resumeAt);
        }

        /// @brief Submits a typed coroutine continuation for timed execution without callable storage.
        template<class Promise>
        [[nodiscard]] ScheduleResult ExecuteAt(
                std::coroutine_handle<Promise> coroutine,
                NGIN::Time::TimePoint          resumeAt) const noexcept
        {
            return ExecuteAt(std::coroutine_handle<> {coroutine}, resumeAt);
        }

        /// @brief Submits a type-erased job for timed execution.
        [[nodiscard]] ScheduleResult ExecuteAt(NGIN::Utilities::Callable<void()> job, NGIN::Time::TimePoint resumeAt) const
        {
            if (!job)
                return std::unexpected(ScheduleError::Rejected);
            try
            {
                return ExecuteAt(WorkItem(std::move(job)), resumeAt);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

        /// @brief Submits a coroutine continuation after a delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] ScheduleResult ExecuteAfter(std::coroutine_handle<> coro, const TUnit& delay) const noexcept
        {
            return ExecuteAfter(WorkItem(coro), delay);
        }

        /// @brief Submits a typed coroutine continuation after a delay without callable storage.
        template<class Promise, typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] ScheduleResult ExecuteAfter(
                std::coroutine_handle<Promise> coroutine,
                const TUnit&                   delay) const noexcept
        {
            return ExecuteAfter(std::coroutine_handle<> {coroutine}, delay);
        }

        /// @brief Submits a type-erased job after a delay.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] ScheduleResult ExecuteAfter(NGIN::Utilities::Callable<void()> job, const TUnit& delay) const
        {
            if (!job)
                return std::unexpected(ScheduleError::Rejected);
            try
            {
                return ExecuteAfter(WorkItem(std::move(job)), delay);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }

    private:
        void*       m_self {nullptr};
        ExecuteFn   m_execute {nullptr};
        ExecuteAtFn m_executeAt {nullptr};
    };
}// namespace NGIN::Execution
