#include <NGIN/Async/CurrentEnvironment.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WithScope.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE("WithScope preserves body outcomes and joins on exit", "[Async][Scope]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    const int                             mode = GENERATE(0, 1, 2, 3);
    auto                                  body = [mode](TaskScope<int>&) -> Task<int, int> {
        if (mode == 1)
            co_return Completion<int, int>::DomainFailure(17);
        if (mode == 2)
            co_return Completion<int, int>::Stopped(StopReason::Deadline);
        if (mode == 3)
            throw std::runtime_error("body error");
        co_return 42;
    };
    auto operationTask = WithScope<int>(context.GetEnvironment(), 2, body);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    const auto result = operation.TakeResult();
    if (mode == 0)
        REQUIRE(result.Value() == 42);
    if (mode == 2)
        REQUIRE(result.StopReason() == StopReason::Deadline);
    if (mode == 1 || mode == 3)
    {
        REQUIRE(result.HasError());
        const auto& failure = result.Error().DomainError();
        REQUIRE(failure.bodyPrimary);
        REQUIRE(failure.SecondaryErrors().empty());
        if (mode == 1)
            REQUIRE(failure.Primary().DomainError() == 17);
        else
            REQUIRE(failure.Primary().IsFault());
    }
}

TEST_CASE("WithScope stops and retires children before releasing owned captures", "[Async][Scope][Lifetime]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    int                                   started  = 0;
    auto                                  lifetime = std::make_shared<int>(42);
    std::weak_ptr<int>                    weak     = lifetime;
    auto                                  body     = [owned = std::move(lifetime), &started](TaskScope<>& scope) -> Task<void> {
        auto admitted = scope.Spawn([owned, &started](TaskContext& child) -> Task<void> {
            ++started;
            co_await child.Delay(NGIN::Units::Seconds(60));
        });
        if (!admitted)
            throw std::runtime_error("child admission failed");
        co_return;
    };
    auto operationTask = WithScope(context.GetEnvironment(), 1, std::move(body));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().IsStopped());
    operation = {};
    REQUIRE(weak.expired());
    REQUIRE(scheduler.PendingTimers() == 0);
}

TEST_CASE("WithScope retains move-only errors and applies body precedence", "[Async][Scope][Lifetime]")
{
    using namespace NGIN::Async;
    using E = std::unique_ptr<int>;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    const bool                            bodyFails = GENERATE(false, true);
    auto                                  body      = [bodyFails](TaskScope<E>& scope) -> Task<int, E> {
        for (int code: {11, 22})
        {
            auto admitted = scope.Spawn([code](TaskContext&) -> Task<void, E> {
                co_await DomainFailure(std::make_unique<int>(code));
            });
            if (!admitted)
                throw std::runtime_error("child admission failed");
        }
        if (bodyFails)
            co_return Completion<int, E>::DomainFailure(std::make_unique<int>(99));
        co_return 42;
    };
    auto operationTask = WithScope<E>(context.GetEnvironment(), 2, body);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    const auto result = operation.TakeResult();
    operation         = {};
    REQUIRE(result.HasError());
    const auto& failure = result.Error().DomainError();
    REQUIRE(*failure.Primary().DomainError() == (bodyFails ? 99 : 11));
    REQUIRE(failure.SecondaryErrors().size() == (bodyFails ? 2 : 1));
    REQUIRE(*failure.SecondaryErrors().back().DomainError() == 22);
}

TEST_CASE("WithScope preserves the supplied environment in body and children", "[Async][Scope][Environment]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    int                                   metadata    = 42;
    auto                                  environment = context.GetEnvironment();
    environment.metadata                              = &metadata;
    environment.priority                              = 3;
    bool childCorrect                                 = false;
    auto body                                         = [&childCorrect, &metadata](TaskScope<>& scope) -> Task<void> {
        auto current = co_await CurrentEnvironment();
        if (current.metadata != &metadata || current.priority != 3 || current.cancellation != scope.GetCancellationToken())
            throw std::runtime_error("body environment mismatch");
        auto admitted = scope.Spawn([&childCorrect, &metadata](TaskContext& child) -> Task<void> {
            const auto inherited = child.GetEnvironment();
            childCorrect         = inherited.metadata == &metadata && inherited.priority == 3;
            co_return;
        });
        if (!admitted)
            throw std::runtime_error("child admission failed");
        (void) co_await scope.Join();
    };
    auto operationTask = WithScope(environment, 1, body);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(childCorrect);
}

TEST_CASE("WithScope joins live children across body exits and parent cancellation", "[Async][Scope][Lifetime]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext context(scheduler);
    CancellationSource cancellation;
    auto environment = context.GetEnvironment();
    environment.cancellation = cancellation.GetToken();
    const int mode = GENERATE(0, 1, 2, 3);
    bool childStarted = false;
    bool childDestroyed = false;
    struct Lifetime final
    {
        bool& destroyed;
        ~Lifetime() { destroyed = true; }
    };
    auto body = [&](TaskScope<int>& scope) -> Task<void, int> {
        auto child = scope.Spawn([&](TaskContext& childContext) -> Task<void, int> {
            Lifetime lifetime {childDestroyed};
            childStarted = true;
            co_await childContext.Delay(NGIN::Units::Seconds(60));
        });
        if (!child)
            throw std::runtime_error("child admission failed");
        TaskContext bodyContext(scope.GetEnvironment());
        while (!childStarted)
            co_await bodyContext.YieldNow();
        if (mode == 1)
            throw std::runtime_error("body exception with a live child");
        if (mode == 2)
            co_await DomainFailure(17);
        if (mode == 3)
            co_await bodyContext.Delay(NGIN::Units::Seconds(60));
        co_return;
    };
    auto task = WithScope<int>(environment, 1, body);
    TaskSupervisor<typename decltype(task)::ErrorType> owner(context.GetEnvironment(), 1);
    auto operation = owner.Spawn(std::move(task)).value();
    scheduler.RunUntilIdle();
    const bool wasPending = !operation.IsCompleted() && !childDestroyed;
    if (mode == 3)
    {
        cancellation.Cancel(StopReason::Requested);
        scheduler.RunUntilIdle();
    }
    REQUIRE(owner.TryJoin());
    REQUIRE(childStarted);
    REQUIRE(childDestroyed);
    REQUIRE(scheduler.PendingTimers() == 0);
    const auto result = operation.TakeResult();
    if (mode == 1 || mode == 2)
    {
        REQUIRE(result.HasError());
        const auto& failure = result.Error().DomainError();
        REQUIRE(failure.bodyPrimary);
        if (mode == 1)
            REQUIRE(failure.Primary().IsFault());
        else
            REQUIRE(failure.Primary().DomainError() == 17);
    }
    else
    {
        REQUIRE(result.IsStopped());
        REQUIRE(result.StopReason() == (mode == 3 ? StopReason::Requested : StopReason::OwnerClosed));
    }
    if (mode == 3)
        REQUIRE(wasPending);
}
