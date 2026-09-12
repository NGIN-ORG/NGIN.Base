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
    bool                     enter = false;
    bool                     timed = false;
    SECTION("initial task submission") {}
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
    auto operation = NGIN::Async::Spawn(ctx, work(ctx));
    if (enter)
        REQUIRE(scheduler.RunOne());
    scheduler.DiscardOrdinary();
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
    REQUIRE(operation.TakeResult().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::Stopped));
}

TEST_CASE("TaskContext WithLinkedCancellationToken cancels when parent token cancels")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource parentSource;
    NGIN::Async::CancellationSource childSource;

    NGIN::Async::TaskContext parentCtx(exec, parentSource.GetToken());
    auto                     childCtx = parentCtx.WithLinkedCancellationToken(childSource.GetToken());

    auto task = DelayForever(childCtx);
    auto op   = NGIN::Async::Spawn(childCtx, std::move(task));

    exec.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    parentSource.Cancel();
    exec.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
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

    auto task = DelayForever(ctx2);
    auto op   = NGIN::Async::Spawn(ctx2, std::move(task));

    exec.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    rootSource.Cancel();
    exec.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
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
    auto operation = NGIN::Async::Spawn(ctx, work(ctx, duration));
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsFaulted());
    REQUIRE(scheduler.PendingTimers() == 0);
}
TEST_CASE("Discarded cancellation-aware timers deliver a terminal fault", "[Async][Timer][Lifetime]")
{
    auto                            scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    NGIN::Async::CancellationSource source;
    NGIN::Async::TaskContext        ctx(*scheduler, source.GetToken());
    std::promise<void>              suspended;
    auto                            signal    = suspended.get_future();
    auto                            operation = NGIN::Async::Spawn(ctx, DelayForever(ctx));
    REQUIRE(scheduler->Execute(NGIN::Execution::WorkItem([&] { suspended.set_value(); })));
    signal.get();
    REQUIRE_FALSE(operation.IsCompleted());
    scheduler.reset();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
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

TEST_CASE("Submission handoff joins dispatch before admission returns", "[Async][Submission][Affinity]")
{
    NGIN::Tests::FailureMemoryResource resource;
    EarlySubmissionExecutor            scheduler(&resource);
    NGIN::Async::TaskContext           ctx(scheduler);
    bool                               entered         = false;
    bool                               correctExecutor = false;
    auto                               work            = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<int> {
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
    auto operation = NGIN::Async::Spawn(ctx, work(ctx));
    REQUIRE_FALSE(entered);
    const auto attempts = resource.AllocationAttempts();
    resource.FailNextAllocation();
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
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
        REQUIRE(result.IsFault());
        const auto reason = scheduler.behavior == EarlySubmissionExecutor::Behavior::Reject
                                    ? NGIN::Execution::ScheduleError::ResourceExhausted
                                    : NGIN::Execution::ScheduleError::Stopped;
        REQUIRE(result.Fault().native == static_cast<int>(reason));
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
    auto       operation = NGIN::Async::Spawn(ctx, work(ctx));
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
    REQUIRE(operation.TakeResult().Value() == 10000);
    REQUIRE(resource.AllocationAttempts() == attempts);
    REQUIRE(scheduler.completions.Outstanding() == 0);
}

TEST_CASE("Discarded child and producer submissions complete their admitted parents", "[Async][Submission][Lifetime]")
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
            co_await SubmissionVoid(context, entered);
        else
            (void) co_await SubmissionValue(context, entered);
    };
    auto operation = NGIN::Async::Spawn(ctx, parent(ctx));
    REQUIRE(scheduler.RunOne());
    if (useGenerator)
        REQUIRE(scheduler.RunOne());
    REQUIRE_FALSE(entered);
    scheduler.DiscardOrdinary();
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
    REQUIRE_FALSE(entered);
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
    auto operation = NGIN::Async::Spawn(parentContext, work(parentContext));
    parentScheduler.RunUntilIdle();
    foreignScheduler.DiscardOrdinary();
    parentScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    foreignScheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
}

TEST_CASE("TaskContext rejects token-free suspension without a completion path", "[Async][Submission]")
{
    NGIN::Execution::InlineScheduler scheduler;
    NGIN::Async::TaskContext         ctx(scheduler);
    auto                             work = [](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<void> {
        co_await context.YieldNow();
    };
    auto operation = NGIN::Async::Spawn(ctx, work(ctx));
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::Rejected));
}

TEST_CASE("Thread-pool shutdown retires an initial task while its worker is occupied", "[Async][Submission][Lifetime]")
{
    auto                     scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    const auto               executor  = NGIN::Execution::ExecutorRef::From(*scheduler);
    NGIN::Async::TaskContext ctx(executor);
    std::latch               occupied(1);
    std::latch               release(1);
    REQUIRE(executor.Execute(NGIN::Execution::WorkItem([&] { occupied.count_down(); release.wait(); })));
    occupied.wait();
    bool        entered   = false;
    auto        operation = NGIN::Async::Spawn(ctx, SubmissionValue(ctx, entered));
    std::thread shutdown([owner = std::move(scheduler)]() mutable { owner.reset(); });
    while (executor.Execute(NGIN::Execution::WorkItem([] {})))
        std::this_thread::yield();
    const bool completedInline = operation.IsCompleted();
    release.count_down();
    shutdown.join();
    REQUIRE_FALSE(completedInline);
    REQUIRE_FALSE(entered);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
}

TEST_CASE("Thread-pool shutdown retires token-free suspended timers", "[Async][Submission][Timer][Lifetime]")
{
    auto                     scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    NGIN::Async::TaskContext ctx(*scheduler);
    std::promise<void>       suspended;
    auto                     signal    = suspended.get_future();
    auto                     operation = NGIN::Async::Spawn(ctx, DelayForever(ctx));
    REQUIRE(scheduler->Execute(NGIN::Execution::WorkItem([&] { suspended.set_value(); })));
    signal.get();
    REQUIRE_FALSE(operation.IsCompleted());
    scheduler.reset();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
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
