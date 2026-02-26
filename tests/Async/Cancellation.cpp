#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Units.hpp>

namespace
{
    class ManualTimerExecutor
    {
    public:
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
}// namespace

TEST_CASE("CreateLinkedCancellationSource cancels when any input cancels")
{
    NGIN::Async::CancellationSource a;
    NGIN::Async::CancellationSource b;

    auto linked = NGIN::Async::CreateLinkedCancellationSource({a.GetToken(), b.GetToken()});
    REQUIRE_FALSE(linked.IsCancellationRequested());

    a.Cancel();
    REQUIRE(linked.IsCancellationRequested());
    REQUIRE(linked.GetToken().IsCancellationRequested());
}

TEST_CASE("Linked cancellation token wakes Delay")
{
    ManualTimerExecutor exec;

    NGIN::Async::CancellationSource a;
    NGIN::Async::CancellationSource b;
    auto linked = NGIN::Async::CreateLinkedCancellationSource({a.GetToken(), b.GetToken()});

    NGIN::Async::TaskContext ctx(exec, linked.GetToken());
    auto task = DelayForever(ctx);
    task.Schedule(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    a.Cancel();
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == NGIN::Async::AsyncErrorCode::Canceled);
}

TEST_CASE("CancelAfter schedules cancellation via executor")
{
    ManualTimerExecutor exec;
    NGIN::Async::CancellationSource src;

    src.CancelAfter(NGIN::Execution::ExecutorRef::From(exec), NGIN::Units::Milliseconds(1.0));
    REQUIRE_FALSE(src.IsCancellationRequested());

    exec.RunAllDelayed();
    exec.RunUntilIdle();

    REQUIRE(src.IsCancellationRequested());
}
