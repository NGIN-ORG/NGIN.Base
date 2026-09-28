#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <latch>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../Support/FailureInjection.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
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

        void RunAllDelayed() noexcept
        {
            for (auto& item: m_delayed)
            {
                (void) Execute(std::move(item));
            }
            m_delayed.clear();
        }

    private:
        NGIN::Execution::detail::CompletionQueue m_completions;
        std::vector<NGIN::Execution::WorkItem>   m_ready;
        std::vector<NGIN::Execution::WorkItem>   m_delayed;
    };

    class RejectingExecutor final
    {
    public:
        [[nodiscard]] NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem) noexcept
        {
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        }

        [[nodiscard]] NGIN::Execution::ScheduleResult ExecuteAt(
                NGIN::Execution::WorkItem,
                NGIN::Time::TimePoint) noexcept
        {
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        }
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

    NGIN::Async::Task<void> ImmediateFrameProbe(NGIN::Tests::FrameLifetimeStatistics& statistics)
    {
        NGIN::Tests::FrameLifetimeProbe probe(statistics);
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
        co_return 0;
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

    NGIN::Async::Task<void> ProbeFrameLifetime(
            NGIN::Async::TaskContext&             ctx,
            NGIN::Tests::FrameLifetimeStatistics& statistics)
    {
        NGIN::Tests::FrameLifetimeProbe probe(statistics);
        co_await ctx.YieldNow();
        co_return;
    }

    NGIN::Async::Task<void> ProbeCanceledFrameLifetime(
            NGIN::Async::TaskContext&             ctx,
            NGIN::Tests::FrameLifetimeStatistics& statistics)
    {
        NGIN::Tests::FrameLifetimeProbe probe(statistics);
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }
}// namespace

TEST_CASE("Destroying or replacing cold tasks never starts their bodies")
{
    int  entered  = 0;
    auto makeTask = [&]() -> NGIN::Async::Task<void> {
        ++entered;
        co_return;
    };
    {
        auto task = makeTask();
        REQUIRE_FALSE(task.IsStarted());
        REQUIRE_FALSE(task.IsCompleted());
        auto moved = std::move(task);
        moved      = makeTask();
        REQUIRE(entered == 0);
    }
    REQUIRE(entered == 0);
}

TEST_CASE("Spawn consumes a cold task and returns a running operation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AddAfterYield(ctx, 2, 5);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 7);
}

TEST_CASE("Scoped spawn reports scheduler admission rejection")
{
    RejectingExecutor                     scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    NGIN::Execution::CooperativeScheduler ownerScheduler;
    NGIN::Async::TaskContext              ownerContext(ownerScheduler);
    NGIN::Async::TaskSupervisor<>         owner(ownerContext.GetEnvironment(), 1);
    auto                                  admitted = owner.SpawnOn(ctx.GetExecutor(), AddAfterYield(ctx, 2, 5));
    REQUIRE_FALSE(admitted);
    REQUIRE(admitted.error() == NGIN::Execution::ScheduleError::Rejected);
    REQUIRE(owner.TryJoin());
}

TEST_CASE("Task continuation publication racing completion has no lost wakeup")
{
    using Runtime       = NGIN::Async::detail::PromiseRuntimeCommon;
    using InstallResult = Runtime::ContinuationInstallResult;

    for (int iteration = 0; iteration < 256; ++iteration)
    {
        Runtime runtime;
        // Concurrent publication belongs to a running operation boundary.
        runtime.m_execution = Runtime::ExecutionState::Create(Runtime::ContinuationState::Empty);
        REQUIRE(runtime.m_execution);
        std::latch              start(3);
        InstallResult           installResult = InstallResult::AlreadyInstalled;
        std::coroutine_handle<> completedContinuation {};

        std::thread installer([&] {
            start.count_down();
            start.wait();
            installResult = runtime.TryInstallContinuation(
                    std::noop_coroutine(),
                    nullptr
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    ,
                    nullptr
#endif
            );
        });
        std::thread completer([&] {
            start.count_down();
            start.wait();
            completedContinuation = runtime.CompleteContinuation();
        });
        start.count_down();
        start.wait();
        installer.join();
        completer.join();

        REQUIRE(installResult != InstallResult::AlreadyInstalled);
        if (installResult == InstallResult::Installed)
            CHECK(completedContinuation == std::noop_coroutine());
        else
            CHECK_FALSE(completedContinuation);
    }
}

TEST_CASE("Operation reports invalid result access before completion")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AddAfterYield(ctx, 2, 3);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();

    auto early = operation.TakeResult();
    REQUIRE_FALSE(early);
    REQUIRE((early.HasError() && early.Error().IsFault()));

    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
}

TEST_CASE("Operation result can be taken only once")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AddAfterYield(ctx, 4, 9);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(operationRoot.TryJoin());
    auto first = operation.TakeResult();
    REQUIRE(first);
    REQUIRE(first.Value() == 13);

    auto second = operation.TakeResult();
    REQUIRE_FALSE(second);
    REQUIRE((second.HasError() && second.Error().IsFault()));
}

namespace
{
    NGIN::Async::Task<void> JoinSupervisor(NGIN::Async::TaskSupervisor<>& owner)
    {
        (void) co_await owner.Join();
    }
}// namespace

TEST_CASE("Supervisor transfer explicitly owns application work")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    int                                   value = 0;

    NGIN::Async::TaskSupervisor supervisor(ctx.GetEnvironment(), 1);
    REQUIRE(supervisor.Transfer(IncrementAfterYield(ctx, value)));
    auto joinedTask = JoinSupervisor(supervisor);

    NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(ctx.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(value == 1);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());
}

TEST_CASE("Transferred task destroys its frame exactly once after completion")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::Tests::FrameLifetimeStatistics  statistics;

    NGIN::Async::TaskSupervisor supervisor(context.GetEnvironment(), 1);
    REQUIRE(supervisor.Transfer(ProbeFrameLifetime(context, statistics)));
    auto joinedTask = JoinSupervisor(supervisor);

    NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());

    CHECK(statistics.constructions.load(std::memory_order_relaxed) == 1U);
    CHECK(statistics.destructions.load(std::memory_order_relaxed) == 1U);
    CHECK(statistics.live.load(std::memory_order_relaxed) == 0U);
}

TEST_CASE("Transferred canceled task destroys its suspended frame exactly once")
{
    ManualTimerExecutor                  scheduler;
    NGIN::Async::CancellationSource      source;
    NGIN::Async::TaskContext             context(scheduler, source.GetToken());
    NGIN::Tests::FrameLifetimeStatistics statistics;

    NGIN::Async::TaskSupervisor supervisor(context.GetEnvironment(), 1);
    REQUIRE(supervisor.Transfer(ProbeCanceledFrameLifetime(context, statistics)));
    scheduler.RunUntilIdle();
    REQUIRE(statistics.live.load(std::memory_order_relaxed) == 1U);

    source.Cancel();
    CHECK(statistics.live.load(std::memory_order_relaxed) == 1U);

    scheduler.RunUntilIdle();
    CHECK(statistics.live.load(std::memory_order_relaxed) == 0U);

    // The stale timer retains inert arbitration state, not a coroutine lease.
    scheduler.RunAllDelayed();
    scheduler.RunUntilIdle();

    NGIN::Async::TaskContext joinContext(scheduler);
    auto                     joinedTask = JoinSupervisor(supervisor);

    NGIN::Async::TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(joinContext.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Succeeded());

    CHECK(statistics.constructions.load(std::memory_order_relaxed) == 1U);
    CHECK(statistics.destructions.load(std::memory_order_relaxed) == 1U);
    CHECK(statistics.live.load(std::memory_order_relaxed) == 0U);
}

TEST_CASE("Operation result release racing completion destroys each owned frame exactly once")
{
    for (std::size_t iteration = 0; iteration < 128; ++iteration)
    {
        NGIN::Execution::ThreadPoolScheduler scheduler(2);
        NGIN::Async::TaskContext             context(scheduler);
        NGIN::Tests::FrameLifetimeStatistics statistics;

        NGIN::Async::TaskSupervisor<> owner(context.GetEnvironment(), 1);
        auto                          operation = owner.Spawn(ImmediateFrameProbe(statistics)).value();
        operation                               = {};
        REQUIRE(NGIN::Async::SyncWait(context, JoinSupervisor(owner)).Succeeded());

        CHECK(statistics.constructions.load(std::memory_order_relaxed) == 1U);
        CHECK(statistics.destructions.load(std::memory_order_relaxed) == 1U);
        CHECK(statistics.live.load(std::memory_order_relaxed) == 0U);
    }
}

TEST_CASE("Supervisor transfer racing immediate completion destroys each frame exactly once")
{
    for (std::size_t iteration = 0; iteration < 128; ++iteration)
    {
        NGIN::Execution::ThreadPoolScheduler scheduler(2);
        NGIN::Async::TaskContext             context(scheduler);
        NGIN::Tests::FrameLifetimeStatistics statistics;

        NGIN::Async::TaskSupervisor supervisor(context.GetEnvironment(), 1);
        REQUIRE(supervisor.Transfer(ImmediateFrameProbe(statistics)));
        REQUIRE(NGIN::Async::SyncWait(context, JoinSupervisor(supervisor)).Succeeded());

        CHECK(statistics.constructions.load(std::memory_order_relaxed) == 1U);
        CHECK(statistics.destructions.load(std::memory_order_relaxed) == 1U);
        CHECK(statistics.live.load(std::memory_order_relaxed) == 0U);
    }
}

TEST_CASE("Task can be awaited without spawning the child explicitly")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AwaitChild(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 3);
}

TEST_CASE("Task domain errors propagate through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AwaitDomainError(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().code == 17);
}

TEST_CASE("Task cancellation: Delay is woken by cancellation")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto operationTask = DelayForever(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();

    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    source.Cancel();
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsStopped());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsStopped());
}

TEST_CASE("Task cancellation: CancelAt wakes Delay without firing timers")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(exec, source.GetToken());

    auto operationTask = DelayForever(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();

    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    REQUIRE(source.CancelAt(ctx.GetExecutor(), NGIN::Time::TimePoint::FromNanoseconds(1)));
    exec.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());

    exec.RunAllDelayed();
    exec.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsStopped());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsStopped());
}

TEST_CASE("Task propagates exceptions through co_await")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto operationTask = AwaitChildThatThrows(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();

    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.HasError());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE((result.HasError() && result.Error().IsFault()));
}
