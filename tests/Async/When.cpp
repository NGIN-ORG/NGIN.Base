#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/WhenAny.hpp>
#include <NGIN/Units.hpp>

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
        co_await ctx.Yield();
        co_return value;
    }

    NGIN::Async::Task<int> YieldTwice(NGIN::Async::TaskContext& ctx, int value)
    {
        co_await ctx.Yield();
        co_await ctx.Yield();
        co_return value;
    }

    NGIN::Async::Task<void> SuspendForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Yield();
        co_await std::suspend_always {};
        co_return;
    }

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

    private:
        std::vector<NGIN::Execution::WorkItem> m_ready;
        std::vector<NGIN::Execution::WorkItem> m_delayed;
    };

    NGIN::Async::Task<void> NeverCompletes(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }

    NGIN::Async::Task<int> ThrowOnce(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Yield();
        throw std::runtime_error("boom");
    }

    NGIN::Async::Task<int> Immediate(NGIN::Async::TaskContext&, int value)
    {
        co_return value;
    }
}// namespace

TEST_CASE("WhenAll returns tuple of results")
{
    ManualExecutor        exec;
    NGIN::Async::TaskContext ctx(exec);

    auto a = YieldOnce(ctx, 1);
    auto b = YieldTwice(ctx, 2);

    auto all = NGIN::Async::WhenAll(ctx, a, b);
    all.Start(ctx);

    exec.RunUntilIdle();

    REQUIRE(all.IsCompleted());
    auto result = all.Get();
    REQUIRE(std::get<0>(result) == 1);
    REQUIRE(std::get<1>(result) == 2);
}

TEST_CASE("WhenAll can be co_awaited without calling Start() on the WhenAll task")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto root = [](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<std::tuple<int, int>> {
        auto a = YieldOnce(ctx, 1);
        auto b = YieldOnce(ctx, 2);
        co_return co_await NGIN::Async::WhenAll(ctx, a, b);
    }(ctx);

    root.Start(ctx);
    exec.RunUntilIdle();

    REQUIRE(root.IsCompleted());
    const auto result = root.Get();
    REQUIRE(std::get<0>(result) == 1);
    REQUIRE(std::get<1>(result) == 2);
}

TEST_CASE("WhenAny returns index of first completed task")
{
    ManualExecutor        exec;
    NGIN::Async::TaskContext ctx(exec);

    auto a = YieldTwice(ctx, 1);
    auto b = YieldOnce(ctx, 2);

    auto any = NGIN::Async::WhenAny(ctx, a, b);
    any.Start(ctx);

    exec.RunUntilIdle();

    REQUIRE(any.IsCompleted());
    const auto idx = any.Get();
    REQUIRE(idx == 1);
}

TEST_CASE("WhenAny throws TaskCanceled when context is already cancelled")
{
    ManualExecutor               exec;
    NGIN::Async::CancellationSource source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(exec, source.GetToken());
    auto                     a = YieldOnce(ctx, 1);
    auto                     b = YieldOnce(ctx, 2);

    auto any = NGIN::Async::WhenAny(ctx, a, b);
    any.Start(ctx);

    exec.RunUntilIdle();

    REQUIRE(any.IsCompleted());
    REQUIRE(any.IsCanceled());
    REQUIRE_THROWS_AS(any.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("WhenAny wakes and throws TaskCanceled on cancellation")
{
    ManualTimerExecutor            exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto a = NeverCompletes(ctx);
    auto b = NeverCompletes(ctx);

    auto any = NGIN::Async::WhenAny(ctx, a, b);
    any.Start(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(any.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(any.IsCompleted());
    REQUIRE(any.IsCanceled());
    REQUIRE_THROWS_AS(any.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("WhenAll wakes and throws TaskCanceled even if children do not observe cancellation")
{
    ManualExecutor                 exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto a = SuspendForever(ctx);
    auto b = SuspendForever(ctx);

    auto all = NGIN::Async::WhenAll(ctx, a, b);
    all.Start(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(all.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(all.IsCompleted());
    REQUIRE(all.IsCanceled());
    REQUIRE_THROWS_AS(all.Get(), NGIN::Async::TaskCanceled);
}

TEST_CASE("WhenAll propagates child exception")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto a = ThrowOnce(ctx);
    auto b = YieldOnce(ctx, 2);

    auto all = NGIN::Async::WhenAll(ctx, a, b);
    all.Start(ctx);

    exec.RunUntilIdle();

    REQUIRE(all.IsCompleted());
    REQUIRE(all.IsFaulted());
    REQUIRE_THROWS_AS(all.Get(), std::runtime_error);
}

TEST_CASE("WhenAny returns index when a task faults")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto a = ThrowOnce(ctx);
    auto b = YieldTwice(ctx, 123);

    auto any = NGIN::Async::WhenAny(ctx, a, b);
    any.Start(ctx);

    exec.RunUntilIdle();

    REQUIRE(any.IsCompleted());
    REQUIRE(any.Get() == 0);
    REQUIRE_THROWS_AS(a.Get(), std::runtime_error);
}

TEST_CASE("WhenAny returns immediately if one input is already completed")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto a = YieldOnce(ctx, 1);
    a.Start(ctx);
    exec.RunUntilIdle();
    REQUIRE(a.IsCompleted());

    auto b = Immediate(ctx, 2); // left unstarted

    auto any = NGIN::Async::WhenAny(ctx, a, b);
    any.Start(ctx);
    exec.RunUntilIdle();

    REQUIRE(any.IsCompleted());
    REQUIRE(any.Get() == 0);
}
