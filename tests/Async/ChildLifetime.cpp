#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAny.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <atomic>
#include <future>
#include <memory>

TEST_CASE("Canceling a task wait joins child work before publishing parent cancellation", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              childContext(scheduler);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              waitContext(scheduler, cancellation.GetToken());
    bool                                  childFinished   = false;
    bool                                  parentContinued = false;
    bool                                  useVoid         = false;
    SECTION("value child") {}
    SECTION("void child")
    {
        useVoid = true;
    }
    auto valueChild = [&](NGIN::Async::TaskContext& ctx, int& borrowed) -> NGIN::Async::Task<int> {
        co_await ctx.Delay(NGIN::Units::Seconds(1));
        borrowed      = 42;
        childFinished = true;
        co_return borrowed;
    };
    auto voidChild = [&](NGIN::Async::TaskContext& ctx, int& borrowed) -> NGIN::Async::Task<void> {
        co_await ctx.Delay(NGIN::Units::Seconds(1));
        borrowed      = 42;
        childFinished = true;
    };
    auto parent = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> {
        int borrowed = 0;
        if (useVoid)
        {
            auto child = voidChild(childContext, borrowed);
            co_await child.WithCancellation(waitContext);
        }
        else
        {
            auto child = valueChild(childContext, borrowed);
            (void) co_await child.WithCancellation(waitContext);
        }
        parentContinued = true;
    };
    auto operation = NGIN::Async::Spawn(childContext, parent(childContext));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(childFinished);
    cancellation.Cancel();
    scheduler.RunUntilIdle();
    const bool completedBeforeChild = operation.IsCompleted();
    scheduler.RunUntilIdleAt(NGIN::Time::TimePoint::FromNanoseconds(
            NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 2'000'000'000ULL));
    REQUIRE_FALSE(completedBeforeChild);
    REQUIRE(childFinished);
    REQUIRE_FALSE(parentContinued);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsCanceled());
}

TEST_CASE("An already canceled task wait does not start a cold child", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              canceledContext(scheduler, cancellation.GetToken());
    cancellation.Cancel();
    bool started = false;
    auto child   = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
        started = true;
        co_return 42;
    };
    auto parent = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<int> {
        auto work = child(context);
        co_return co_await work.WithCancellation(canceledContext);
    };
    auto operation = NGIN::Async::Spawn(ctx, parent(ctx));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(started);
    REQUIRE(operation.IsCanceled());
}

TEST_CASE("Nested canceled tasks retire child locals before their borrowed parent locals", "[Async][Lifetime][Race]")
{
    NGIN::Execution::ThreadPoolScheduler executor(4);
    NGIN::Async::TaskContext             context(executor);
    struct Lifetime
    {
        std::atomic<bool>  parentAlive {false};
        std::atomic<bool>  orderViolation {false};
        std::atomic<bool>  middleAlive {false};
        std::promise<void> childRetired;
        std::promise<void> middleRetired;
    };
    struct ParentGuard
    {
        std::shared_ptr<Lifetime> state;
        explicit ParentGuard(std::shared_ptr<Lifetime> value) : state(std::move(value)) { state->parentAlive = true; }
        ~ParentGuard() { state->parentAlive = false; }
    };
    struct MiddleGuard
    {
        std::shared_ptr<Lifetime> state;
        explicit MiddleGuard(std::shared_ptr<Lifetime> value) : state(std::move(value)) { state->middleAlive = true; }
        ~MiddleGuard()
        {
            if (!state->parentAlive.load())
                state->orderViolation = true;
            state->middleAlive = false;
            state->middleRetired.set_value();
        }
    };
    struct ChildGuard
    {
        std::shared_ptr<Lifetime> state;
        ~ChildGuard()
        {
            if (!state->middleAlive.load())
                state->orderViolation = true;
            state->childRetired.set_value();
        }
    };
    bool useValue          = false;
    bool cancellationAware = false;
    bool suspended         = false;
    SECTION("void canceled entry") {}
    SECTION("value canceled entry")
    {
        useValue = true;
    }
    SECTION("cancellation-aware void await")
    {
        cancellationAware = true;
    }
    SECTION("cancellation-aware value await")
    {
        cancellationAware = true;
        useValue          = true;
    }
    SECTION("void suspended delay")
    {
        suspended = true;
    }
    SECTION("value suspended delay")
    {
        suspended = true;
        useValue  = true;
    }
    for (int iteration = 0; iteration < (suspended ? 256 : 2000); ++iteration)
    {
        auto              state         = std::make_shared<Lifetime>();
        auto              retired       = state->childRetired.get_future();
        auto              middleRetired = state->middleRetired.get_future();
        std::atomic<bool> dispatchRejected {false};
        auto              child = [&dispatchRejected]<typename Result>(NGIN::Async::TaskContext&       ctx,
                                                                       NGIN::Async::CancellationSource cancellation, std::shared_ptr<Lifetime> lifetime,
                                                                       bool suspend) -> NGIN::Async::Task<Result> {
            ChildGuard guard {std::move(lifetime)};
            if (suspend)
            {
                auto queued = ctx.GetExecutor().Execute(NGIN::Execution::WorkItem([cancellation]() mutable {
                    cancellation.Cancel();
                }));
                if (!queued)
                {
                    dispatchRejected = true;
                    cancellation.Cancel();
                }
                co_await ctx.Delay(NGIN::Units::Seconds(60));
            }
            else
            {
                cancellation.Cancel();
                co_await ctx.YieldNow();
            }
            if constexpr (std::is_void_v<Result>)
                co_return;
            else
                co_return 42;
        };
        auto middle = [state, &child, useValue, cancellationAware, suspended](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<void> {
            MiddleGuard                     guard {state};
            NGIN::Async::CancellationSource cancellation;
            auto                            childContext = ctx.WithLinkedCancellationToken(cancellation.GetToken());
            if (useValue)
            {
                auto task = child.template operator()<int>(childContext, cancellation, state, suspended);
                if (cancellationAware)
                    (void) co_await task.WithCancellation(ctx);
                else
                    (void) co_await std::move(task);
            }
            else
            {
                auto task = child.template operator()<void>(childContext, cancellation, state, suspended);
                if (cancellationAware)
                    co_await task.WithCancellation(ctx);
                else
                    co_await std::move(task);
            }
        };
        auto parent = [state, &middle](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<void> {
            ParentGuard guard {state};
            co_await middle(ctx);
        };
        auto result = NGIN::Async::SyncWait(context, NGIN::Async::WhenAny(context, std::move(parent)));
        REQUIRE(result.IsCanceled());
        REQUIRE(retired.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        REQUIRE(middleRetired.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        REQUIRE_FALSE(state->orderViolation.load());
        REQUIRE_FALSE(state->parentAlive.load());
        REQUIRE_FALSE(dispatchRejected.load());
    }
}

TEST_CASE("Operation awaits release canceled child locals before normal parent return", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler executor;
    NGIN::Async::TaskContext              context(executor);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              childContext(executor, cancellation.GetToken());
    bool                                  useValue = false;
    bool                                  owning   = false;
    SECTION("borrowed void operation") {}
    SECTION("owned void operation")
    {
        owning = true;
    }
    SECTION("borrowed value operation")
    {
        useValue = true;
    }
    SECTION("owned value operation")
    {
        useValue = true;
        owning   = true;
    }
    bool parentAlive          = false;
    bool orderViolation       = false;
    bool childDestroyed       = false;
    bool observedCancellation = false;
    struct ParentGuard
    {
        bool& alive;
        explicit ParentGuard(bool& value) : alive(value) { alive = true; }
        ~ParentGuard() { alive = false; }
    };
    struct ChildGuard
    {
        bool& parentAlive;
        bool& orderViolation;
        bool& destroyed;
        ~ChildGuard()
        {
            orderViolation = !parentAlive;
            destroyed      = true;
        }
    };
    auto child = [&]<typename Result>(NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<Result> {
        ChildGuard guard {parentAlive, orderViolation, childDestroyed};
        cancellation.Cancel();
        co_await ctx.YieldNow();
        if constexpr (std::is_void_v<Result>)
            co_return;
        else
            co_return 42;
    };
    auto parent = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> {
        ParentGuard guard {parentAlive};
        if (useValue)
        {
            auto operation = NGIN::Async::Spawn(childContext, child.template operator()<int>(childContext));
            if (owning)
                observedCancellation = (co_await std::move(operation)).IsCanceled();
            else
                observedCancellation = (co_await operation).IsCanceled();
        }
        else
        {
            auto operation = NGIN::Async::Spawn(childContext, child.template operator()<void>(childContext));
            if (owning)
                observedCancellation = (co_await std::move(operation)).IsCanceled();
            else
                observedCancellation = (co_await operation).IsCanceled();
        }
    };
    auto operation = NGIN::Async::Spawn(context, parent(context));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(observedCancellation);
    REQUIRE(childDestroyed);
    REQUIRE_FALSE(orderViolation);
    REQUIRE_FALSE(parentAlive);
}
