#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>
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
        auto ReserveCompletion(NGIN::Execution::WorkItem item) noexcept
        {
            return m_completions.Reserve(std::move(item));
        }
        ManualTimerExecutor()
        {
            m_ready.reserve(256);
            m_delayed.reserve(256);
        }

        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem item) noexcept
        {
            m_ready.push_back(std::move(item));
            return {};
        }

        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint) noexcept
        {
            m_delayed.push_back(std::move(item));
            return {};
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            if (m_completions.RunOne())
                return true;
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
        NGIN::Execution::detail::CompletionQueue m_completions;
        std::vector<NGIN::Execution::WorkItem>   m_ready;
        std::vector<NGIN::Execution::WorkItem>   m_delayed;
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
        NGIN::Async::TaskScope<> scope(ctx.GetEnvironment(), 1);
        auto                     op     = scope.Spawn(Throws(ctx)).value();
        auto                     result = co_await op;
        (void) co_await scope.Join();
        if (!result)
        {
            co_await NGIN::Async::Faulted(result.Error().Fault());
        }
        co_return;
    }

    NGIN::Async::Task<int> AwaitOperationSuccess(NGIN::Async::TaskContext& ctx)
    {
        NGIN::Async::TaskScope<> scope(ctx.GetEnvironment(), 1);
        auto                     op     = scope.Spawn(Value(ctx)).value();
        auto                     result = co_await op;
        (void) co_await scope.Join();
        if (!result)
        {
            co_return result;
        }
        co_return *result * 3;
    }

    NGIN::Async::Task<void> AwaitOperationCancellation(NGIN::Async::TaskContext& ctx)
    {
        NGIN::Async::TaskScope<> scope(ctx.GetEnvironment(), 1);
        auto                     op     = scope.Spawn(DelayThenValue(ctx)).value();
        auto                     result = co_await op;
        (void) co_await scope.Join();
        if (result.IsStopped())
        {
            co_await NGIN::Async::Stopped();
        }
        co_return;
    }
}// namespace

TEST_CASE("Operation co_await exposes child fault as completion")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              ctx(scheduler, source.GetToken());

    auto opTask = AwaitOperationFault(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(opTask)::ErrorType> opRoot(ctx.GetEnvironment(), 1);

    auto op = opRoot.Spawn(std::move(opTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.HasError());
    REQUIRE(opRoot.TryJoin());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE((result.HasError() && result.Error().IsFault()));
}

TEST_CASE("Operation co_await exposes child success")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto opTask = AwaitOperationSuccess(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(opTask)::ErrorType> opRoot(ctx.GetEnvironment(), 1);

    auto op = opRoot.Spawn(std::move(opTask)).value();

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE_FALSE(op.HasError());
    REQUIRE(opRoot.TryJoin());
    auto result = op.TakeResult();
    REQUIRE(result);
    REQUIRE(*result == 21);
}

TEST_CASE("Operation co_await is woken by cancellation")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto opTask = AwaitOperationCancellation(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(opTask)::ErrorType> opRoot(ctx.GetEnvironment(), 1);

    auto op = opRoot.Spawn(std::move(opTask)).value();

    exec.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsStopped());
    REQUIRE(opRoot.TryJoin());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsStopped());
}
