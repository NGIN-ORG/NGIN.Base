#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    using namespace NGIN::Async;
    struct ReservedOnlyExecutor
    {
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint) noexcept
        {
            return Execute(std::move(work));
        }

        NGIN::Execution::detail::CompletionQueue completions {2};
        int                                      submissions {};
        auto                                     ReserveCompletion(NGIN::Execution::WorkItem work) noexcept { return completions.Reserve(std::move(work)); }
        NGIN::Execution::ScheduleResult          Execute(NGIN::Execution::WorkItem) noexcept
        {
            ++submissions;
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        }
    };
    Task<void> Join(TaskScope<>& scope)
    {
        (void) co_await scope.Join();
    }
}// namespace

TEST_CASE("Scoped execution reservation failure rejects before invoking a factory", "[Async][Scope][Admission]")
{
    NGIN::Execution::CooperativeScheduler scheduler(1);
    auto                                  executor = NGIN::Execution::ExecutorRef::From(scheduler);
    TaskScope                             scope(AsyncEnvironment {.executor = executor}, 1);
    bool                                  invoked  = false;
    auto                                  rejected = scope.Spawn([&](TaskContext&) {
        invoked = true;
        return Task<void>::FromCompletion(Completion<void, NoError>::Success());
    });
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    REQUIRE_FALSE(invoked);
    REQUIRE(scope.Pending() == 0);
    auto slot = executor.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE(slot);
}

TEST_CASE("Admitted scope startup uses its reserved delivery when general work is rejected", "[Async][Scope][Admission]")
{
    ReservedOnlyExecutor                  executor;
    NGIN::Execution::CooperativeScheduler observer;
    TaskContext                           context(observer);
    TaskScope                             scope(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(executor)}, 1);
    bool                                  started = false;
    auto                                  child   = scope.Spawn([&](TaskContext&) -> Task<int> {
        started = true;
        co_return 42;
    });
    REQUIRE(child);
    REQUIRE_FALSE(started);
    auto joinedTask = Join(scope);

    NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    observer.RunUntilIdle();
    while (executor.completions.RunOne()) {}
    observer.RunUntilIdle();
    REQUIRE(started);
    REQUIRE(executor.submissions == 0);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
    REQUIRE(child->TakeResult().Value() == 42);
    REQUIRE(scope.Pending() == 0);
}

TEST_CASE("Factory failure releases its execution reservation before retained diagnostics", "[Async][Scope][Admission]")
{
    ReservedOnlyExecutor                  executor;
    NGIN::Execution::CooperativeScheduler observer;
    TaskContext                           context(observer);
    auto                                  join = [](TaskScope<>& scope) -> Task<ScopeReport<NoError>> { co_return co_await scope.Join(); };
    std::optional<ScopeReport<NoError>>   report;
    {
        TaskScope scope(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(executor)}, 1);
        auto      child = scope.Spawn([](TaskContext&) -> Task<int> { throw std::runtime_error("factory failure"); });
        REQUIRE(child);
        auto joinedTask = join(scope);

        NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        observer.RunUntilIdle();
        while (executor.completions.RunOne()) {}
        observer.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        report.emplace(std::move(joined.TakeResult()).Value());
        REQUIRE(report->HasError());
        REQUIRE(executor.completions.Outstanding() == 0);
    }
    REQUIRE(report->Error().Fault().code == AsyncFaultCode::UnhandledException);
    REQUIRE(executor.completions.Outstanding() == 0);
}
