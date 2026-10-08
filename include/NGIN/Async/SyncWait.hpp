/// @file SyncWait.hpp
/// @brief Blocking root entry using structured task ownership.
#pragma once
#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>
#include <condition_variable>

namespace NGIN::Async
{
    namespace detail
    {
        /// One-shot root retirement signal owned by the blocked SyncWait caller.
        /// The caller destroys it as soon as Wait returns, so Notify must not touch
        /// the object after publishing completion.
        class RootRetirementSignal final
        {
        public:
            static void Notify(void* raw) noexcept
            {
                auto& self = *static_cast<RootRetirementSignal*>(raw);
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
                // The exchange is the final access to this object. A wake that races
                // the caller's return only names the stack address; at worst it is a
                // spurious wakeup, which every address-based waiter tolerates.
                UInt32* const address = &self.m_state;
                if (std::atomic_ref<UInt32>(self.m_state).exchange(Done, std::memory_order_acq_rel) == Sleeping)
                    NGIN::Sync::detail::AtomicConditionNotifyOne(*address);
#else
                std::lock_guard lock(self.m_mutex);
                self.m_done = true;
                self.m_condition.notify_one();
#endif
            }

            void Wait() noexcept
            {
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
                std::atomic_ref<UInt32> state(m_state);
                UInt32                  observed = Pending;
                if (!state.compare_exchange_strong(observed, Sleeping, std::memory_order_acquire) && observed == Done)
                    return;
                while (state.load(std::memory_order_acquire) != Done)
                    NGIN::Sync::detail::AtomicConditionWait(m_state, Sleeping);
#else
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [this] { return m_done; });
#endif
            }

        private:
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            static constexpr UInt32 Pending  = 0;
            static constexpr UInt32 Sleeping = 1;
            static constexpr UInt32 Done     = 2;
            alignas(std::atomic_ref<UInt32>::required_alignment) UInt32 m_state {Pending};
#else
            std::mutex              m_mutex;
            std::condition_variable m_condition;
            bool                    m_done {};
#endif
        };
    }// namespace detail

    /// @brief Runs a cold root and joins its execution before returning the completion.
    /// @details The executor must be driven independently. Do not block an executor
    /// thread needed by this root. Ready inputs require no executor or owner allocation.
    template<typename T, typename E>
    [[nodiscard]] Completion<T, E> SyncWait(TaskContext& ctx, Task<T, E>&& task)
    {
        using Result = Completion<T, E>;
        try
        {
            if (task.m_ready || !task.m_handle)
                return task.AsCompletion().await_resume();

            Task<T, E> input(std::move(task));

            detail::RootRetirementSignal retirement;
            TaskScope<E>                 owner(ctx.GetEnvironment(), 1);
            detail::RootScopeAccess::OnRetirement(owner, &detail::RootRetirementSignal::Notify, &retirement);
            auto admitted = owner.Spawn(std::move(input));
            if (!admitted)
            {
                if (admitted.error() == NGIN::Execution::ScheduleError::Stopped)
                    return Result::Stopped(ctx.GetCancellationToken().GetStopReason());
                return Result::Faulted(detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, admitted.error()));
            }
            owner.Close();
            retirement.Wait();
            // No producer callback retains owner storage after the notification.
            // Finalize the already-retired scope on this thread, including captures.
            detail::RootScopeAccess::JoinRetired(owner);
            return admitted->TakeResult();
        } catch (const std::bad_alloc&)
        {
            return Result::Faulted(detail::MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed,
                                                               NGIN::Execution::ScheduleError::ResourceExhausted));
        } catch (...)
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            return Result::Faulted(std::move(fault));
        }
    }
}// namespace NGIN::Async
