#include <NGIN/Async/FirstCompleted.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <thread>

namespace
{
    using namespace NGIN::Async;
    using NGIN::Execution::CooperativeScheduler;
    template<typename E>
    Task<void> Join(TaskScope<E>& scope)
    {
        (void) co_await scope.Join();
    }
    Task<int> Value(int value)
    {
        co_return value;
    }

    struct Resource final : std::pmr::memory_resource
    {
        std::size_t attempts {}, live {}, failAt = std::numeric_limits<std::size_t>::max();
        void*       do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (attempts++ == failAt)
                throw std::bad_alloc();
            auto* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
            ++live;
            return pointer;
        }
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
        {
            --live;
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };
    Task<CompletionNotification> FailObservation(std::span<const OperationRef> refs, Resource& resource,
                                                 std::size_t offset, CancellationToken token)
    {
        resource.failAt = resource.attempts + offset;
        co_return co_await FirstCompleted(refs, std::move(token)).AsCompletion();
    }
}// namespace

TEST_CASE("FirstCompleted preserves consumed results and precompleted input order", "[Async][Observation]")
{
    CooperativeScheduler        scheduler;
    TaskContext                 context(scheduler);
    std::array<OperationRef, 2> refs;
    {
        TaskScope scope(context.GetEnvironment(), 2);
        auto      first  = scope.Spawn(Value(11)).value();
        auto      second = scope.Spawn(Value(22)).value();
        refs             = {second.Observe(), first.Observe()};
        auto joinedTask  = Join(scope);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE(joined.TakeResult().Succeeded());
        REQUIRE(second.TakeResult().Value() == 22);
        REQUIRE(first.TakeResult().Value() == 11);
    }
    CancellationSource cancellation;
    cancellation.Cancel(StopReason::Deadline);
    auto observerTask = FirstCompleted(refs, cancellation.GetToken());

    TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(context.GetEnvironment(), 1);

    auto observer = observerRoot.Spawn(std::move(observerTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(observerRoot.TryJoin());
    auto result = observer.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().index == 0);
    REQUIRE(result.Value().kind == CompletionKind::Value);
    REQUIRE(refs[1].Kind() == CompletionKind::Value);
}

TEST_CASE("FirstCompleted selects publication before observer dispatch", "[Async][Observation]")
{
    CooperativeScheduler firstExecutor, secondExecutor, observerExecutor;
    TaskContext          firstContext(firstExecutor), secondContext(secondExecutor), observerContext(observerExecutor);
    TaskScope            firstScope(firstContext.GetEnvironment(), 1), secondScope(secondContext.GetEnvironment(), 1);
    auto                 first  = firstScope.Spawn(Value(11)).value();
    auto                 second = secondScope.Spawn(Value(22)).value();
    std::array           refs {first.Observe(), second.Observe()};
    auto                 observerTask = FirstCompleted(refs);

    TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(observerContext.GetEnvironment(), 1);

    auto observer = observerRoot.Spawn(std::move(observerTask)).value();
    observerExecutor.RunUntilIdle();
    auto firstJoinTask = Join(firstScope);

    TaskSupervisor<typename decltype(firstJoinTask)::ErrorType> firstJoinRoot(firstContext.GetEnvironment(), 1);

    auto firstJoin      = firstJoinRoot.Spawn(std::move(firstJoinTask)).value();
    auto secondJoinTask = Join(secondScope);

    TaskSupervisor<typename decltype(secondJoinTask)::ErrorType> secondJoinRoot(secondContext.GetEnvironment(), 1);

    auto secondJoin = secondJoinRoot.Spawn(std::move(secondJoinTask)).value();
    secondExecutor.RunUntilIdle();
    firstExecutor.RunUntilIdle();
    REQUIRE_FALSE(observer.IsCompleted());
    observerExecutor.RunUntilIdle();
    REQUIRE(observerRoot.TryJoin());
    REQUIRE(observer.TakeResult().Value().index == 1);
    REQUIRE(first.TakeResult().Value() == 11);
    REQUIRE(second.TakeResult().Value() == 22);
    REQUIRE(firstJoinRoot.TryJoin());
    REQUIRE(firstJoin.TakeResult().Succeeded());
    REQUIRE(secondJoinRoot.TryJoin());
    REQUIRE(secondJoin.TakeResult().Succeeded());
}

TEST_CASE("FirstCompleted cancellation stops only observation and retires duplicate records", "[Async][Observation][Lifetime]")
{
    CooperativeScheduler worker, observerExecutor;
    TaskContext          workerContext(worker), observerContext(observerExecutor);
    TaskScope            scope(workerContext.GetEnvironment(), 1);
    auto                 child = scope.Spawn(Value(42)).value();
    std::array           refs {child.Observe(), child.Observe()};
    CancellationSource   cancellation;
    auto                 observerTask = FirstCompleted(refs, cancellation.GetToken());

    TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(observerContext.GetEnvironment(), 1);

    auto observer = observerRoot.Spawn(std::move(observerTask)).value();
    observerExecutor.RunUntilIdle();
    cancellation.Cancel(StopReason::Deadline);
    observerExecutor.RunUntilIdle();
    REQUIRE(observerRoot.TryJoin());
    auto result = observer.TakeResult();
    REQUIRE(result.IsStopped());
    REQUIRE(result.StopReason() == StopReason::Deadline);
    REQUIRE_FALSE(child.IsCompleted());
    REQUIRE_FALSE(scope.GetCancellationToken().IsCancellationRequested());
    observer                = {};
    auto secondObserverTask = FirstCompleted(refs);

    TaskSupervisor<typename decltype(secondObserverTask)::ErrorType> secondObserverRoot(observerContext.GetEnvironment(), 1);

    auto secondObserver = secondObserverRoot.Spawn(std::move(secondObserverTask)).value();
    observerExecutor.RunUntilIdle();
    auto joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(workerContext.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    worker.RunUntilIdle();
    observerExecutor.RunUntilIdle();
    REQUIRE(secondObserverRoot.TryJoin());
    REQUIRE(secondObserver.TakeResult().Value().index < 2);
    REQUIRE(child.TakeResult().Value() == 42);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
}

TEST_CASE("FirstCompleted handles simultaneous notifications", "[Async][Observation][Concurrency]")
{
    for (int iteration = 0; iteration != 64; ++iteration)
    {
        CooperativeScheduler firstExecutor, secondExecutor, observerExecutor;
        TaskContext          firstContext(firstExecutor), secondContext(secondExecutor), observerContext(observerExecutor);
        TaskScope            firstScope(firstContext.GetEnvironment(), 1), secondScope(secondContext.GetEnvironment(), 1);
        auto                 first  = firstScope.Spawn(Value(11)).value();
        auto                 second = secondScope.Spawn(Value(22)).value();
        std::array           refs {first.Observe(), second.Observe()};
        auto                 observerTask = FirstCompleted(refs);

        TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(observerContext.GetEnvironment(), 1);

        auto observer = observerRoot.Spawn(std::move(observerTask)).value();
        observerExecutor.RunUntilIdle();
        auto firstJoinTask = Join(firstScope);

        TaskSupervisor<typename decltype(firstJoinTask)::ErrorType> firstJoinRoot(firstContext.GetEnvironment(), 1);

        auto firstJoin      = firstJoinRoot.Spawn(std::move(firstJoinTask)).value();
        auto secondJoinTask = Join(secondScope);

        TaskSupervisor<typename decltype(secondJoinTask)::ErrorType> secondJoinRoot(secondContext.GetEnvironment(), 1);

        auto        secondJoin = secondJoinRoot.Spawn(std::move(secondJoinTask)).value();
        std::thread firstThread([&] { firstExecutor.RunUntilIdle(); });
        std::thread secondThread([&] { secondExecutor.RunUntilIdle(); });
        while (!observer.IsCompleted())
            observerExecutor.RunUntilIdle();
        firstThread.join();
        secondThread.join();
        observerExecutor.RunUntilIdle();
        REQUIRE(observerRoot.TryJoin());
        REQUIRE(observer.TakeResult().Value().index < 2);
        REQUIRE(first.TakeResult().Value() == 11);
        REQUIRE(second.TakeResult().Value() == 22);
        REQUIRE(firstJoinRoot.TryJoin());
        REQUIRE(firstJoin.TakeResult().Succeeded());
        REQUIRE(secondJoinRoot.TryJoin());
        REQUIRE(secondJoin.TakeResult().Succeeded());
    }
}

TEST_CASE("FirstCompleted validates all references before selecting a ready input", "[Async][Observation]")
{
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    TaskScope            scope(context.GetEnvironment(), 1);
    auto                 child      = scope.Spawn(Value(42)).value();
    auto                 joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
    std::array refs {child.Observe(), OperationRef {}};
    const bool empty        = GENERATE(false, true);
    auto       observerTask = FirstCompleted(empty ? std::span<const OperationRef> {} : refs);

    TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(context.GetEnvironment(), 1);

    auto observer = observerRoot.Spawn(std::move(observerTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(observerRoot.TryJoin());
    REQUIRE(observer.TakeResult().Error().Fault().code == AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("FirstCompleted reports terminal error and stop kinds without consuming", "[Async][Observation]")
{
    const bool           stopped = GENERATE(false, true);
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    TaskScope<int>       scope(context.GetEnvironment(), 1);
    auto                 child = scope.Spawn(Task<void, int>::FromCompletion(stopped
                                                                                     ? Completion<void, int>::Stopped(StopReason::Deadline)
                                                                                     : Completion<void, int>::DomainFailure(23)))
                         .value();
    std::array refs {child.Observe()};
    auto       observerTask = FirstCompleted(refs);

    TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(context.GetEnvironment(), 1);

    auto observer   = observerRoot.Spawn(std::move(observerTask)).value();
    auto joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(observerRoot.TryJoin());
    REQUIRE(observer.TakeResult().Value().kind == (stopped ? CompletionKind::Stopped : CompletionKind::Error));
    auto result = child.TakeResult();
    if (stopped)
        REQUIRE(result.StopReason() == StopReason::Deadline);
    else
        REQUIRE(result.Error().DomainError() == 23);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
}

TEST_CASE("FirstCompleted rolls back boundary allocation failures", "[Async][Observation][Allocation]")
{
    const std::size_t offset = GENERATE(0u, 1u, 2u);
    Resource          resource;
    {
        CooperativeScheduler worker, observerExecutor;
        TaskContext          workerContext(worker);
        TaskContext          observerContext(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(observerExecutor), .resource = &resource});
        TaskScope            scope(workerContext.GetEnvironment(), 1);
        auto                 child = scope.Spawn(Value(42)).value();
        std::array           refs {child.Observe()};
        CancellationSource   cancellation(&resource);
        const auto           baseline = resource.live;
        {
            auto observerTask = FailObservation(refs, resource, offset, cancellation.GetToken());

            TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(observerContext.GetEnvironment(), 1);

            auto observer = observerRoot.Spawn(std::move(observerTask)).value();
            observerExecutor.RunUntilIdle();
            REQUIRE(observerRoot.TryJoin());
            auto result = observer.TakeResult();
            REQUIRE(result.HasError());
            REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
            observer = {};
        }
        REQUIRE(resource.live == baseline);
        auto joinedTask = Join(scope);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(workerContext.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        worker.RunUntilIdle();
        REQUIRE(child.TakeResult().Value() == 42);
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE(joined.TakeResult().Succeeded());
    }
    REQUIRE(resource.live == 0);
}

TEST_CASE("FirstCompleted joins callbacks racing cancellation with terminal publication", "[Async][Observation][Concurrency]")
{
    for (int iteration = 0; iteration != 64; ++iteration)
    {
        CooperativeScheduler worker, observerExecutor;
        TaskContext          workerContext(worker), observerContext(observerExecutor);
        TaskScope            scope(workerContext.GetEnvironment(), 1);
        auto                 child = scope.Spawn(Value(42)).value();
        std::array           refs {child.Observe()};
        CancellationSource   cancellation;
        auto                 observerTask = FirstCompleted(refs, cancellation.GetToken());

        TaskSupervisor<typename decltype(observerTask)::ErrorType> observerRoot(observerContext.GetEnvironment(), 1);

        auto observer = observerRoot.Spawn(std::move(observerTask)).value();
        observerExecutor.RunUntilIdle();
        auto joinedTask = Join(scope);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(workerContext.GetEnvironment(), 1);

        auto        joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        std::thread workerThread([&] { worker.RunUntilIdle(); });
        std::thread cancelThread([&] { cancellation.Cancel(StopReason::Deadline); });
        while (!observer.IsCompleted())
            observerExecutor.RunUntilIdle();
        workerThread.join();
        cancelThread.join();
        REQUIRE(observerRoot.TryJoin());
        auto result = observer.TakeResult();
        REQUIRE_FALSE(result.HasError());
        if (result.IsStopped())
            REQUIRE(result.StopReason() == StopReason::Deadline);
        else
            REQUIRE(result.Value().index == 0);
        REQUIRE(child.TakeResult().Value() == 42);
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE(joined.TakeResult().Succeeded());
        REQUIRE_FALSE(scope.GetCancellationToken().IsCancellationRequested());
    }
}

TEST_CASE("FirstCompleted uses admitted delivery when observer capacity is exhausted", "[Async][Observation][Admission]")
{
    const bool cancelWait = GENERATE(false, true);
    CooperativeScheduler producerExecutor, observerExecutor(2);
    TaskContext producerContext(producerExecutor), observerContext(observerExecutor);
    TaskSupervisor<> producer(producerContext.GetEnvironment(), 1);
    TaskSupervisor<> observerOwner(observerContext.GetEnvironment(), 1);
    CancellationSource cancellation;
    auto source = producer.Spawn(Value(42)).value();
    std::array refs {source.Observe()};
    auto observation = observerOwner.Spawn(FirstCompleted(refs, cancellation.GetToken())).value();
    observerExecutor.RunUntilIdle();
    const bool initiallyPending = !observation.IsCompleted();
    auto extra = observerExecutor.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    const bool exhausted = !extra && extra.error() == NGIN::Execution::ScheduleError::ResourceExhausted;
    extra = std::unexpected(NGIN::Execution::ScheduleError::Rejected);
    if (cancelWait)
    {
        cancellation.Cancel(StopReason::Deadline);
        observerExecutor.RunUntilIdle();
    }
    const bool sourceStillOwned = !source.IsCompleted() && producer.Pending() == 1;
    producerExecutor.RunUntilIdle();
    observerExecutor.RunUntilIdle();
    REQUIRE(producer.TryJoin());
    REQUIRE(observerOwner.TryJoin());
    REQUIRE(initiallyPending);
    REQUIRE(exhausted);
    REQUIRE(sourceStillOwned);
    auto result = observation.TakeResult();
    if (cancelWait)
    {
        REQUIRE(result.IsStopped());
        REQUIRE(result.StopReason() == StopReason::Deadline);
    }
    else
    {
        REQUIRE(result.Succeeded());
        REQUIRE(result.Value().index == 0);
    }
    REQUIRE(source.TakeResult().Value() == 42);
}
