#include <catch2/catch_test_macros.hpp>

#include <exception>
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
#if NGIN_ASYNC_HAS_EXCEPTIONS
        throw std::runtime_error("parent");
        co_return 0;
#else
        co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::Unknown));
#endif
    }

    NGIN::Async::Task<int> ParentValue(NGIN::Async::TaskContext&)
    {
        co_return 7;
    }

    NGIN::Async::Task<void> ContinuationThrows(NGIN::Async::TaskContext&)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        throw std::runtime_error("continuation");
        co_return;
#else
        co_await NGIN::Async::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::Unknown));
        co_return;
#endif
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
        parent.Schedule(ctx);
        co_await parent.ContinueWith(ctx, [&](int) { return Noop(ctx); });
        co_return;
    }

    NGIN::Async::Task<void> AwaitContinuationFault(NGIN::Async::TaskContext& ctx)
    {
        auto parent = ParentValue(ctx);
        parent.Schedule(ctx);
        co_await parent.ContinueWith(ctx, [&](int) { return ContinuationThrows(ctx); });
        co_return;
    }

    NGIN::Async::Task<void> AwaitCancellation(NGIN::Async::TaskContext& ctx, NGIN::Async::Task<int>& parent)
    {
        co_await parent.ContinueWith(ctx, [&](int) { return Noop(ctx); });
        co_return;
    }

    NGIN::Async::Task<int> MultiplyAfterYield(NGIN::Async::TaskContext& ctx, int value, int factor)
    {
        co_await ctx.YieldNow();
        co_return value * factor;
    }

    NGIN::Async::Task<int> ContinueWithSuccess(NGIN::Async::TaskContext& ctx)
    {
        auto parent = ParentValue(ctx);
        parent.Schedule(ctx);

        co_return co_await parent.ContinueWith(ctx, [&](int v) { return MultiplyAfterYield(ctx, v, 3); });
    }
}// namespace

TEST_CASE("Task::ContinueWith propagates parent fault")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    NGIN::Async::TaskContext         ctx(scheduler, source.GetToken());

    auto task = AwaitParentFault(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}

TEST_CASE("Task::ContinueWith propagates continuation fault")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    NGIN::Async::TaskContext         ctx(scheduler, source.GetToken());

    auto task = AwaitContinuationFault(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}

TEST_CASE("Task::ContinueWith is woken by cancellation even if parent never completes")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto parent = SuspendForever(ctx);
    parent.Schedule(ctx);

    auto task = AwaitCancellation(ctx, parent);
    task.Schedule(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task::ContinueWith runs continuation on success")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = ContinueWithSuccess(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsFaulted());
    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 21);
}
