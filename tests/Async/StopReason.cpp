#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/WithScope.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
    using namespace NGIN::Async;
    Task<void> Wait(TaskContext& context)
    {
        co_await context.Delay(NGIN::Units::Seconds(60));
    }
}// namespace

TEST_CASE("The first stop reason survives linked cancellation and checkpoints", "[Async][Cancellation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    const auto                            reason = GENERATE(StopReason::Requested, StopReason::Deadline, StopReason::OwnerClosed, StopReason::RuntimeShutdown);
    const bool                            early  = GENERATE(false, true);
    CancellationSource                    source;
    LinkedCancellationSource              linked({source.GetToken(), source.GetToken()});
    LinkedCancellationSource              nested({linked.GetToken()});
    TaskContext                           context(NGIN::Execution::ExecutorRef::From(scheduler), nested.GetToken());
    auto                                  operationTask = Wait(context);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    if (early)
        source.Cancel(reason);
    scheduler.RunUntilIdle();
    if (!early)
    {
        REQUIRE_FALSE(operation.IsCompleted());
        source.Cancel(reason);
    }
    source.Cancel(StopReason::Requested);
    linked.Cancel(StopReason::Requested);
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().StopReason() == reason);
    REQUIRE(nested.GetToken().GetStopReason() == reason);
    LinkedCancellationSource late({source.GetToken()});
    REQUIRE(late.GetToken().GetStopReason() == reason);
    REQUIRE(scheduler.PendingTimers() == 0);
}

TEST_CASE("WithScope closure reports OwnerClosed to unfinished children", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    auto                                  body = [](TaskScope<>& scope) -> Task<void> {
        auto child = scope.Spawn([](TaskContext& context) { return Wait(context); });
        if (!child)
            throw std::runtime_error("admission");
        co_return;
    };
    auto operationTask = WithScope(context.GetEnvironment(), 1, body);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().StopReason() == StopReason::OwnerClosed);
}

TEST_CASE("Void WhenAll forwards a child's stopped reason", "[Async][Cancellation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    const auto                            reason        = GENERATE(StopReason::Requested, StopReason::Deadline, StopReason::OwnerClosed, StopReason::RuntimeShutdown);
    auto                                  child         = Task<void>::FromCompletion(Completion<void, NoError>::Stopped(reason));
    auto                                  operationTask = WhenAll(context, std::move(child));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().StopReason() == reason);
}
