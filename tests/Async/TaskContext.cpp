#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
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

    private:
        std::vector<NGIN::Execution::WorkItem> m_ready;
        std::vector<NGIN::Execution::WorkItem> m_delayed;
    };

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }
}// namespace

TEST_CASE("TaskContext WithLinkedCancellation cancels when parent token cancels")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource parentSource;
    NGIN::Async::CancellationSource childSource;

    NGIN::Async::TaskContext parentCtx(exec, parentSource.GetToken());
    auto childCtx = parentCtx.WithLinkedCancellation(childSource.GetToken());

    auto task = DelayForever(childCtx);
    task.Start(childCtx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    parentSource.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("TaskContext WithLinkedCancellation supports chaining without losing root linkage")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource rootSource;
    NGIN::Async::CancellationSource extra1;
    NGIN::Async::CancellationSource extra2;

    NGIN::Async::TaskContext ctx0(exec, rootSource.GetToken());
    auto ctx1 = ctx0.WithLinkedCancellation(extra1.GetToken());
    auto ctx2 = ctx1.WithLinkedCancellation(extra2.GetToken());

    auto task = DelayForever(ctx2);
    task.Start(ctx2);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    rootSource.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.Get(), NGIN::Async::TaskCanceled);
}
