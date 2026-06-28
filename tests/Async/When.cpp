#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <stdexcept>
#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/WhenAny.hpp>

namespace
{
    class ManualExecutor
    {
    public:
        ManualExecutor()
        {
            m_queue.reserve(256);
        }

        void Execute(NGIN::Execution::WorkItem item) noexcept
        {
            m_queue.push_back(std::move(item));
        }

        void ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint)
        {
            Execute(std::move(item));
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            if (m_head >= m_queue.size())
            {
                m_queue.clear();
                m_head = 0;
                return false;
            }

            auto item = std::move(m_queue[m_head++]);
            item.Invoke();

            if (m_head >= m_queue.size())
            {
                m_queue.clear();
                m_head = 0;
            }
            return true;
        }

        void RunUntilIdle() noexcept
        {
            while (RunOne()) {}
        }

    private:
        std::vector<NGIN::Execution::WorkItem> m_queue;
        std::size_t                            m_head {0};
    };

    NGIN::Async::Task<int> YieldOnce(NGIN::Async::TaskContext& ctx, int value)
    {
        co_await ctx.YieldNow();
        co_return value;
    }

    NGIN::Async::Task<int> YieldTwice(NGIN::Async::TaskContext& ctx, int value)
    {
        co_await ctx.YieldNow();
        co_await ctx.YieldNow();
        co_return value;
    }

    NGIN::Async::Task<void> VoidYieldOnce(NGIN::Async::TaskContext& ctx, int& value)
    {
        co_await ctx.YieldNow();
        ++value;
        co_return;
    }

    NGIN::Async::Task<int> ThrowOnce(NGIN::Async::TaskContext& ctx)
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

    NGIN::Async::Task<std::tuple<int, int>> AwaitWhenAll(NGIN::Async::TaskContext& ctx)
    {
        co_return co_await NGIN::Async::WhenAll(ctx, YieldOnce(ctx, 1), YieldOnce(ctx, 2));
    }

    NGIN::Async::Task<NGIN::UIntSize> AwaitWhenAnyWithLocalLoser(NGIN::Async::TaskContext& ctx)
    {
        auto loser = [&]() -> NGIN::Async::Task<int> {
            co_await ctx.YieldNow();
            co_await ctx.YieldNow();
            co_return 9;
        }();

        co_return co_await NGIN::Async::WhenAny(ctx, YieldOnce(ctx, 1), std::move(loser));
    }
}// namespace

TEST_CASE("WhenAll consumes tasks and returns tuple of results")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, NGIN::Async::WhenAll(ctx, YieldOnce(ctx, 1), YieldTwice(ctx, 2)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(std::get<0>(result.Value()) == 1);
    REQUIRE(std::get<1>(result.Value()) == 2);
}

TEST_CASE("WhenAll can be co_awaited directly")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, AwaitWhenAll(ctx));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(std::get<0>(result.Value()) == 1);
    REQUIRE(std::get<1>(result.Value()) == 2);
}

TEST_CASE("WhenAll consumes void tasks")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);
    int                      value = 0;

    auto operation =
            NGIN::Async::Spawn(ctx, NGIN::Async::WhenAll(ctx, VoidYieldOnce(ctx, value), VoidYieldOnce(ctx, value)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(value == 2);
}

TEST_CASE("WhenAny returns index of first completed task")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, NGIN::Async::WhenAny(ctx, YieldTwice(ctx, 1), YieldOnce(ctx, 2)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 1);
}

TEST_CASE("WhenAny returns canceled when context is already cancelled")
{
    ManualExecutor                  exec;
    NGIN::Async::CancellationSource source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(exec, source.GetToken());

    auto operation = NGIN::Async::Spawn(ctx, NGIN::Async::WhenAny(ctx, YieldOnce(ctx, 1), YieldOnce(ctx, 2)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsCanceled());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("WhenAll propagates child exception")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, NGIN::Async::WhenAll(ctx, ThrowOnce(ctx), YieldOnce(ctx, 2)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}

TEST_CASE("WhenAny returns index when a task faults")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, NGIN::Async::WhenAny(ctx, ThrowOnce(ctx), YieldTwice(ctx, 123)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 0);
}

TEST_CASE("WhenAny owns local loser task lifetime")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(ctx, AwaitWhenAnyWithLocalLoser(ctx));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 0);
}
