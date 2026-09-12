#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Task resumes its parent on the parent executor after external work", "[Async][Affinity]")
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
            co_await VoidChild(childContext);
        else
            (void) co_await ValueChild(childContext);
        continued = true;
        co_return 42;
    };
    auto operation = NGIN::Async::Spawn(parentContext, parent(parentContext));
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE_FALSE(operation.IsCanceled());
    REQUIRE_FALSE(operation.IsFaulted());
    childScheduler.RunUntilIdle();
    const bool resumedOnChildExecutor = continued;
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(resumedOnChildExecutor);
    REQUIRE(continued);
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
        if (useVoid)
        {
            auto child = NGIN::Async::Spawn(childContext, VoidChild(childContext));
            (void) co_await child;
        }
        else
        {
            auto child = NGIN::Async::Spawn(childContext, ValueChild(childContext));
            (void) co_await child;
        }
        continued = true;
        co_return 42;
    };
    auto operation = NGIN::Async::Spawn(parentContext, parent(parentContext));
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    childScheduler.RunUntilIdle();
    const bool resumedOnChildExecutor = continued;
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(resumedOnChildExecutor);
    REQUIRE(continued);
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
        if (useVoid)
        {
            auto child  = NGIN::Async::Spawn(childContext, VoidChild(childContext));
            auto owned  = std::move(child).operator co_await();
            auto moved  = std::move(owned);
            auto result = co_await moved;
            co_return result.Succeeded();
        }
        auto child  = NGIN::Async::Spawn(childContext, ValueChild(childContext));
        auto owned  = std::move(child).operator co_await();
        auto moved  = std::move(owned);
        auto result = co_await moved;
        co_return result.Succeeded() && result.Value() == 42;
    };
    auto operation = NGIN::Async::Spawn(parentContext, parent(parentContext));
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    childScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    parentScheduler.RunUntilIdle();
    REQUIRE(operation.TakeResult().Value());
}
