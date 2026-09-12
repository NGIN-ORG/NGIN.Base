#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>

#include "../Support/FailureInjection.hpp"

#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>

namespace
{
    NGIN::Async::AsyncGenerator<int> ProduceValues(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.YieldNow();
        co_yield 2;
        co_await ctx.YieldNow();
        co_yield 3;
    }

#if NGIN_ASYNC_HAS_EXCEPTIONS
    NGIN::Async::AsyncGenerator<int> YieldThenThrow(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.YieldNow();
        throw std::runtime_error("boom");
    }
#endif

    NGIN::Async::AsyncGenerator<int> YieldOnceThenNever(NGIN::Async::TaskContext& ctx)
    {
        co_yield 1;
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
    }

    struct CaptureSuspension final
    {
        std::coroutine_handle<>& handle;
        bool                     await_ready() const noexcept { return false; }
        void                     await_suspend(std::coroutine_handle<> awaiting) const noexcept { handle = awaiting; }
        void                     await_resume() const noexcept {}
    };

    NGIN::Async::AsyncGenerator<int> SuspendsBeforeYield(NGIN::Async::TaskContext&, std::coroutine_handle<>& suspended)
    {
        co_await CaptureSuspension {suspended};
        co_yield 1;
    }

    NGIN::Async::AsyncGenerator<int> QueuedProducer(NGIN::Async::TaskContext& ctx, bool& started, bool& continued)
    {
        started = true;
        co_await ctx.YieldNow();
        continued = true;
        co_yield 7;
    }

    NGIN::Async::Task<int> ReadOne(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto next = co_await gen.Next(ctx);
        if (next)
        {
            co_return *next;
        }
        co_return 0;
    }

    NGIN::Async::Task<int> SumAll(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        int sum = 0;
        for (;;)
        {
            auto next = co_await gen.Next(ctx);
            if (next.IsEnd())
            {
                break;
            }
            sum += *next;
        }
        co_return sum;
    }

    NGIN::Async::Task<void> ConsumeThenCancel(NGIN::Async::TaskContext& ctx, NGIN::Async::AsyncGenerator<int>& gen)
    {
        auto first = co_await gen.Next(ctx);
        if (first)
        {
            (void) *first;
        }

        static_cast<void>(co_await gen.Next(ctx));
        co_return;
    }
}// namespace

TEST_CASE("AsyncGenerator yields values via Next(TaskContext)")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = ProduceValues(ctx);
    auto task = SumAll(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    auto result = op.TakeResult();
    REQUIRE(result);
    REQUIRE(*result == 6);
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
TEST_CASE("AsyncGenerator propagates exceptions from producer")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    auto gen  = YieldThenThrow(ctx);
    auto task = SumAll(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsFault());
}
#endif

TEST_CASE("AsyncGenerator Next observes TaskContext cancellation")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              ctx(scheduler, source.GetToken());

    auto gen  = YieldOnceThenNever(ctx);
    auto task = ConsumeThenCancel(ctx, gen);
    auto op   = NGIN::Async::Spawn(ctx, std::move(task));

    scheduler.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    source.Cancel();
    scheduler.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("AsyncGenerator faults concurrent Next consumers")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);

    std::coroutine_handle<> suspended;
    auto                    gen    = SuspendsBeforeYield(ctx, suspended);
    auto                    first  = ReadOne(ctx, gen);
    auto                    second = ReadOne(ctx, gen);

    auto firstOp  = NGIN::Async::Spawn(ctx, std::move(first));
    auto secondOp = NGIN::Async::Spawn(ctx, std::move(second));
    scheduler.RunUntilIdle();

    REQUIRE(suspended);
    const bool firstRejected  = firstOp.IsCompleted() && firstOp.IsFaulted();
    const bool secondRejected = secondOp.IsCompleted() && secondOp.IsFaulted();
    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem(suspended)));
    scheduler.RunUntilIdle();
    REQUIRE(firstRejected != secondRejected);
    REQUIRE(firstOp.IsCompleted());
    REQUIRE(secondOp.IsCompleted());
    REQUIRE((firstOp.IsFaulted() || secondOp.IsFaulted()));
}

TEST_CASE("AsyncGenerator cancellation joins queued producer access", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler producerScheduler;
    NGIN::Execution::CooperativeScheduler consumerScheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              producerContext(producerScheduler);
    NGIN::Async::TaskContext              consumerContext(consumerScheduler, source.GetToken());
    bool                                  started   = false;
    bool                                  continued = false;
    auto                                  generator = QueuedProducer(producerContext, started, continued);
    auto                                  operation = NGIN::Async::Spawn(consumerContext, generator.Next(consumerContext));
    consumerScheduler.RunUntilIdle();
    while (!started)
        REQUIRE(producerScheduler.RunOne());
    REQUIRE_FALSE(continued);
    source.Cancel();
    consumerScheduler.RunUntilIdle();
    const bool completedBeforeProducer = operation.IsCompleted();
    producerScheduler.RunUntilIdle();
    const bool completedOnProducer = operation.IsCompleted();
    consumerScheduler.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeProducer);
    REQUIRE(continued);
    REQUIRE_FALSE(completedOnProducer);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().IsCanceled());
}

namespace
{
    class GeneratorExecutor final
    {
    public:
        explicit GeneratorExecutor(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : completions(1, nullptr, nullptr, resource) {}
        auto ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
        {
            return completions.Reserve(std::move(work));
        }
        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem work) noexcept
        {
            if (reject)
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            return ordinary.Execute(std::move(work));
        }
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint at) noexcept
        {
            if (reject)
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            return ordinary.ExecuteAt(std::move(work), at);
        }
        bool RunOne() { return completions.RunOne() || ordinary.RunOne(); }
        void RunUntilIdle()
        {
            while (RunOne()) {}
        }
        void Close()
        {
            reject = true;
            completions.Close();
        }

        NGIN::Execution::CooperativeScheduler    ordinary;
        NGIN::Execution::detail::CompletionQueue completions;
        bool                                     reject {false};
    };

    NGIN::Async::AsyncGenerator<int> OwnedProducer(NGIN::Async::TaskContext& ctx,
                                                   std::shared_ptr<int> lifetime, bool& started)
    {
        started = true;
        co_await ctx.YieldNow();
        co_yield *lifetime;
    }

    NGIN::Async::AsyncGenerator<int> AffinityProducer(NGIN::Async::TaskContext& ctx, bool& correct)
    {
        correct = ctx.GetExecutor().IsCurrent();
        co_await ctx.YieldNow();
        correct = correct && ctx.GetExecutor().IsCurrent();
        co_yield 42;
    }
}// namespace

TEST_CASE("AsyncGenerator Next retains producer ownership across moves and destruction", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler producerScheduler;
    NGIN::Execution::CooperativeScheduler consumerScheduler;
    NGIN::Async::TaskContext              producerContext(producerScheduler);
    NGIN::Async::TaskContext              consumerContext(consumerScheduler);
    auto                                  lifetime  = std::make_shared<int>(42);
    std::weak_ptr<int>                    weak      = lifetime;
    bool                                  started   = false;
    auto                                  generator = OwnedProducer(producerContext, lifetime, started);
    lifetime.reset();
    auto next           = generator.Next(consumerContext);
    bool destroyPending = false;
    SECTION("destroy before starting the cold Next")
    {
        generator = {};
    }
    SECTION("move before starting the cold Next")
    {
        auto moved = std::move(generator);
        generator  = std::move(moved);
    }
    SECTION("destroy with producer work queued")
    {
        destroyPending = true;
    }
    auto operation = NGIN::Async::Spawn(consumerContext, std::move(next));
    consumerScheduler.RunUntilIdle();
    REQUIRE_FALSE(started);
    REQUIRE(producerScheduler.RunOne());
    REQUIRE(started);
    if (destroyPending)
        generator = {};
    REQUIRE_FALSE(weak.expired());
    producerScheduler.RunUntilIdle();
    consumerScheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value().Value() == 42);
    generator = {};
    operation = {};
    REQUIRE(weak.expired());
}

TEST_CASE("AsyncGenerator advances and resumes its consumer on their selected executors", "[Async][Affinity]")
{
    NGIN::Execution::CooperativeScheduler producerScheduler;
    NGIN::Execution::CooperativeScheduler consumerScheduler;
    NGIN::Async::TaskContext              producerContext(producerScheduler);
    NGIN::Async::TaskContext              consumerContext(consumerScheduler);
    bool                                  producerCorrect = false;
    bool                                  consumerCorrect = false;
    auto                                  generator       = AffinityProducer(producerContext, producerCorrect);
    auto                                  consume         = [&](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<int> {
        auto next       = co_await generator.Next(ctx);
        consumerCorrect = ctx.GetExecutor().IsCurrent();
        co_return next.Value();
    };
    auto operation = NGIN::Async::Spawn(consumerContext, consume(consumerContext));
    consumerScheduler.RunUntilIdle();
    REQUIRE_FALSE(producerCorrect);
    producerScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    consumerScheduler.RunUntilIdle();
    REQUIRE(operation.TakeResult().Value() == 42);
    REQUIRE(producerCorrect);
    REQUIRE(consumerCorrect);
}

TEST_CASE("AsyncGenerator preserves reserved consumer delivery after admission closes", "[Async][Reservation]")
{
    GeneratorExecutor        producerScheduler;
    GeneratorExecutor        consumerScheduler;
    NGIN::Async::TaskContext producerContext(producerScheduler);
    NGIN::Async::TaskContext consumerContext(consumerScheduler);
    bool                     started   = false;
    bool                     continued = false;
    auto                     generator = QueuedProducer(producerContext, started, continued);
    auto                     operation = NGIN::Async::Spawn(consumerContext, generator.Next(consumerContext));
    consumerScheduler.RunUntilIdle();
    REQUIRE(producerScheduler.RunOne());
    REQUIRE(started);
    REQUIRE_FALSE(continued);
    REQUIRE(producerScheduler.completions.Outstanding() == 1);
    REQUIRE(consumerScheduler.completions.Outstanding() == 1);
    producerScheduler.Close();
    consumerScheduler.Close();
    producerScheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    consumerScheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().Value().Value() == 7);
    REQUIRE(producerScheduler.completions.Outstanding() == 0);
    REQUIRE(consumerScheduler.completions.Outstanding() == 0);
}

TEST_CASE("AsyncGenerator rejects failed producer admission before entering its body", "[Async][Reservation]")
{
    NGIN::Tests::FailureMemoryResource     resource;
    GeneratorExecutor                      producerScheduler(&resource);
    NGIN::Execution::InlineScheduler       unsupportedScheduler;
    NGIN::Execution::CooperativeScheduler  consumerScheduler;
    NGIN::Async::TaskContext               producerContext(producerScheduler);
    NGIN::Async::TaskContext               consumerContext(consumerScheduler);
    NGIN::Execution::CompletionReservation occupied;
    auto                                   expected = NGIN::Execution::ScheduleError::ResourceExhausted;
    SECTION("capacity")
    {
        auto ticket = producerScheduler.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
        REQUIRE(ticket);
        occupied = std::move(*ticket);
    }
    SECTION("allocation")
    {
        resource.FailNextAllocation();
    }
    SECTION("stopped reservation")
    {
        producerScheduler.Close();
        expected = NGIN::Execution::ScheduleError::Stopped;
    }
    SECTION("rejected initial submission")
    {
        producerScheduler.reject = true;
        expected                 = NGIN::Execution::ScheduleError::Stopped;
    }
    SECTION("unsupported executor")
    {
        producerContext.BindExecutor(unsupportedScheduler);
        expected = NGIN::Execution::ScheduleError::Rejected;
    }
    bool started   = false;
    bool continued = false;
    auto generator = QueuedProducer(producerContext, started, continued);
    auto operation = NGIN::Async::Spawn(consumerContext, generator.Next(consumerContext));
    consumerScheduler.RunUntilIdle();
    producerScheduler.RunUntilIdle();
    consumerScheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().native == static_cast<int>(expected));
    REQUIRE_FALSE(started);
    REQUIRE_FALSE(continued);
    occupied.Reset();
    REQUIRE(producerScheduler.completions.Outstanding() == 0);
}

TEST_CASE("AsyncGenerator cancellation waits for uncancelable producer suspension", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              ctx(scheduler, source.GetToken());
    std::coroutine_handle<>               suspended;
    auto                                  generator = SuspendsBeforeYield(ctx, suspended);
    auto                                  operation = NGIN::Async::Spawn(ctx, generator.Next(ctx));
    scheduler.RunUntilIdle();
    REQUIRE(suspended);
    source.Cancel();
    scheduler.RunUntilIdle();
    const bool premature = operation.IsCompleted();
    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem(suspended)));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(premature);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().IsCanceled());
}

TEST_CASE("AsyncGenerator supports concurrent executor delivery across repeated advances", "[Async][Lifetime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(4);
    NGIN::Async::TaskContext             ctx(scheduler);
    for (int iteration = 0; iteration < 2000; ++iteration)
    {
        auto generator = ProduceValues(ctx);
        auto result    = NGIN::Async::SyncWait(ctx, SumAll(ctx, generator));
        REQUIRE(result);
        REQUIRE(result.Value() == 6);
    }
}

namespace
{
    NGIN::Async::Task<int> ExternalValue(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return 42;
    }

    NGIN::Async::AsyncGenerator<int> ObserveExternal(NGIN::Async::TaskContext&,
                                                     NGIN::Async::Operation<int>& operation, bool owned)
    {
        auto result = owned ? co_await std::move(operation) : co_await operation;
        co_yield result.Value();
    }
}// namespace

TEST_CASE("AsyncGenerator joins Operation delivery on its reserved producer continuation", "[Async][Affinity][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler producerScheduler;
    NGIN::Execution::CooperativeScheduler consumerScheduler;
    NGIN::Execution::CooperativeScheduler childScheduler;
    NGIN::Async::TaskContext              producerContext(producerScheduler);
    NGIN::Async::TaskContext              consumerContext(consumerScheduler);
    NGIN::Async::TaskContext              childContext(childScheduler);
    bool                                  owned = false;
    SECTION("borrowed operation") {}
    SECTION("owned operation")
    {
        owned = true;
    }
    auto child     = NGIN::Async::Spawn(childContext, ExternalValue(childContext));
    auto generator = ObserveExternal(producerContext, child, owned);
    auto next      = NGIN::Async::Spawn(consumerContext, generator.Next(consumerContext));
    consumerScheduler.RunUntilIdle();
    producerScheduler.RunUntilIdle();
    REQUIRE_FALSE(next.IsCompleted());
    childScheduler.RunUntilIdle();
    REQUIRE_FALSE(next.IsCompleted());
    producerScheduler.RunUntilIdle();
    REQUIRE_FALSE(next.IsCompleted());
    consumerScheduler.RunUntilIdle();
    REQUIRE(next.IsCompleted());
    REQUIRE(next.TakeResult().Value().Value() == 42);
}

TEST_CASE("AsyncGenerator empty and abandoned cold advances need no execution lifetime", "[Async][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    SECTION("empty generator")
    {
        NGIN::Async::AsyncGenerator<int> generator;
        auto                             next = NGIN::Async::Spawn(ctx, generator.Next(ctx));
        scheduler.RunUntilIdle();
        REQUIRE(next.TakeResult().Value().IsEnd());
    }
    SECTION("cold advance abandoned after its generator is destroyed")
    {
        auto               lifetime  = std::make_shared<int>(42);
        std::weak_ptr<int> weak      = lifetime;
        bool               started   = false;
        auto               generator = OwnedProducer(ctx, std::move(lifetime), started);
        auto               next      = generator.Next(ctx);
        generator                    = {};
        REQUIRE_FALSE(weak.expired());
        next = {};
        REQUIRE(weak.expired());
        REQUIRE_FALSE(started);
    }
    SECTION("pre-canceled consumer starts no producer work")
    {
        NGIN::Async::CancellationSource source;
        auto                            canceledContext = ctx.WithCancellationToken(source.GetToken());
        source.Cancel();
        bool started   = false;
        bool continued = false;
        auto generator = QueuedProducer(ctx, started, continued);
        auto next      = NGIN::Async::Spawn(canceledContext, generator.Next(canceledContext));
        scheduler.RunUntilIdle();
        REQUIRE(next.TakeResult().IsCanceled());
        REQUIRE_FALSE(started);
    }
}

namespace
{
    NGIN::Async::AsyncGenerator<int> CancelingProducer(NGIN::Async::TaskContext&           ctx,
                                                       NGIN::Async::CancellationSource     source,
                                                       std::shared_ptr<std::promise<void>> retired)
    {
        struct Retirement final
        {
            std::shared_ptr<std::promise<void>> signal;
            ~Retirement() { signal->set_value(); }
        } retirement {std::move(retired)};
        co_await ctx.YieldNow();
        source.Cancel();
        co_await ctx.YieldNow();
        co_yield 1;
    }

    NGIN::Async::Task<void> ConsumeOwnedCanceledProducer(NGIN::Async::TaskContext&           ctx,
                                                         NGIN::Async::CancellationSource     source,
                                                         std::shared_ptr<std::promise<void>> retired)
    {
        auto generator = CancelingProducer(ctx, std::move(source), std::move(retired));
        (void) co_await generator.Next(ctx);
    }
}// namespace

TEST_CASE("AsyncGenerator retires owned canceled producers under concurrent delivery", "[Async][Lifetime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(4);
    for (int iteration = 0; iteration < 2000; ++iteration)
    {
        NGIN::Async::CancellationSource source;
        NGIN::Async::TaskContext        ctx(scheduler, source.GetToken());
        auto                            retired    = std::make_shared<std::promise<void>>();
        auto                            retirement = retired->get_future();
        auto                            result     = NGIN::Async::SyncWait(ctx, ConsumeOwnedCanceledProducer(ctx, source, std::move(retired)));
        REQUIRE(result.IsCanceled());
        REQUIRE(retirement.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    }
}
