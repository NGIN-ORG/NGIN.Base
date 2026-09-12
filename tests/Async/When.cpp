#include <NGIN/Execution/detail/CompletionQueue.hpp>
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
        auto ReserveCompletion(NGIN::Execution::WorkItem item) noexcept
        {
            return m_completions.Reserve(std::move(item));
        }
        ManualExecutor()
        {
            m_queue.reserve(256);
        }

        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem item) noexcept
        {
            m_queue.push_back(std::move(item));
            return {};
        }

        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint) noexcept
        {
            return Execute(std::move(item));
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            if (m_completions.RunOne())
                return true;
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
        NGIN::Execution::detail::CompletionQueue m_completions;
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

    NGIN::Async::Task<void> RecordParallelStart(NGIN::Async::TaskContext& ctx, int& started)
    {
        ++started;
        co_await ctx.YieldNow();
        co_return;
    }

    NGIN::Async::Task<void, int> VoidDomainFailure(NGIN::Async::TaskContext& ctx, int error)
    {
        co_await ctx.YieldNow();
        co_await NGIN::Async::DomainFailure(error);
        co_return;
    }

    NGIN::Async::Task<void, int> VoidSuccess(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return;
    }

    NGIN::Async::Task<int> ThrowOnce(NGIN::Async::TaskContext& ctx)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
        co_return 0;
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

    NGIN::Async::Task<int> LocalLoser(NGIN::Async::TaskContext& ctx, int& parentLocal)
    {
        NGIN::Async::CancellationRegistration             registration;
        const NGIN::Async::CancellationRegistrationResult result = ctx.GetCancellationToken().Register(
                registration,
                {},
                {},
                +[](void* context) noexcept -> bool {
                    *static_cast<int*>(context) = 9;
                    return false;
                },
                &parentLocal);
        if (!result)
        {
            co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                    NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed));
        }
        co_await ctx.YieldNow();
        co_await ctx.YieldNow();
        co_return parentLocal;
    }

    NGIN::Async::Task<NGIN::UIntSize> AwaitWhenAnyWithLocalLoser(NGIN::Async::TaskContext& ctx)
    {
        int                  local  = 0;
        const NGIN::UIntSize winner = co_await NGIN::Async::WhenAny(
                ctx,
                [](NGIN::Async::TaskContext& child) { return YieldOnce(child, 1); },
                [&local](NGIN::Async::TaskContext& child) { return LocalLoser(child, local); });
        co_return local == 9 ? winner : winner + 100;
    }

    bool RecordCancellation(void* context) noexcept
    {
        *static_cast<bool*>(context) = true;
        return false;
    }

    NGIN::Async::Task<int> CancellationAwareLoser(NGIN::Async::TaskContext& ctx, bool& observed)
    {
        NGIN::Async::CancellationRegistration             registration;
        const NGIN::Async::CancellationRegistrationResult result = ctx.GetCancellationToken().Register(
                registration,
                {},
                {},
                &RecordCancellation,
                &observed);
        if (!result)
        {
            co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                    NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed));
        }
        co_await ctx.YieldNow();
        co_await ctx.YieldNow();
        co_return 2;
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

TEST_CASE("WhenAll propagates failures from void tasks")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation =
            NGIN::Async::Spawn(ctx, NGIN::Async::WhenAll(ctx, VoidDomainFailure(ctx, 42), VoidSuccess(ctx)));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError() == 42);
}

TEST_CASE("WhenAll starts every child before waiting for the first result")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);
    int                      started = 0;

    auto operation = NGIN::Async::Spawn(
            ctx,
            NGIN::Async::WhenAll(ctx, RecordParallelStart(ctx, started), RecordParallelStart(ctx, started)));

    REQUIRE(exec.RunOne());
    REQUIRE(exec.RunOne());
    REQUIRE(exec.RunOne());
    CHECK(started == 2);
    exec.RunUntilIdle();
    CHECK(operation.IsCompleted());
}

TEST_CASE("WhenAny returns index of first completed task")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(
            ctx,
            NGIN::Async::WhenAny(
                    ctx,
                    [](NGIN::Async::TaskContext& child) { return YieldTwice(child, 1); },
                    [](NGIN::Async::TaskContext& child) { return YieldOnce(child, 2); }));
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

    auto operation = NGIN::Async::Spawn(
            ctx,
            NGIN::Async::WhenAny(
                    ctx,
                    [](NGIN::Async::TaskContext& child) { return YieldOnce(child, 1); },
                    [](NGIN::Async::TaskContext& child) { return YieldOnce(child, 2); }));
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

TEST_CASE("WhenAny propagates a winning task fault after draining losers")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);

    auto operation = NGIN::Async::Spawn(
            ctx,
            NGIN::Async::WhenAny(
                    ctx,
                    [](NGIN::Async::TaskContext& child) { return ThrowOnce(child); },
                    [](NGIN::Async::TaskContext& child) { return YieldTwice(child, 123); }));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
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

TEST_CASE("WhenAny requests child-specific cancellation for losing tasks")
{
    ManualExecutor           exec;
    NGIN::Async::TaskContext ctx(exec);
    bool                     loserObservedCancellation = false;

    auto operation = NGIN::Async::Spawn(
            ctx,
            NGIN::Async::WhenAny(
                    ctx,
                    [](NGIN::Async::TaskContext& child) { return YieldOnce(child, 1); },
                    [&loserObservedCancellation](NGIN::Async::TaskContext& child) {
                        return CancellationAwareLoser(child, loserObservedCancellation);
                    }));
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    const auto result = operation.TakeResult();
    REQUIRE(result);
    CHECK(result.Value() == 0);
    CHECK(loserObservedCancellation);
}
