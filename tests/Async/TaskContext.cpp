#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../Support/FailureInjection.hpp"
#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <future>
#include <latch>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
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

        void DiscardOrdinary() noexcept
        {
            m_ready.clear();
            m_delayed.clear();
        }

    private:
        NGIN::Execution::detail::CompletionQueue m_completions;
        std::vector<NGIN::Execution::WorkItem>   m_ready;
        std::vector<NGIN::Execution::WorkItem>   m_delayed;
    };

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }
}// namespace

TEST_CASE("Discarded token-free submissions deliver a terminal fault", "[Async][Lifetime][Submission]")
{
    ManualTimerExecutor      scheduler;
    NGIN::Async::TaskContext ctx(scheduler);
    bool                     enter = true;
    bool                     timed = false;
    SECTION("queued yield")
    {
        enter = true;
    }
    SECTION("queued delay")
    {
        enter = true;
        timed = true;
    }
    auto work = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<void> {
        if (timed)
            co_await context.Delay(NGIN::Units::Seconds(60.0));
        else
            co_await context.YieldNow();
    };
    auto operationTask = work(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    if (enter)
        REQUIRE(scheduler.RunOne());
    scheduler.DiscardOrdinary();
    scheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.HasError());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::Stopped));
}

TEST_CASE("TaskContext WithLinkedCancellationToken cancels when parent token cancels")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource parentSource;
    NGIN::Async::CancellationSource childSource;

    NGIN::Async::TaskContext parentCtx(exec, parentSource.GetToken());
    auto                     childCtx = parentCtx.WithLinkedCancellationToken(childSource.GetToken());

    auto task   = DelayForever(childCtx);
    auto opTask = std::move(task);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(opTask)::ErrorType> opRoot(rootContext.GetEnvironment(), 1);

    auto op = opRoot.SpawnOn(childCtx.GetExecutor(), std::move(opTask)).value();

    exec.RunUntilIdle();

    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    parentSource.Cancel();
    exec.RunUntilIdle();
    rootExecutor.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsStopped());
    REQUIRE(opRoot.TryJoin());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsStopped());
}

TEST_CASE("TaskContext WithLinkedCancellationToken supports chaining without losing root linkage")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource rootSource;
    NGIN::Async::CancellationSource extra1;
    NGIN::Async::CancellationSource extra2;

    NGIN::Async::TaskContext ctx0(exec, rootSource.GetToken());
    auto                     ctx1 = ctx0.WithLinkedCancellationToken(extra1.GetToken());
    auto                     ctx2 = ctx1.WithLinkedCancellationToken(extra2.GetToken());

    auto task   = DelayForever(ctx2);
    auto opTask = std::move(task);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(opTask)::ErrorType> opRoot(rootContext.GetEnvironment(), 1);

    auto op = opRoot.SpawnOn(ctx2.GetExecutor(), std::move(opTask)).value();

    exec.RunUntilIdle();

    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    rootSource.Cancel();
    exec.RunUntilIdle();
    rootExecutor.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsStopped());
    REQUIRE(opRoot.TryJoin());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsStopped());
}
TEST_CASE("TaskContext rejects nonfinite delays without creating timers", "[Async][Timer]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    double                                duration = 0;
    SECTION("infinity")
    {
        duration = std::numeric_limits<double>::infinity();
    }
    SECTION("NaN")
    {
        duration = std::numeric_limits<double>::quiet_NaN();
    }
    auto work = [](NGIN::Async::TaskContext& context, double seconds) -> NGIN::Async::Task<void> {
        co_await context.Delay(NGIN::Units::Seconds(seconds));
    };
    auto operationTask = work(ctx, duration);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.HasError());
    REQUIRE(scheduler.PendingTimers() == 0);
    REQUIRE(operationRoot.TryJoin());
}
TEST_CASE("Discarded cancellation-aware timers deliver a terminal fault", "[Async][Timer][Lifetime]")
{
    auto                            scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(*scheduler, source.GetToken());
    std::promise<void>              suspended;
    auto                            signal        = suspended.get_future();
    auto                            operationTask = DelayForever(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    REQUIRE(scheduler->Execute(NGIN::Execution::WorkItem([&] { suspended.set_value(); })));
    signal.get();
    REQUIRE_FALSE(operation.IsCompleted());
    scheduler.reset();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.HasError());
    REQUIRE(operationRoot.TryJoin());
}

namespace
{
    class EarlySubmissionExecutor final
    {
    public:
        enum class Behavior
        {
            Run,
            Discard,
            Reject,
            RunOnThread
        };
        explicit EarlySubmissionExecutor(std::pmr::memory_resource* resource)
            : completions(1, nullptr, nullptr, resource) {}
        auto ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
        {
            return completions.Reserve(std::move(work));
        }
        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem work) noexcept
        {
            if (behavior == Behavior::Reject)
                return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
            if (behavior == Behavior::Run)
            {
                work.Invoke();
                work.Invoke();// The submission payload consumes its dispatch exactly once.
            }
            if (behavior == Behavior::RunOnThread)
            {
                std::thread worker([item = std::move(work)]() mutable { item.Invoke(); });
                worker.join();
            }
            return {};
        }
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint) noexcept
        {
            return Execute(std::move(work));
        }
        bool IsCurrent() const noexcept { return current; }
        bool RunOne()
        {
            current        = true;
            const bool ran = completions.RunOne();
            current        = false;
            return ran;
        }
        void RunUntilIdle()
        {
            while (RunOne()) {}
        }

        NGIN::Execution::detail::CompletionQueue completions;
        Behavior                                 behavior {Behavior::Run};
        bool                                     current {false};
    };

    NGIN::Async::Task<int> SubmissionValue(NGIN::Async::TaskContext&, bool& entered)
    {
        entered = true;
        co_return 42;
    }

    NGIN::Async::Task<void> SubmissionVoid(NGIN::Async::TaskContext&, bool& entered)
    {
        entered = true;
        co_return;
    }

    NGIN::Async::AsyncGenerator<int> SubmissionGenerator(NGIN::Async::TaskContext&, bool& entered)
    {
        entered = true;
        co_yield 42;
    }
}// namespace

TEST_CASE("Yield submission handoff joins dispatch before resuming", "[Async][Submission][Affinity]")
{
    NGIN::Tests::FailureMemoryResource resource;
    EarlySubmissionExecutor            scheduler(&resource);
    NGIN::Async::TaskContext           ctx(scheduler);
    bool                               entered         = false;
    bool                               correctExecutor = false;
    auto                               work            = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<int> {
        co_await context.YieldNow();
        entered         = true;
        correctExecutor = context.GetExecutor().IsCurrent();
        co_return 42;
    };
    SECTION("inline dispatch before submission returns") {}
    SECTION("another thread dispatches before submission returns")
    {
        scheduler.behavior = EarlySubmissionExecutor::Behavior::RunOnThread;
    }
    SECTION("discard before successful submission returns")
    {
        scheduler.behavior = EarlySubmissionExecutor::Behavior::Discard;
    }
    SECTION("immediate rejection preserves its reason")
    {
        scheduler.behavior = EarlySubmissionExecutor::Behavior::Reject;
    }
    auto operationTask = work(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    REQUIRE_FALSE(entered);
    const auto attempts = resource.AllocationAttempts();
    resource.FailNextAllocation();
    scheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    if (scheduler.behavior == EarlySubmissionExecutor::Behavior::Run ||
        scheduler.behavior == EarlySubmissionExecutor::Behavior::RunOnThread)
    {
        REQUIRE(result);
        REQUIRE(result.Value() == 42);
        REQUIRE(correctExecutor);
    }
    else
    {
        REQUIRE_FALSE(entered);
        REQUIRE((result.HasError() && result.Error().IsFault()));
        const auto reason = scheduler.behavior == EarlySubmissionExecutor::Behavior::Reject
                                    ? NGIN::Execution::ScheduleError::ResourceExhausted
                                    : NGIN::Execution::ScheduleError::Stopped;
        REQUIRE(result.Error().Fault().native == static_cast<int>(reason));
    }
    REQUIRE(resource.AllocationAttempts() == attempts);
    REQUIRE(scheduler.completions.Outstanding() == 0);
}

TEST_CASE("Token-free yields reuse a single admitted continuation without recursive dispatch", "[Async][Submission][Reservation]")
{
    NGIN::Tests::FailureMemoryResource resource;
    EarlySubmissionExecutor            scheduler(&resource);
    NGIN::Async::TaskContext           ctx(scheduler);
    int                                resumed = 0;
    auto                               work    = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<int> {
        for (int iteration = 0; iteration < 10000; ++iteration)
        {
            co_await context.YieldNow();
            ++resumed;
        }
        co_return resumed;
    };
    auto operationTask = work(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto       operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    const auto attempts  = resource.AllocationAttempts();
    resource.FailNextAllocation();
    scheduler.completions.Close();
    for (int expected = 0; expected < 10000; ++expected)
    {
        REQUIRE(scheduler.RunOne());
        REQUIRE(resumed == expected);
        REQUIRE_FALSE(operation.IsCompleted());
    }
    REQUIRE(scheduler.RunOne());
    rootExecutor.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 10000);
    REQUIRE(resource.AllocationAttempts() == attempts);
    REQUIRE(scheduler.completions.Outstanding() == 0);
}

TEST_CASE("Discarded ordinary submissions preserve scoped startup and retire generator startup", "[Async][Submission][Lifetime]")
{
    ManualTimerExecutor      scheduler;
    NGIN::Async::TaskContext ctx(scheduler);
    bool                     entered      = false;
    bool                     useVoid      = false;
    bool                     useGenerator = false;
    SECTION("value child") {}
    SECTION("void child")
    {
        useVoid = true;
    }
    SECTION("generator initial advance")
    {
        useGenerator = true;
    }
    auto generator = SubmissionGenerator(ctx, entered);
    auto parent    = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<void> {
        if (useGenerator)
            (void) co_await generator.Next(context);
        else if (useVoid)
        {
            NGIN::Async::TaskScope<> scope(context.GetEnvironment(), 1);
            auto                     child  = scope.Spawn(SubmissionVoid(context, entered)).value();
            auto                     result = co_await child;
            (void) co_await scope.Join();
            if ((result.HasError() && result.Error().IsFault()))
                co_await NGIN::Async::Faulted(std::move(result).Error().Fault());
        }
        else
        {
            NGIN::Async::TaskScope<> scope(context.GetEnvironment(), 1);
            auto                     child  = scope.Spawn(SubmissionValue(context, entered)).value();
            auto                     result = co_await child;
            (void) co_await scope.Join();
            if ((result.HasError() && result.Error().IsFault()))
                co_await NGIN::Async::Faulted(std::move(result).Error().Fault());
        }
    };
    auto operationTask = parent(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    REQUIRE(scheduler.RunOne());
    if (useGenerator)
        REQUIRE(scheduler.RunOne());
    REQUIRE_FALSE(entered);
    scheduler.DiscardOrdinary();
    scheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError() == useGenerator);
    REQUIRE(result.Succeeded() == !useGenerator);
    REQUIRE(entered == !useGenerator);
}

TEST_CASE("TaskContext preserves a different executor's discard delivery", "[Async][Submission][Affinity]")
{
    NGIN::Execution::CooperativeScheduler parentScheduler;
    ManualTimerExecutor                   foreignScheduler;
    NGIN::Async::TaskContext              parentContext(parentScheduler);
    NGIN::Async::TaskContext              foreignContext(foreignScheduler);
    auto                                  work = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> {
        co_await foreignContext.YieldNow();
    };
    auto operationTask = work(parentContext);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(parentContext.GetExecutor(), std::move(operationTask)).value();
    parentScheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    foreignScheduler.DiscardOrdinary();
    parentScheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    foreignScheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.HasError());
    REQUIRE(operationRoot.TryJoin());
}

TEST_CASE("TaskContext rejects token-free suspension without a completion path", "[Async][Submission]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    auto                                  untracked = NGIN::Execution::ExecutorRef(&scheduler, +[](void* value, NGIN::Execution::WorkItem work) noexcept { return static_cast<NGIN::Execution::CooperativeScheduler*>(value)->Execute(std::move(work)); }, +[](void* value, NGIN::Execution::WorkItem work, NGIN::Time::TimePoint time) noexcept { return static_cast<NGIN::Execution::CooperativeScheduler*>(value)->ExecuteAt(std::move(work), time); });
    NGIN::Async::TaskContext              ctx(untracked);
    auto                                  work = [](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<void> {
        co_await context.YieldNow();
    };
    auto operationTask = work(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::Rejected));
}

TEST_CASE("Thread-pool shutdown delivers reserved startup after its occupied worker is released", "[Async][Submission][Lifetime]")
{
    auto                     scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    const auto               executor  = NGIN::Execution::ExecutorRef::From(*scheduler);
    NGIN::Async::TaskContext ctx(executor);
    std::latch               occupied(1);
    std::latch               release(1);
    REQUIRE(executor.Execute(NGIN::Execution::WorkItem([&] { occupied.count_down(); release.wait(); })));
    occupied.wait();
    bool entered       = false;
    auto operationTask = SubmissionValue(ctx, entered);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto        operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    std::thread shutdown([owner = std::move(scheduler)]() mutable { owner.reset(); });
    while (executor.Execute(NGIN::Execution::WorkItem([] {})))
        std::this_thread::yield();
    const bool completedInline = operation.IsCompleted();
    release.count_down();
    shutdown.join();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(completedInline);
    REQUIRE(entered);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
}

TEST_CASE("Thread-pool shutdown retires token-free suspended timers", "[Async][Submission][Timer][Lifetime]")
{
    auto                     scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    NGIN::Async::TaskContext ctx(*scheduler);
    std::promise<void>       suspended;
    auto                     signal        = suspended.get_future();
    auto                     operationTask = DelayForever(ctx);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    REQUIRE(scheduler->Execute(NGIN::Execution::WorkItem([&] { suspended.set_value(); })));
    signal.get();
    REQUIRE_FALSE(operation.IsCompleted());
    scheduler.reset();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.HasError());
    REQUIRE(operationRoot.TryJoin());
}

TEST_CASE("Executor references compare dispatch bindings for continuation reuse", "[Async][Submission]")
{
    NGIN::Execution::CooperativeScheduler first;
    NGIN::Execution::CooperativeScheduler second;
    const auto                            original = NGIN::Execution::ExecutorRef::From(first);
    const auto                            copied   = original;
    REQUIRE(original == copied);
    REQUIRE(original == NGIN::Execution::ExecutorRef::From(first));
    REQUIRE_FALSE(original == NGIN::Execution::ExecutorRef::From(second));
    REQUIRE_FALSE(original == NGIN::Execution::ExecutorRef {});
}

TEST_CASE("TaskContext timer delivery inlines when eligible and yields retain deferred delivery", "[Async][Dispatch]")
{
    struct Counter final
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        int                                   deliveries {0};
        auto                                  Execute(NGIN::Execution::WorkItem work) noexcept { return scheduler.Execute(std::move(work)); }
        auto                                  ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint at) noexcept { return scheduler.ExecuteAt(std::move(work), at); }
        auto                                  ScheduleTimer(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint at) noexcept { return scheduler.ScheduleTimer(std::move(work), at); }
        auto                                  ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
        {
            return scheduler.ReserveCompletion(NGIN::Execution::WorkItem([this, work = std::move(work)]() mutable {
                ++deliveries;
                work.Invoke();
            }));
        }
        bool IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    };
    for (int mode = 0; mode != 3; ++mode)
    {
        Counter                         executor;
        NGIN::Async::CancellationSource cancellation;
        NGIN::Async::TaskContext        context(executor, cancellation.GetToken());
        bool                            resumed = false;
        auto                            task    = [&]() -> NGIN::Async::Task<void> {
            if (mode == 2)
                co_await context.YieldNow();
            else
                co_await context.Delay(NGIN::Units::Seconds(1));
            resumed = true;
        };
        auto operationTask = task();

        NGIN::Execution::CooperativeScheduler rootExecutor;

        NGIN::Async::TaskContext rootContext(rootExecutor);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

        auto operation = operationRoot.SpawnOn(context.GetExecutor(), std::move(operationTask)).value();
        REQUIRE(executor.scheduler.RunOne());
        const auto startupDeliveries = executor.deliveries;
        REQUIRE_FALSE(resumed);
        const auto later = NGIN::Time::TimePoint::FromNanoseconds((std::numeric_limits<std::uint64_t>::max)());
        if (mode == 1)
        {
            NGIN::Execution::detail::WorkBudget budget(1);
            REQUIRE(executor.scheduler.RunOneAt(later));
            REQUIRE_FALSE(resumed);
        }
        else
        {
            REQUIRE(executor.scheduler.RunOneAt(later));
            REQUIRE(resumed == (mode == 0));
        }
        executor.scheduler.RunUntilIdleAt(later);
        rootExecutor.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().Succeeded());
        REQUIRE(executor.deliveries - startupDeliveries == (mode == 0 ? 0 : 1));
    }
}
