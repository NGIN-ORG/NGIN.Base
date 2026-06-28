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

    NGIN::Async::Task<int> Throws(NGIN::Async::TaskContext&)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        throw std::runtime_error("parent");
        co_return 0;
#else
        co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
#endif
    }

    NGIN::Async::Task<int> Value(NGIN::Async::TaskContext&)
    {
        co_return 7;
    }

    NGIN::Async::Task<int> DelayThenValue(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return 42;
    }

    NGIN::Async::Task<void> AwaitOperationFault(NGIN::Async::TaskContext& ctx)
    {
        auto op     = NGIN::Async::Spawn(ctx, Throws(ctx));
        auto result = co_await op;
        if (!result)
        {
            co_await NGIN::Async::Faulted(result.Fault());
        }
        co_return;
    }

    NGIN::Async::Task<int> AwaitOperationSuccess(NGIN::Async::TaskContext& ctx)
    {
        auto op     = NGIN::Async::Spawn(ctx, Value(ctx));
        auto result = co_await op;
        if (!result)
        {
            co_return result;
        }
        co_return *result * 3;
    }

    NGIN::Async::Task<void> AwaitOperationCancellation(NGIN::Async::TaskContext& ctx)
    {
        auto op     = NGIN::Async::Spawn(ctx, DelayThenValue(ctx));
        auto result = co_await op;
        if (result.IsCanceled())
        {
            co_await NGIN::Async::Canceled();
        }
        co_return;
    }
}// namespace

TEST_CASE("Operation co_await exposes child fault as completion")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    NGIN::Async::TaskContext         ctx(scheduler, source.GetToken());

    auto op = NGIN::Async::Spawn(ctx, AwaitOperationFault(ctx));

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsFaulted());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}

TEST_CASE("Operation co_await exposes child success")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto op = NGIN::Async::Spawn(ctx, AwaitOperationSuccess(ctx));

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE_FALSE(op.IsFaulted());
    auto result = op.TakeResult();
    REQUIRE(result);
    REQUIRE(*result == 21);
}

TEST_CASE("Operation co_await is woken by cancellation")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto op = NGIN::Async::Spawn(ctx, AwaitOperationCancellation(ctx));

    exec.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}
