#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WithScope.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// Public consumers cannot manufacture producer ownership from a frame or inline result.
static_assert(!std::is_constructible_v<NGIN::Async::Operation<int>,
                                      NGIN::Async::Task<int>::handle_type, NGIN::Execution::ExecutorRef>);
static_assert(!std::is_constructible_v<NGIN::Async::Operation<void>,
                                      NGIN::Async::Task<void>::handle_type, NGIN::Execution::ExecutorRef>);
static_assert(!std::is_constructible_v<NGIN::Async::Operation<int>, NGIN::Async::Completion<int, NGIN::Async::NoError>>);
static_assert(!std::is_constructible_v<NGIN::Async::Operation<void>, NGIN::Async::Completion<void, NGIN::Async::NoError>>);

namespace
{
    using namespace NGIN::Async;

    template<typename T, typename E>
    Task<T, E> Consume(Operation<T, E>& operation)
    {
        if constexpr (std::is_void_v<T>)
        {
            auto result = co_await operation;
            if (result.HasError())
                co_await Failure(std::move(result).Error());
            if (result.IsStopped())
                co_await Stopped(result.StopReason());
            co_return;
        }
        else
            co_return co_await operation;
    }

    template<typename E>
    Task<void> Join(TaskScope<E>& scope)
    {
        (void) co_await scope.Join();
    }

    Task<std::unique_ptr<int>> ColdValue(bool& started)
    {
        started = true;
        co_return std::make_unique<int>(42);
    }
}// namespace

TEST_CASE("Scoped results consume move-only values and survive owner destruction", "[Async][Scope][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    Operation<std::unique_ptr<int>>       result;
    bool                                  started = false;
    {
        TaskScope scope(context.GetEnvironment(), 1);
        auto      admitted = scope.Spawn(ColdValue(started));
        REQUIRE(admitted);
        REQUIRE_FALSE(started);
        result = std::move(*admitted);
        REQUIRE_FALSE(admitted->IsValid());
        REQUIRE_FALSE(result.TryTakeResult());
        auto joinTask = Join(scope);

        TaskSupervisor<typename decltype(joinTask)::ErrorType> joinRoot(context.GetEnvironment(), 1);

        auto join = joinRoot.Spawn(std::move(joinTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinRoot.TryJoin());
        REQUIRE(join.TakeResult().Succeeded());
        REQUIRE(started);
        REQUIRE(result.IsCompleted());
    }
    REQUIRE(*result.TakeResult().Value() == 42);
    REQUIRE(result.TakeResult().Error().Fault().code == AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("Scoped await and Join both retain move-only child errors", "[Async][Scope][Lifetime]")
{
    using E = std::unique_ptr<int>;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    std::optional<ScopeReport<E>>         report;
    std::optional<Completion<int, E>>     consumed;
    {
        TaskScope<E> scope(context.GetEnvironment(), 1);
        auto         child        = scope.Spawn([](TaskContext& context) -> Task<int, E> {
                              co_await context.YieldNow();
                              co_return Completion<int, E>::DomainFailure(std::make_unique<int>(23));
                          }).value();
        auto         consumerTask = Consume(child);

        TaskSupervisor<typename decltype(consumerTask)::ErrorType> consumerRoot(context.GetEnvironment(), 1);

        auto consumer   = consumerRoot.Spawn(std::move(consumerTask)).value();
        auto joinedTask = Join(scope);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE(joined.TakeResult().Succeeded());
        REQUIRE(scope.GetCancellationToken().IsCancellationRequested());
        REQUIRE(consumerRoot.TryJoin());
        consumed.emplace(consumer.TakeResult());
        report = scope.TakeResult();
        REQUIRE(child.HasError());
        REQUIRE(child.TakeResult().Error().Fault().code == AsyncFaultCode::InvalidTaskUsage);
    }
    REQUIRE(*consumed->Error().DomainError() == 23);
    REQUIRE(*report->Error().DomainError() == 23);
    REQUIRE(&consumed->Error().DomainError() == &report->Error().DomainError());
    consumed.reset();
    REQUIRE(*report->Error().DomainError() == 23);
}

TEST_CASE("Scoped result waits for final producer frame lease", "[Async][Scope][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    TaskScope                             scope(context.GetEnvironment(), 1);
    NGIN::Execution::WorkItem             release;
    bool                                  destroyed = false;
    struct Guard
    {
        bool& destroyed;
        ~Guard() { destroyed = true; }
    };
    struct HoldFrame
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        bool                       await_suspend(std::coroutine_handle<Task<void>::promise_type> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };
    auto child        = scope.Spawn([&](TaskContext& ctx) -> Task<void> {
                          Guard guard {destroyed};
                          co_await HoldFrame {release};
                          scope.RequestStop();
                          co_await ctx.YieldNow();
                      }).value();
    auto consumerTask = Consume(child);

    TaskSupervisor<typename decltype(consumerTask)::ErrorType> consumerRoot(context.GetEnvironment(), 1);

    auto consumer   = consumerRoot.Spawn(std::move(consumerTask)).value();
    auto joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    const bool early            = child.IsCompleted() || consumer.IsCompleted() || joined.IsCompleted();
    const bool earlyDestruction = destroyed;
    release.Invoke();
    release = {};
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(early);
    REQUIRE_FALSE(earlyDestruction);
    REQUIRE(destroyed);
    REQUIRE(consumerRoot.TryJoin());
    REQUIRE(consumer.TakeResult().IsStopped());
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
}

TEST_CASE("Scoped consumer resumes on its own executor", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler owner, worker, observer;
    TaskContext                           ownerContext(owner), observerContext(observer);
    TaskScope                             scope(ownerContext.GetEnvironment(), 1);
    auto                                  child        = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(worker), [](TaskContext&) -> Task<int> {
                          co_return 42;
                      }).value();
    auto                                  consumerTask = Consume(child);

    TaskSupervisor<typename decltype(consumerTask)::ErrorType> consumerRoot(observerContext.GetEnvironment(), 1);

    auto consumer   = consumerRoot.Spawn(std::move(consumerTask)).value();
    auto joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(ownerContext.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    observer.RunUntilIdle();
    worker.RunUntilIdle();
    owner.RunUntilIdle();
    REQUIRE(child.IsCompleted());
    REQUIRE(joined.IsCompleted());
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE_FALSE(consumer.IsCompleted());
    observer.RunUntilIdle();
    REQUIRE(consumerRoot.TryJoin());
    REQUIRE(consumer.TakeResult().Value() == 42);
}

TEST_CASE("Scoped result rejects a second pending consumer without losing the first", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler scheduler, worker;
    TaskContext                           context(scheduler);
    TaskScope                             scope(context.GetEnvironment(), 1);
    auto                                  child     = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(worker), [](TaskContext&) -> Task<int> {
                          co_return 42;
                      }).value();
    auto                                  firstTask = Consume(child);

    TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first      = firstRoot.Spawn(std::move(firstTask)).value();
    auto secondTask = Consume(child);

    TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    scheduler.RunUntilIdle();
    const bool firstPending = !first.IsCompleted();
    REQUIRE(secondRoot.TryJoin());
    const auto secondResult = second.TakeResult();
    worker.RunUntilIdle();
    auto joinedTask = Join(scope);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(firstPending);
    REQUIRE(secondResult.HasError());
    REQUIRE(secondResult.Error().Fault().code == AsyncFaultCode::InvalidTaskUsage);
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(first.TakeResult().Value() == 42);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
}

TEST_CASE("Scoped rvalue await and WithScope join permit reentrant owner release", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    const bool                            ready = GENERATE(false, true);
    auto                                  body  = [ready](TaskScope<>& scope) -> Task<int> {
        auto child = scope.Spawn([ready](TaskContext& ctx) -> Task<int> {
                              if (!ready)
                                  co_await ctx.YieldNow();
                              co_return 42;
                          }).value();
        co_return co_await std::move(child);
    };
    auto rootTask = WithScope(context.GetEnvironment(), 1, std::move(body));

    TaskSupervisor<typename decltype(rootTask)::ErrorType> rootRoot(context.GetEnvironment(), 1);

    auto root = rootRoot.Spawn(std::move(rootTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(root.IsCompleted());
    REQUIRE(rootRoot.TryJoin());
    REQUIRE(root.TakeResult().Value() == 42);
}

TEST_CASE("Rejected cold scoped input never starts", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    TaskScope                             scope(context.GetEnvironment(), 1);
    bool                                  started = false;
    scope.Close();
    auto rejected = scope.Spawn(ColdValue(started));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == NGIN::Execution::ScheduleError::Stopped);
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(started);
    REQUIRE(scope.Pending() == 0);
}
