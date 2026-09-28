#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <memory>
#include <stdexcept>

TEST_CASE("Ready tasks can be run without an executor", "[Async][Ready]")
{
    NGIN::Async::TaskContext context(NGIN::Execution::ExecutorRef {});
    auto                     task = NGIN::Async::Task<std::unique_ptr<int>>::FromValue(std::make_unique<int>(42));
    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsStarted());
    auto moved = std::move(task);
    REQUIRE_FALSE(task.IsCompleted());
    REQUIRE(moved.IsCompleted());
    auto result = NGIN::Async::SyncWait(context, std::move(moved));
    REQUIRE(result.Succeeded());
    REQUIRE(*result.Value() == 42);
    REQUIRE_FALSE(moved.IsCompleted());
    REQUIRE(NGIN::Async::SyncWait(context, std::move(moved)).Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
    REQUIRE(NGIN::Async::SyncWait(context, NGIN::Async::Task<void>::FromValue()).Succeeded());
}

TEST_CASE("Ready child values compose and are consumed", "[Async][Ready]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    bool                                  reached = false;
    auto                                  parent  = [&]() -> NGIN::Async::Task<void> {
        auto child = NGIN::Async::Task<std::unique_ptr<int>>::FromValue(std::make_unique<int>(42));
        auto value = co_await child;
        REQUIRE(*value == 42);
        co_await NGIN::Async::Task<void>::FromValue();
        reached = true;
        (void) co_await child;
        FAIL("a consumed ready Task must not resume its second parent");
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(reached);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("Ready terminal outcomes propagate through both task specializations", "[Async][Ready]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const bool                            useVoid = GENERATE(false, true);
    const int                             channel = GENERATE(0, 1, 2);
    using Error                                   = std::unique_ptr<int>;
    auto make                                     = [&]<typename T>() {
        using C = NGIN::Async::Completion<T, Error>;
        if (channel == 0)
            return NGIN::Async::Task<T, Error>::FromCompletion(C::DomainFailure(std::make_unique<int>(7)));
        if (channel == 1)
            return NGIN::Async::Task<T, Error>::FromCompletion(C::Stopped());
        return NGIN::Async::Task<T, Error>::FromCompletion(C::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure)));
    };
    auto parent = [&]() -> NGIN::Async::Task<void, Error> {
        if (useVoid)
            co_await make.template operator()<void>();
        else
            (void) co_await make.template operator()<int>();
        FAIL("ready failure must skip the parent's remaining body");
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    if (channel == 0)
    {
        REQUIRE((result.HasError() && result.Error().IsDomainError()));
        REQUIRE(*result.Error().DomainError() == 7);
    }
    else if (channel == 1)
        REQUIRE(result.IsStopped());
    else
        REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure);
}

namespace
{
    struct ThrowOnMove
    {
        bool* throwing;
        explicit ThrowOnMove(bool& enabled) : throwing(&enabled) {}
        ThrowOnMove(ThrowOnMove&& other) : throwing(other.throwing)
        {
            if (*throwing)
                throw std::runtime_error("ready payload move");
        }
        ThrowOnMove(const ThrowOnMove&) = delete;
    };
}// namespace

TEST_CASE("Throwing ready payload moves become observable faults", "[Async][Ready]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const bool                            spawn    = GENERATE(false, true);
    bool                                  throwing = false;
    auto                                  task     = NGIN::Async::Task<ThrowOnMove>::FromValue(ThrowOnMove(throwing));
    throwing                                       = true;
    if (spawn)
    {
        NGIN::Async::TaskSupervisor<> operationRoot(rootContext.GetEnvironment(), 1);
        auto                          operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor),
                                                                        [&](NGIN::Async::TaskContext&) { return std::move(task); })
                                 .value();
        executor.RunUntilIdle();
        rootExecutor.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().Error().Fault().code == NGIN::Async::AsyncFaultCode::UnhandledException);
    }
    else
    {
        auto parent        = [&]() -> NGIN::Async::Task<void> { (void) co_await task; };
        auto operationTask = parent();

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

        auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
        executor.RunUntilIdle();
        rootExecutor.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().Error().Fault().code == NGIN::Async::AsyncFaultCode::UnhandledException);
    }
}

TEST_CASE("A canceled wait cannot replace an inline published result", "[Async][Ready]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::CancellationSource       cancellation;
    cancellation.Cancel();
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    NGIN::Async::TaskContext              waitContext(executor, cancellation.GetToken());
    auto                                  parent = [&]() -> NGIN::Async::Task<int> {
        auto empty = NGIN::Async::Task<void>::FromValue();
        co_await empty.WithCancellation(waitContext);
        auto value = NGIN::Async::Task<int>::FromValue(42);
        co_return co_await value.WithCancellation(waitContext);
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("Ready Task move assignment replaces and consumes payload ownership", "[Async][Ready]")
{
    NGIN::Async::TaskContext context(NGIN::Execution::ExecutorRef {});
    auto                     firstOwner  = std::make_shared<int>(1);
    auto                     secondOwner = std::make_shared<int>(2);
    auto                     first       = NGIN::Async::Task<std::shared_ptr<int>>::FromValue(firstOwner);
    auto                     second      = NGIN::Async::Task<std::shared_ptr<int>>::FromValue(secondOwner);
    REQUIRE(firstOwner.use_count() == 2);
    first = std::move(second);
    REQUIRE(firstOwner.use_count() == 1);
    REQUIRE_FALSE(second.IsCompleted());
    auto replacement = NGIN::Async::Task<std::shared_ptr<int>>::FromValue(firstOwner);
    replacement      = std::move(first);
    REQUIRE_FALSE(first.IsCompleted());
    REQUIRE(firstOwner.use_count() == 1);
    REQUIRE(*NGIN::Async::SyncWait(context, std::move(replacement)).Value() == 2);
    REQUIRE(secondOwner.use_count() == 1);
}

TEST_CASE("Scopes observe and retire ready children", "[Async][Ready][Scope]")
{
    NGIN::Execution::CooperativeScheduler executor;
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    NGIN::Async::TaskScope<int>           scope(NGIN::Execution::ExecutorRef::From(executor));
    const int                             channel  = GENERATE(0, 1, 2, 3);
    auto                                  owner    = std::make_shared<int>(42);
    std::weak_ptr<int>                    lifetime = owner;
    REQUIRE(scope.Spawn([owner = std::move(owner), channel](NGIN::Async::TaskContext&) {
        using C = NGIN::Async::Completion<void, int>;
        if (channel == 0)
            return NGIN::Async::Task<void, int>::FromValue();
        if (channel == 1)
            return NGIN::Async::Task<void, int>::FromCompletion(C::DomainFailure(*owner));
        if (channel == 2)
            return NGIN::Async::Task<void, int>::FromCompletion(C::Stopped());
        return NGIN::Async::Task<void, int>::FromCompletion(C::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure)));
    }));
    auto join       = [&]() -> NGIN::Async::Task<void, int> { (void) co_await scope.Join(); };
    auto joinedTask = join();

    NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(rootContext.GetEnvironment(), 1);

    auto joined = joinedRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(joinedTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(joined.IsCompleted());
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
    REQUIRE(scope.Pending() == 0);
    REQUIRE(lifetime.expired());
    auto result = scope.TakeResult();
    if (channel == 0)
        REQUIRE(result.Succeeded());
    else if (channel == 1)
        REQUIRE(result.Error().DomainError() == 42);
    else if (channel == 2)
        REQUIRE(result.IsStopped());
    else
        REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure);
}
