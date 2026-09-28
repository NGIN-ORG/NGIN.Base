#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

namespace
{
    NGIN::Async::Task<int> ValueChild(NGIN::Async::TaskContext&)
    {
        co_return 42;
    }

    NGIN::Async::Task<void> VoidChild(NGIN::Async::TaskContext&)
    {
        co_return;
    }
}// namespace

TEST_CASE("StartsOn transfers the child placement to its parent", "[Async][Affinity]")
{
    NGIN::Execution::CooperativeScheduler parentScheduler;
    NGIN::Execution::CooperativeScheduler childScheduler;
    NGIN::Async::TaskContext              parentContext(parentScheduler);
    NGIN::Async::TaskContext              childContext(childScheduler);
    bool                                  continued = false;
    bool                                  useVoid   = false;
    SECTION("value task") {}
    SECTION("void task")
    {
        useVoid = true;
    }
    auto parent = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
        if (useVoid)
            co_await NGIN::Async::StartsOn(NGIN::Execution::ExecutorRef::From(childScheduler), VoidChild(childContext));
        else
            (void) co_await NGIN::Async::StartsOn(NGIN::Execution::ExecutorRef::From(childScheduler), ValueChild(childContext));
        continued = true;
        co_return 42;
    };
    auto operationTask = parent(parentContext);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(parentContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE_FALSE(operation.IsStopped());
    REQUIRE_FALSE(operation.HasError());
    childScheduler.RunUntilIdle();
    const bool resumedOnChildExecutor = continued;
    parentScheduler.RunUntilIdle();
    REQUIRE(resumedOnChildExecutor);
    REQUIRE(continued);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("Operation resumes its observer on the observer executor", "[Async][Affinity]")
{
    NGIN::Execution::CooperativeScheduler parentScheduler;
    NGIN::Execution::CooperativeScheduler childScheduler;
    NGIN::Async::TaskContext              parentContext(parentScheduler);
    NGIN::Async::TaskContext              childContext(childScheduler);
    bool                                  continued = false;
    bool                                  useVoid   = false;
    SECTION("value operation") {}
    SECTION("void operation")
    {
        useVoid = true;
    }
    auto parent = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
        NGIN::Async::TaskScope<> scope(parentContext.GetEnvironment(), 1);
        if (useVoid)
        {
            auto child = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(childScheduler), VoidChild(childContext)).value();
            (void) co_await child;
            (void) co_await scope.Join();
        }
        else
        {
            auto child = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(childScheduler), ValueChild(childContext)).value();
            (void) co_await child;
            (void) co_await scope.Join();
        }
        continued = true;
        co_return 42;
    };
    auto operationTask = parent(parentContext);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(parentContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    childScheduler.RunUntilIdle();
    const bool resumedOnChildExecutor = continued;
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(resumedOnChildExecutor);
    REQUIRE(continued);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("Moving an owned Operation awaiter preserves its child and selected executor", "[Async][Affinity][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler parentScheduler;
    NGIN::Execution::CooperativeScheduler childScheduler;
    NGIN::Async::TaskContext              parentContext(parentScheduler);
    NGIN::Async::TaskContext              childContext(childScheduler);
    bool                                  useVoid = false;
    SECTION("value operation") {}
    SECTION("void operation")
    {
        useVoid = true;
    }
    auto parent = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<bool> {
        NGIN::Async::TaskScope<> scope(parentContext.GetEnvironment(), 1);
        if (useVoid)
        {
            auto child  = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(childScheduler), VoidChild(childContext)).value();
            auto owned  = std::move(child).operator co_await();
            auto moved  = std::move(owned);
            auto result = co_await moved;
            (void) co_await scope.Join();
            co_return result.Succeeded();
        }
        auto child  = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(childScheduler), ValueChild(childContext)).value();
        auto owned  = std::move(child).operator co_await();
        auto moved  = std::move(owned);
        auto result = co_await moved;
        (void) co_await scope.Join();
        co_return result.Succeeded() && result.Value() == 42;
    };
    auto operationTask = parent(parentContext);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(parentContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    childScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    parentScheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value());
}
