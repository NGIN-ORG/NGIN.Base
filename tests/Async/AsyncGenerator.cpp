#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <exception>
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
        co_await ctx.YieldNow();
        co_yield 2;
        co_await ctx.YieldNow();
        co_yield 3;
    }

#if NGIN_ASYNC_HAS_EXCEPTIONS
    NGIN::Async::AsyncGenerator<int> YieldThenThrow(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
    }
#endif

    NGIN::Async::AsyncGenerator<int> YieldOnceThenNever(NGIN::Async::TaskContext& ctx)
    {
        (void) ctx;
        co_yield 1;
        while (true)
        {
            co_await std::suspend_always {};
        }
    }

    NGIN::Async::AsyncGenerator<int> SuspendsBeforeYield(NGIN::Async::TaskContext&)
    {
        co_await std::suspend_always {};
        co_yield 1;
    }

    NGIN::Async::Task<int> ReadOne(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto next = co_await gen.Next(ctx);
        if (next)
        {
            co_return *next;
        }
        co_return 0;
    }

    NGIN::Async::Task<int> SumAll(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        int sum = 0;
        for (;;)
        {
            auto next = co_await gen.Next(ctx);
            if (next.IsEnd())
            {
                break;
            }
            sum += *next;
        }
        co_return sum;
    }

    NGIN::Async::Task<void> ConsumeThenCancel(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto first = co_await gen.Next(ctx);
        if (first)
        {
            (void) *first;
        }

        static_cast<void>(co_await gen.Next(ctx));
        co_return;
    }
}// namespace

TEST_CASE("AsyncGenerator yields values via Next(TaskContext)")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = ProduceValues(ctx);
    auto task = SumAll(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    auto result = op.TakeResult();
    REQUIRE(result);
    REQUIRE(*result == 6);
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
TEST_CASE("AsyncGenerator propagates exceptions from producer")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = YieldThenThrow(ctx);
    auto task = SumAll(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}
#endif

TEST_CASE("AsyncGenerator Next observes TaskContext cancellation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              ctx(scheduler, source.GetToken());

    auto gen  = YieldOnceThenNever(ctx);
    auto task = ConsumeThenCancel(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    source.Cancel();
    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("AsyncGenerator faults concurrent Next consumers")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen    = SuspendsBeforeYield(ctx);
    auto first  = ReadOne(ctx, gen);
    auto second = ReadOne(ctx, gen);

    auto firstOp  = NGIN::Async::Spawn(ctx, std::move(first));
    auto secondOp = NGIN::Async::Spawn(ctx, std::move(second));
    scheduler.RunUntilIdle();

    REQUIRE(firstOp.IsCompleted());
    REQUIRE(secondOp.IsCompleted());
    REQUIRE((firstOp.IsFaulted() || secondOp.IsFaulted()));
}
