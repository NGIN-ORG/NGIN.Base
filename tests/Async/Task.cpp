#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <vector>
#include <stdexcept>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Units.hpp>

namespace
{
    class ManualTimerExecutor
    {
    public:
        ManualTimerExecutor()
        {
            m_ready.reserve(256);
            m_delayed.reserve(256);
        }

        void Execute(NGIN::Execution::WorkItem item) noexcept
        {
            m_ready.push_back(std::move(item));
        }

        void ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint)
        {
            m_delayed.push_back(std::move(item));
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            if (m_ready.empty())
            {
                return false;
            }
            auto item = std::move(m_ready.back());
            m_ready.pop_back();
            item.Invoke();
            return true;
        }

        void RunUntilIdle() noexcept
        {
            while (RunOne()) {}
        }

        void RunAllDelayed() noexcept
        {
            for (auto& item: m_delayed)
            {
                Execute(std::move(item));
            }
            m_delayed.clear();
        }

    private:
        std::vector<NGIN::Execution::WorkItem> m_ready;
        std::vector<NGIN::Execution::WorkItem> m_delayed;
    };

    NGIN::Async::Task<int> CancelledDelayTask(NGIN::Async::TaskContext& ctx)
    {
        auto delayResult = co_await ctx.Delay(NGIN::Units::Milliseconds(1.0));
        if (!delayResult)
        {
            co_return std::unexpected(delayResult.error());
        }
        co_return 123;
    }

    NGIN::Async::Task<void> CancelledYieldTask(NGIN::Async::TaskContext& ctx)
    {
        auto yieldResult = co_await ctx.YieldNow();
        if (!yieldResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(yieldResult.error());
            co_return;
        }
        co_return;
    }

    NGIN::Async::Task<void> CancelledThrowTask(NGIN::Async::TaskContext& ctx)
    {
        auto cancelResult = ctx.CheckCancellation();
        if (!cancelResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(cancelResult.error());
            co_return;
        }
        co_return;
    }

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        auto delayResult = co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        if (!delayResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(delayResult.error());
            co_return;
        }
        co_return;
    }

    NGIN::Async::Task<int> AddAfterYield(NGIN::Async::TaskContext& ctx, int a, int b)
    {
        auto yieldResult = co_await ctx.YieldNow();
        if (!yieldResult)
        {
            co_return std::unexpected(yieldResult.error());
        }
        co_return a + b;
    }

    NGIN::Async::Task<int> ThrowAfterYield(NGIN::Async::TaskContext& ctx)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        auto yieldResult = co_await ctx.YieldNow();
        if (!yieldResult)
        {
            co_return std::unexpected(yieldResult.error());
        }
        throw std::runtime_error("boom");
#else
        auto yieldResult = co_await ctx.YieldNow();
        if (!yieldResult)
        {
            co_return std::unexpected(yieldResult.error());
        }
        co_return std::unexpected(NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Fault));
#endif
    }

    NGIN::Async::Task<int> AwaitChild(NGIN::Async::TaskContext& ctx)
    {
        auto child = AddAfterYield(ctx, 1, 2);
        auto childResult = co_await child;
        if (!childResult)
        {
            co_return std::unexpected(childResult.error());
        }
        co_return *childResult;
    }

    NGIN::Async::Task<void> AwaitChildThatThrows(NGIN::Async::TaskContext& ctx)
    {
        auto child = ThrowAfterYield(ctx);
        auto childResult = co_await child;
        if (!childResult)
        {
            co_await NGIN::Async::Task<void>::ReturnError(childResult.error());
            co_return;
        }
        co_return;
    }
}// namespace

TEST_CASE("Task cancellation: Delay returns canceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledDelayTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("Task cancellation: Delay is woken by cancellation")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
    task.Schedule(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("Task cancellation: CancelAt wakes Delay without firing timers")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
    task.Schedule(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.CancelAt(ctx.GetExecutor(), NGIN::Time::TimePoint::FromNanoseconds(1));
    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    exec.RunAllDelayed(); // execute the cancel job
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("Task cancellation: Yield returns canceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledYieldTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("Task cancellation: CheckCancellation returns canceled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledThrowTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("Task can be awaited without calling Start() on the child task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChild(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsFaulted());
    REQUIRE_FALSE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 3);
}

TEST_CASE("Task propagates exceptions through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChildThatThrows(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    auto result = task.Get();
    REQUIRE_FALSE(result);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
    REQUIRE(task.GetException() != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(task.GetException()), std::runtime_error);
#endif
}

TEST_CASE("TaskContext::Run starts and schedules a task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = ctx.Run(AddAfterYield, 2, 5);
    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 7);
}
