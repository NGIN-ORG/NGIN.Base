#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <stdexcept>

#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

namespace
{
    NGIN::Async::AsyncGenerator<int> ProduceValues(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.Yield();
        co_yield 2;
        co_await ctx.Yield();
        co_yield 3;
    }

    NGIN::Async::AsyncGenerator<int> YieldThenThrow(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.Yield();
        throw std::runtime_error("boom");
    }

    NGIN::Async::AsyncGenerator<int> YieldOnceThenNever(NGIN::Async::TaskContext& ctx)
    {
        (void)ctx;
        co_yield 1;
        while (true)
        {
            co_await std::suspend_always {};
        }
    }

    NGIN::Async::Task<int> SumAll(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        int sum = 0;
        while (auto value = co_await gen.Next(ctx))
        {
            sum += *value;
        }
        co_return sum;
    }

    NGIN::Async::Task<void> ConsumeThenCancel(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto first = co_await gen.Next(ctx);
        if (first)
        {
            (void)*first;
        }

        (void)co_await gen.Next(ctx);
        co_return;
    }
}// namespace

TEST_CASE("AsyncGenerator yields values via Next(TaskContext)")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = ProduceValues(ctx);
    auto task = SumAll(ctx, gen);
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.Get() == 6);
}

TEST_CASE("AsyncGenerator propagates exceptions from producer")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = YieldThenThrow(ctx);
    auto task = SumAll(ctx, gen);
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_THROWS_AS(task.Get(), std::runtime_error);
}

TEST_CASE("AsyncGenerator Next observes TaskContext cancellation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              ctx(scheduler, source.GetToken());

    auto gen  = YieldOnceThenNever(ctx);
    auto task = ConsumeThenCancel(ctx, gen);
    task.Start(ctx);

    scheduler.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.Cancel();
    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

