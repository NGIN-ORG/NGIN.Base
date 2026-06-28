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
#include <NGIN/Units.hpp>

namespace
{
    struct TestDomainError final
    {
        int code {0};
    };

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

    NGIN::Async::Task<int> AddAfterYield(NGIN::Async::TaskContext& ctx, int a, int b)
    {
        co_await ctx.YieldNow();
        co_return a + b;
    }

    NGIN::Async::Task<void> IncrementAfterYield(NGIN::Async::TaskContext& ctx, int& value)
    {
        co_await ctx.YieldNow();
        ++value;
        co_return;
    }

    NGIN::Async::Task<int> AwaitChild(NGIN::Async::TaskContext& ctx)
    {
        co_return co_await AddAfterYield(ctx, 1, 2);
    }

    NGIN::Async::Task<int, TestDomainError> DomainErrorAfterYield(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return TestDomainError {17};
    }

    NGIN::Async::Task<int, TestDomainError> AwaitDomainError(NGIN::Async::TaskContext& ctx)
    {
        co_return co_await DomainErrorAfterYield(ctx);
    }

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }

    NGIN::Async::Task<int> ThrowAfterYield(NGIN::Async::TaskContext& ctx)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
#else
        co_await ctx.YieldNow();
        co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
#endif
    }

    NGIN::Async::Task<int> AwaitChildThatThrows(NGIN::Async::TaskContext& ctx)
    {
        static_cast<void>(co_await ThrowAfterYield(ctx));
        co_return 1;
    }
}// namespace

TEST_CASE("Spawn consumes a cold task and returns a running operation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AddAfterYield(ctx, 2, 5));
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 7);
}

TEST_CASE("Operation reports invalid result access before completion")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AddAfterYield(ctx, 2, 3));

    auto early = operation.TakeResult();
    REQUIRE_FALSE(early);
    REQUIRE(early.IsFault());

    scheduler.RunUntilIdle();
}

TEST_CASE("Operation result can be taken only once")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AddAfterYield(ctx, 4, 9));
    scheduler.RunUntilIdle();

    auto first = operation.TakeResult();
    REQUIRE(first);
    REQUIRE(first.Value() == 13);

    auto second = operation.TakeResult();
    REQUIRE_FALSE(second);
    REQUIRE(second.IsFault());
}

TEST_CASE("Detach explicitly runs fire-and-forget work")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    int                                   value = 0;

    NGIN::Async::Detach(ctx, IncrementAfterYield(ctx, value));
    scheduler.RunUntilIdle();

    REQUIRE(value == 1);
}

TEST_CASE("Task can be awaited without spawning the child explicitly")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AwaitChild(ctx));
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 3);
}

TEST_CASE("Task domain errors propagate through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AwaitDomainError(ctx));
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().code == 17);
}

TEST_CASE("Task cancellation: Delay is woken by cancellation")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto operation = NGIN::Async::Spawn(ctx, DelayForever(ctx));

    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsCanceled());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task cancellation: CancelAt wakes Delay without firing timers")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto operation = NGIN::Async::Spawn(ctx, DelayForever(ctx));

    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    source.CancelAt(ctx.GetExecutor(), NGIN::Time::TimePoint::FromNanoseconds(1));
    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    exec.RunAllDelayed();
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsCanceled());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task propagates exceptions through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operation = NGIN::Async::Spawn(ctx, AwaitChildThatThrows(ctx));
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}
