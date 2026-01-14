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
        if (auto yieldResult = co_await ctx.YieldNow(); !yieldResult)
        {
            co_await NGIN::Async::AsyncGenerator<int>::ReturnError(yieldResult.error());
            co_return;
        }
        co_yield 2;
        if (auto yieldResult = co_await ctx.YieldNow(); !yieldResult)
        {
            co_await NGIN::Async::AsyncGenerator<int>::ReturnError(yieldResult.error());
            co_return;
        }
        co_yield 3;
    }

#if NGIN_ASYNC_HAS_EXCEPTIONS
    NGIN::Async::AsyncGenerator<int> YieldThenThrow(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        if (auto yieldResult = co_await ctx.YieldNow(); !yieldResult)
        {
            co_await NGIN::Async::AsyncGenerator<int>::ReturnError(yieldResult.error());
            co_return;
        }
        throw std::runtime_error("boom");
    }
#endif

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
        for (;;)
        {
            auto nextResult = co_await gen.Next(ctx);
            if (!nextResult)
            {
                co_return std::unexpected(nextResult.error());
            }
            if (!*nextResult)
            {
                break;
            }
            sum += **nextResult;
        }
        co_return sum;
    }

    NGIN::Async::Task<void> ConsumeThenCancel(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto firstResult = co_await gen.Next(ctx);
        if (!firstResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(firstResult.error());
            co_return;
        }
        if (*firstResult)
        {
            (void)**firstResult;
        }

        auto secondResult = co_await gen.Next(ctx);
        if (!secondResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(secondResult.error());
            co_return;
        }
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
    auto result = task.Get();
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
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(task.IsFaulted());
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
    REQUIRE(task.GetException() != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(task.GetException()), std::runtime_error);
#endif
}
#endif

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
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

