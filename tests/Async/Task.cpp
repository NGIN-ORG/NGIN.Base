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

    struct MappedError final
    {
        int mappedCode {0};
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

    NGIN::Async::Task<int> CancelledDelayTask(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Milliseconds(1.0));
        co_return 123;
    }

    NGIN::Async::Task<void> CancelledYieldTask(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return;
    }

    NGIN::Async::Task<void> CancelledThrowTask(NGIN::Async::TaskContext& ctx)
    {
        if (ctx.CheckCancellation())
        {
            co_await NGIN::Async::Canceled();
            co_return;
        }
        co_return;
    }

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }

    NGIN::Async::Task<int> AddAfterYield(NGIN::Async::TaskContext& ctx, int a, int b)
    {
        co_await ctx.YieldNow();
        co_return a + b;
    }

    NGIN::Async::Task<int> ThrowAfterYield(NGIN::Async::TaskContext& ctx)
    {
#if NGIN_ASYNC_HAS_EXCEPTIONS
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
#else
        co_await ctx.YieldNow();
        co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::Unknown));
#endif
    }

    NGIN::Async::Task<int> AwaitChild(NGIN::Async::TaskContext& ctx)
    {
        auto child = AddAfterYield(ctx, 1, 2);
        co_return co_await child;
    }

    NGIN::Async::Task<int, TestDomainError> DomainErrorAfterYield(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return TestDomainError {17};
    }

    NGIN::Async::Task<void, TestDomainError> VoidDomainErrorAfterYield(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_await NGIN::Async::DomainFailure(TestDomainError {23});
        co_return;
    }

    NGIN::Async::Task<void> AwaitChildThatThrows(NGIN::Async::TaskContext& ctx)
    {
        auto child = ThrowAfterYield(ctx);
        static_cast<void>(co_await child);
        co_return;
    }

#if NGIN_ASYNC_HAS_EXCEPTIONS
    NGIN::Async::Task<int> CatchThrownDomainError(NGIN::Async::TaskContext& ctx)
    {
        try
        {
            static_cast<void>(co_await DomainErrorAfterYield(ctx).AsThrowing());
        } catch (const NGIN::Async::AsyncDomainErrorException<TestDomainError>& ex)
        {
            co_return ex.Error().code;
        }

        co_return -1;
    }
#endif
}// namespace

TEST_CASE("Task cancellation: Delay returns canceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledDelayTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task cancellation: Delay is woken by cancellation")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
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

TEST_CASE("Task cancellation: CancelAt wakes Delay without firing timers")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto task = DelayForever(ctx);
    task.Schedule(ctx);

    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    source.CancelAt(ctx.GetExecutor(), NGIN::Time::TimePoint::FromNanoseconds(1));
    exec.RunUntilIdle();
    REQUIRE_FALSE(task.IsCompleted());

    exec.RunAllDelayed();// execute the cancel job
    exec.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task cancellation: Yield returns canceled when already cancelled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledYieldTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task cancellation: CheckCancellation returns canceled")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledThrowTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Task can be awaited without calling Start() on the child task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChild(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsFaulted());
    REQUIRE_FALSE(task.IsCanceled());
    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 3);
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
TEST_CASE("Task AsThrowing().Get returns successful values")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AddAfterYield(ctx, 2, 5);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.AsThrowing().Get() == 7);
}

TEST_CASE("Task AsThrowing().Get throws typed domain errors")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = DomainErrorAfterYield(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE_FALSE(task.IsCanceled());
    REQUIRE_FALSE(task.IsFaulted());

    try
    {
        static_cast<void>(task.AsThrowing().Get());
        FAIL("expected typed domain error");
    } catch (const NGIN::Async::AsyncDomainErrorException<TestDomainError>& ex)
    {
        REQUIRE(ex.Error().code == 17);
    }
}

TEST_CASE("Task AsThrowing awaiter converts domain errors into exceptions")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = CatchThrownDomainError(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 17);
}

TEST_CASE("Task AsCompletion exposes terminal completion without propagation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = [](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<int> {
        auto completion = co_await DomainErrorAfterYield(ctx).AsCompletion();
        if (completion.IsDomainError())
        {
            co_return completion.DomainError().code;
        }

        co_return -1;
    }(ctx);

    task.Schedule(ctx);
    scheduler.RunUntilIdle();

    const auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 17);
}

TEST_CASE("Task MapError remaps domain failures explicitly")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto source = DomainErrorAfterYield(ctx);
    auto task   = source.MapError([](const TestDomainError& error) {
        return MappedError {.mappedCode = error.code + 100};
    });
    task.Schedule(ctx);
    scheduler.RunUntilIdle();

    const auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().mappedCode == 117);
}

TEST_CASE("Task As<E2> remaps errors across domains")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto source = DomainErrorAfterYield(ctx);
    auto task   = source.As<MappedError>([](const TestDomainError& error) {
        return MappedError {.mappedCode = error.code * 2};
    });
    task.Schedule(ctx);
    scheduler.RunUntilIdle();

    const auto result = task.Get();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().mappedCode == 34);
}

TEST_CASE("Task AsThrowing().Get throws for void task domain errors")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = VoidDomainErrorAfterYield(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());

    try
    {
        task.AsThrowing().Get();
        FAIL("expected typed domain error");
    } catch (const NGIN::Async::AsyncDomainErrorException<TestDomainError>& ex)
    {
        REQUIRE(ex.Error().code == 23);
    }
}

TEST_CASE("Task AsThrowing is the explicit exception view")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = CatchThrownDomainError(ctx);
    task.Schedule(ctx);
    scheduler.RunUntilIdle();

    const auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 17);
}

TEST_CASE("Task AsThrowing().Get throws for canceled void tasks")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::CancellationSource  source;
    source.Cancel();

    NGIN::Async::TaskContext ctx(scheduler, source.GetToken());
    auto                     task = CancelledYieldTask(ctx);
    task.Schedule(ctx);

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsCanceled());
    REQUIRE_THROWS_AS(task.AsThrowing().Get(), NGIN::Async::AsyncCanceledException);
}

TEST_CASE("Task AsThrowing().Get rethrows captured exceptions from faults")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = ThrowAfterYield(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
    REQUIRE_THROWS_AS(task.AsThrowing().Get(), std::runtime_error);
#else
    REQUIRE_THROWS_AS(task.AsThrowing().Get(), NGIN::Async::AsyncFaultException);
#endif
}
#endif

TEST_CASE("Task propagates exceptions through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AwaitChildThatThrows(ctx);
    task.Schedule(ctx);

    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    REQUIRE(task.IsFaulted());
    auto result = task.Get();
    REQUIRE_FALSE(result);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
    REQUIRE(task.GetException() != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(task.GetException()), std::runtime_error);
#endif
}

TEST_CASE("TaskContext::Run starts and schedules a task")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto task = AddAfterYield(ctx, 2, 5);
    task.Schedule(ctx);
    scheduler.RunUntilIdle();

    REQUIRE(task.IsCompleted());
    auto result = task.Get();
    REQUIRE(result);
    REQUIRE(*result == 7);
}
