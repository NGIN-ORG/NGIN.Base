#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/TimePoint.hpp>

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

    private:
        std::vector<NGIN::Execution::WorkItem> m_ready;
        std::vector<NGIN::Execution::WorkItem> m_delayed;
    };

    NGIN::Async::Task<int> ParentThrows(NGIN::Async::TaskContext&)
    {
        throw std::runtime_error("parent");
        co_return 0;
    }

    NGIN::Async::Task<int> ParentValue(NGIN::Async::TaskContext&)
    {
        co_return 7;
    }

    NGIN::Async::Task<void> ContinuationThrows(NGIN::Async::TaskContext&)
    {
        throw std::runtime_error("continuation");
        co_return;
    }

    NGIN::Async::Task<void> Noop(NGIN::Async::TaskContext&)
    {
        co_return;
    }

    NGIN::Async::Task<int> SuspendForever(NGIN::Async::TaskContext&)
    {
        co_await std::suspend_always {};
        co_return 42;
    }

    NGIN::Async::Task<void> AwaitParentFault(NGIN::Async::TaskContext& ctx)
    {
        auto parent = ParentThrows(ctx);
        parent.Start(ctx);
        co_await parent.Then([&](int) { return Noop(ctx); });
        co_return;
    }

    NGIN::Async::Task<void> AwaitContinuationFault(NGIN::Async::TaskContext& ctx)
    {
        auto parent = ParentValue(ctx);
        parent.Start(ctx);
        co_await parent.Then([&](int) { return ContinuationThrows(ctx); });
        co_return;
    }

    NGIN::Async::Task<void> AwaitCancellation(NGIN::Async::TaskContext& ctx, NGIN::Async::Task<int>& parent)
    {
        co_await parent.Then([&](int) { return Noop(ctx); });
        co_return;
    }

    NGIN::Async::Task<int> MultiplyAfterYield(NGIN::Async::TaskContext& ctx, int value, int factor)
    {
        co_await ctx.YieldNow();
        co_return value * factor;
    }

    NGIN::Async::Task<int> ThenSuccess(NGIN::Async::TaskContext& ctx)
    {
        auto parent = ParentValue(ctx);
        parent.Start(ctx);

        co_await parent.Then([&](int v) { return MultiplyAfterYield(ctx, v, 3); });
        co_return 21;
    }
}// namespace

TEST_CASE("Task::Then propagates parent exception")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    NGIN::Async::TaskContext         ctx(scheduler, source.GetToken());

    auto task = AwaitParentFault(ctx);
    task.Start(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    REQUIRE_THROWS_AS(task.Get(), std::runtime_error);
}

TEST_CASE("Task::Then propagates continuation exception")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    NGIN::Async::TaskContext         ctx(scheduler, source.GetToken());

    auto task = AwaitContinuationFault(ctx);
    task.Start(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    REQUIRE_THROWS_AS(task.Get(), std::runtime_error);
}

TEST_CASE("Task::Then is woken by cancellation even if parent never completes")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto parent = SuspendForever(ctx);
    parent.Start(ctx);

    auto task = AwaitCancellation(ctx, parent);
    task.Start(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("Task::Then runs continuation on success")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = ThenSuccess(ctx);
    task.Start(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsFaulted());
    REQUIRE(task.Get() == 21);
}
