#include <catch2/catch_test_macros.hpp>

#include <array>
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
        co_await ctx.Yield();
        co_return value;
    }

    NGIN::Async::Task<int> YieldTwice(NGIN::Async::TaskContext& ctx, int value)
    {
        co_await ctx.Yield();
        co_await ctx.Yield();
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
