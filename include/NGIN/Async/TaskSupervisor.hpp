/// @file TaskSupervisor.hpp
/// @brief Bounded application-lifetime ownership and explicit error reporting.
#pragma once
#include <NGIN/Async/TaskScope.hpp>

namespace NGIN::Async
{
    /// Reuses retired child slots while owning every admitted task through cleanup.
    /// Destruction with live work fails fast; the application explicitly joins or
    /// shuts down this owner while continuing to drive its executor.
    template<typename E = NoError>
    class TaskSupervisor final
    {
    public:
        struct ErrorSink final
        {
            /// Accepts a retained immutable error. Return true only when reporting
            /// succeeded or ownership was accepted; false retains it for shutdown.
            /// Must be noexcept, nonblocking, and safe on the owner's executor.
            bool (*report)(void*, AsyncError<E>) noexcept {};
            void* context {};
        };

        explicit TaskSupervisor(AsyncEnvironment environment, std::size_t capacity = 256, ErrorSink sink = {})
            : m_scope(std::move(environment), capacity)
        {
            detail::SupervisorAccess::Configure(m_scope, sink.report, sink.context);
        }
        TaskSupervisor(const TaskSupervisor&)            = delete;
        TaskSupervisor& operator=(const TaskSupervisor&) = delete;

        template<typename Work>
        [[nodiscard]] auto Spawn(Work&& work)
        {
            return m_scope.Spawn(std::forward<Work>(work));
        }
        template<typename Work>
        [[nodiscard]] auto SpawnOn(NGIN::Execution::ExecutorRef executor, Work&& work)
        {
            return m_scope.SpawnOn(executor, std::forward<Work>(work));
        }

        /// Transfers a factory/cold task to this application owner and relinquishes
        /// result interest. Errors still reach the sink or retained shutdown report.
        template<typename Work>
        [[nodiscard]] NGIN::Execution::ScheduleResult Transfer(Work&& work)
        {
            auto admitted = Spawn(std::forward<Work>(work));
            if (!admitted)
                return std::unexpected(admitted.error());
            return {};
        }

        void               Close() noexcept { m_scope.Close(); }
        void               RequestStop(StopReason reason = StopReason::Requested) noexcept { m_scope.RequestStop(reason); }
        [[nodiscard]] auto Join() { return m_scope.Join(); }
        /// Closes admission and returns a report only when cleanup has finished.
        /// Event-loop roots can poll this without installing an asynchronous waiter.
        /// Do not combine it with an outstanding Join/Shutdown awaiter.
        [[nodiscard]] std::optional<ScopeReport<E>> TryJoin()
        {
            Close();
            if (Pending() != 0)
                return {};
            auto join = m_scope.Join();
            if (!join.await_ready())
                std::terminate();
            return join.await_resume();
        }

        /// Closes admission, requests runtime-shutdown stop, and asynchronously joins.
        [[nodiscard]] auto Shutdown()
        {
            Close();
            RequestStop(StopReason::RuntimeShutdown);
            return Join();
        }
        [[nodiscard]] ScopeReport<E>    TakeResult() { return m_scope.TakeResult(); }
        [[nodiscard]] std::size_t       Pending() const noexcept { return m_scope.Pending(); }
        [[nodiscard]] AsyncEnvironment  GetEnvironment() const noexcept { return m_scope.GetEnvironment(); }
        [[nodiscard]] CancellationToken GetCancellationToken() const noexcept { return m_scope.GetCancellationToken(); }

    private:
        TaskScope<E> m_scope;
    };
}// namespace NGIN::Async
