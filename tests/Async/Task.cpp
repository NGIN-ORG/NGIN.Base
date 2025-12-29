#include <catch2/catch_test_macros.hpp>

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
        co_await ctx.Delay(NGIN::Units::Milliseconds(1.0));
        co_return 123;
    }

    NGIN::Async::Task<void> CancelledYieldTask(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return;
    }

    NGIN::Async::Task<void> CancelledThrowTask(NGIN::Async::TaskContext& ctx)
    {
        ctx.ThrowIfCancellationRequested();
        co_return;
    }

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }

    NGIN::Async::Task<int> AddAfterYield(NGIN::Async::TaskContext& ctx, int a, int b)
    {
        co_await ctx.YieldNow();
        co_return a + b;
    }

    NGIN::Async::Task<int> ThrowAfterYield(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
    }

    NGIN::Async::Task<int> AwaitChild(NGIN::Async::TaskContext& ctx)
    {
        auto child = AddAfterYield(ctx, 1, 2);
        co_return co_await child;
    }

    NGIN::Async::Task<void> AwaitChildThatThrows(NGIN::Async::TaskContext& ctx)
    {
        auto child = ThrowAfterYield(ctx);
        (void)co_await child;
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

TEST_CASE("Task cancellation: Delay is woken by cancellation")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
    task.Start(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("Task cancellation: CancelAt wakes Delay without firing timers")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
    task.Start(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.CancelAt(ctx.GetExecutor(), NGIN::Time::TimePoint::FromNanoseconds(1));
    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    exec.RunAllDelayed(); // execute the cancel job
    exec.RunUntilIdle();

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

TEST_CASE("Task can be awaited without calling Start() on the child task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChild(ctx);
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsFaulted());
    REQUIRE_FALSE(task.IsCanceled());
    REQUIRE(task.Get() == 3);
}

TEST_CASE("Task propagates exceptions through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChildThatThrows(ctx);
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    REQUIRE_THROWS_AS(task.Get(), std::runtime_error);
}

TEST_CASE("TaskContext::Run starts and schedules a task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = ctx.Run(AddAfterYield, 2, 5);
    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.Get() == 7);
}
