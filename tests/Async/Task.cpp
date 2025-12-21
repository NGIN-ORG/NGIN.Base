#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Units.hpp>

namespace
{
    NGIN::Async::Task<int> CancelledDelayTask(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Milliseconds(1.0));
        co_return 123;
    }

    NGIN::Async::Task<void> CancelledYieldTask(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Yield();
        co_return;
    }

    NGIN::Async::Task<void> CancelledThrowTask(NGIN::Async::TaskContext& ctx)
    {
        ctx.ThrowIfCancellationRequested();
        co_return;
    }
}// namespace

TEST_CASE("Task cancellation: Delay throws TaskCanceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledDelayTask(ctx);
    task.Start(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("Task cancellation: Yield throws TaskCanceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledYieldTask(ctx);
    task.Start(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("Task cancellation: ThrowIfCancellationRequested throws TaskCanceled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledThrowTask(ctx);
    task.Start(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

