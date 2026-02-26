/// @file ExecutorRef.hpp
/// @brief Lightweight type-erased reference to an executor/scheduler.
#pragma once

#include <coroutine>
#include <concepts>
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
            requires requires(TScheduler& t, WorkItem item, NGIN::Time::TimePoint tp) {
                t.Execute(std::move(item));
                t.ExecuteAt(std::move(item), tp);
            }
        static constexpr ExecutorRef From(TScheduler& scheduler) noexcept
        {
            return ExecutorRef(
                    &scheduler,
                    +[](void* s, WorkItem item) noexcept {
                        auto* sched = static_cast<TScheduler*>(s);
                        sched->Execute(std::move(item));
                    },
                    +[](void* s, WorkItem item, NGIN::Time::TimePoint tp) {
                        auto* sched = static_cast<TScheduler*>(s);
                        sched->ExecuteAt(std::move(item), tp);
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

        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        void Execute(F&& job) const noexcept
        {
            Execute(WorkItem(std::forward<F>(job)));
        }

        void ExecuteAt(WorkItem item, NGIN::Time::TimePoint resumeAt) const
        {
            m_executeAt(m_self, std::move(item), resumeAt);
        }

        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        void ExecuteAt(F&& job, NGIN::Time::TimePoint resumeAt) const
        {
            ExecuteAt(WorkItem(std::forward<F>(job)), resumeAt);
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(WorkItem item, const TUnit& delay) const
        {
            const auto nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(delay).GetValue();
            if (nsDouble <= 0.0)
            {
                Execute(std::move(item));
                return;
            }

            const auto now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            auto       add = static_cast<NGIN::UInt64>(nsDouble);
            if (static_cast<double>(add) < nsDouble)
            {
                ++add;
            }

            ExecuteAt(std::move(item), NGIN::Time::TimePoint::FromNanoseconds(now + add));
        }

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

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(std::coroutine_handle<> coro, const TUnit& delay) const
        {
            ExecuteAfter(WorkItem(coro), delay);
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void ExecuteAfter(NGIN::Utilities::Callable<void()> job, const TUnit& delay) const
        {
            ExecuteAfter(WorkItem(std::move(job)), delay);
        }

    private:
        void*      m_self {nullptr};
        ExecuteFn  m_execute {nullptr};
        ExecuteAtFn m_executeAt {nullptr};
    };
}// namespace NGIN::Execution
