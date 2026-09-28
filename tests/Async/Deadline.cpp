#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WithTimeout.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>

namespace
{
    using namespace NGIN::Async;
    using NGIN::Time::TimePoint;
    TimePoint Future()
    {
        return detail::DeadlineAfter(1e9).value();
    }
    struct Executor
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        int                                   timers {};
        bool                                  reject {};
        TimePoint                             scheduled;
        auto                                  Execute(NGIN::Execution::WorkItem work) noexcept { return scheduler.Execute(std::move(work)); }
        auto                                  ExecuteAt(NGIN::Execution::WorkItem work, TimePoint time) noexcept { return scheduler.ExecuteAt(std::move(work), time); }
        auto                                  ReserveCompletion(NGIN::Execution::WorkItem work) noexcept { return scheduler.ReserveCompletion(std::move(work)); }
        auto                                  ScheduleTimer(NGIN::Execution::WorkItem work, TimePoint time) noexcept
                -> std::expected<NGIN::Execution::TimerRegistration, NGIN::Execution::ScheduleError>
        {
            ++timers;
            scheduled = time;
            if (reject)
                return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
            return scheduler.ScheduleTimer(std::move(work), time);
        }
        bool IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    };
    Task<TimePoint> Nested(int depth, TimePoint deadline, Executor& executor, int expectedTimers)
    {
        if (depth)
            co_return co_await WithDeadline(deadline, Nested(depth - 1, deadline, executor, expectedTimers)).AsCompletion();
        REQUIRE(executor.scheduler.PendingTimers() == static_cast<std::size_t>(expectedTimers));
        const auto environment = co_await CurrentEnvironment();
        REQUIRE(environment.cancellation.HasState());
        co_return *environment.deadline;
    }
    struct ClaimedExecutor
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Execution::WorkItem             timer;
        auto                                  Execute(NGIN::Execution::WorkItem work) noexcept { return scheduler.Execute(std::move(work)); }
        auto                                  ExecuteAt(NGIN::Execution::WorkItem work, TimePoint time) noexcept { return scheduler.ExecuteAt(std::move(work), time); }
        auto                                  ReserveCompletion(NGIN::Execution::WorkItem work) noexcept { return scheduler.ReserveCompletion(std::move(work)); }
        auto                                  ScheduleTimer(NGIN::Execution::WorkItem work, TimePoint) noexcept
                -> std::expected<NGIN::Execution::TimerRegistration, NGIN::Execution::ScheduleError>
        {
            timer = std::move(work);
            return NGIN::Execution::TimerRegistration(this, 0, 0, +[](void*, std::uint64_t, std::uint64_t) noexcept { return false; });
        }
        bool IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    };
    struct Gate
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { detail::PromiseRuntimeCommon::ResumeRetained(handle); });
            return true;
        }
        void await_resume() const noexcept {}
    };
    Task<int> Value(bool& entered)
    {
        entered = true;
        co_return 42;
    }
    Task<void> Waiting(bool& entered)
    {
        TaskContext context(co_await CurrentEnvironment());
        entered = true;
        co_await context.Delay(NGIN::Units::Seconds(60));
    }
    struct Resource final : std::pmr::memory_resource
    {
        std::size_t calls {}, live {}, failAt = (std::numeric_limits<std::size_t>::max)();
        void*       do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (calls++ == failAt)
                throw std::bad_alloc();
            auto* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
            ++live;
            return pointer;
        }
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
        {
            --live;
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return &other == this; }
    };
}// namespace

TEST_CASE("Deadline helper depth shares the inherited timer and earlier deadline", "[Async][Deadline]")
{
    Executor    executor;
    TaskContext context(executor);
    const auto  outer         = Future();
    const auto  later         = TimePoint::FromNanoseconds(outer.ToNanoseconds() + 1'000'000'000);
    auto        operationTask = WithDeadline(outer, Nested(64, later, executor, 1));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == outer);
    REQUIRE(executor.timers == 1);
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("A stricter nested deadline owns one additional timer", "[Async][Deadline]")
{
    Executor    executor;
    TaskContext context(executor);
    const auto  inner         = Future();
    const auto  outer         = TimePoint::FromNanoseconds(inner.ToNanoseconds() + 1'000'000'000);
    auto        operationTask = WithDeadline(outer, WithDeadline(inner, Nested(16, outer, executor, 2)));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == inner);
    REQUIRE(executor.timers == 2);
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("Deadline expiry requests stop and retires inner timers", "[Async][Deadline]")
{
    Executor    executor;
    TaskContext context(executor);
    bool        entered       = false;
    const auto  deadline      = Future();
    auto        operationTask = WithDeadline(deadline, Waiting(entered));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(entered);
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE(executor.scheduler.PendingTimers() == 2);
    executor.scheduler.RunUntilIdleAt(deadline);
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.IsStopped());
    REQUIRE(result.StopReason() == StopReason::Deadline);
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("Deadline waits past expiry for noncooperative work and preserves its produced outcome", "[Async][Deadline][Lifetime]")
{
    Executor                  executor;
    TaskContext               context(executor);
    NGIN::Execution::WorkItem release;
    const bool                failed = GENERATE(false, true);
    auto                      inner  = [&]() -> Task<int, int> {
        co_await Gate {release};
        co_return failed ? Completion<int, int>::DomainFailure(23) : Completion<int, int>::Success(42);
    };
    const auto deadline      = Future();
    auto       operationTask = WithDeadline(deadline, inner());

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    executor.scheduler.RunUntilIdleAt(deadline);
    REQUIRE_FALSE(operation.IsCompleted());
    release.Invoke();
    release = {};
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    if (failed)
        REQUIRE(result.Error().DomainError() == 23);
    else
        REQUIRE(result.Value() == 42);
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("Deadline joins a claimed timer before releasing its boundary", "[Async][Deadline][Lifetime]")
{
    ClaimedExecutor executor;
    TaskContext     context(executor);
    bool            entered       = false;
    auto            operationTask = WithDeadline(Future(), Value(entered));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(entered);
    REQUIRE_FALSE(operation.IsCompleted());
    executor.timer.Invoke();
    executor.scheduler.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    const bool foreign = GENERATE(false, true);
    if (foreign)
    {
        std::thread retire([work = std::move(executor.timer)] {});
        while (!operation.IsCompleted())
            executor.scheduler.RunUntilIdle();
        retire.join();
    }
    else
    {
        executor.timer = {};
        executor.scheduler.RunUntilIdle();
    }
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("Deadline admission failure starts no inner work", "[Async][Deadline][Admission]")
{
    struct UnsupportedExecutor
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        auto                                  Execute(NGIN::Execution::WorkItem work) noexcept { return scheduler.Execute(std::move(work)); }
        auto                                  ExecuteAt(NGIN::Execution::WorkItem work, TimePoint time) noexcept { return scheduler.ExecuteAt(std::move(work), time); }
        auto                                  ReserveCompletion(NGIN::Execution::WorkItem work) noexcept { return scheduler.ReserveCompletion(std::move(work)); }
        bool                                  IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    } unsupported;
    Executor rejected;
    rejected.reject       = true;
    bool        entered   = false;
    const bool  supported = GENERATE(false, true);
    TaskContext context(supported ? NGIN::Execution::ExecutorRef::From(rejected) : NGIN::Execution::ExecutorRef::From(unsupported));
    auto        operationTask = WithDeadline(Future(), Value(entered));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    unsupported.scheduler.RunUntilIdle();
    rejected.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError());
    REQUIRE_FALSE(entered);
    REQUIRE(result.Error().Fault().native == static_cast<int>(supported ? NGIN::Execution::ScheduleError::ResourceExhausted : NGIN::Execution::ScheduleError::Rejected));
}

TEST_CASE("Timeout converts its duration when the cold task starts", "[Async][Deadline]")
{
    Executor    executor;
    TaskContext context(executor);
    bool        entered = false;
    auto        cold    = WithTimeout(NGIN::Units::Seconds(1), Value(entered));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto before        = NGIN::Time::MonotonicClock::Now();
    auto       operationTask = std::move(cold);

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
    REQUIRE(executor.scheduled.ToNanoseconds() >= before.ToNanoseconds() + 1'000'000'000);
    REQUIRE(executor.timers == 1);
}

TEST_CASE("Ready values and errors preserve completion without deadline admission", "[Async][Deadline]")
{
    TaskContext context(NGIN::Execution::ExecutorRef {});
    auto        value = WithDeadline(TimePoint {}, Task<int, int>::FromCompletion(Completion<int, int>::Success(42)));
    auto        error = WithTimeout(NGIN::Units::Seconds(0), Task<void, int>::FromCompletion(Completion<void, int>::DomainFailure(23)));
    REQUIRE(SyncWait(context, std::move(value)).Value() == 42);
    REQUIRE(SyncWait(context, std::move(error)).Error().DomainError() == 23);
}

TEST_CASE("Expired and invalid timeout durations preserve channel semantics", "[Async][Deadline]")
{
    Executor    executor;
    TaskContext context(executor);
    const bool  invalid       = GENERATE(false, true);
    bool        entered       = false;
    auto        operationTask = WithTimeout(NGIN::Units::Seconds(invalid ? std::numeric_limits<double>::infinity() : -1.0), Waiting(entered));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    if (invalid)
    {
        REQUIRE(result.HasError());
        REQUIRE_FALSE(entered);
    }
    else
    {
        REQUIRE(result.IsStopped());
        REQUIRE(result.StopReason() == StopReason::Deadline);
    }
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("Deadline allocation rollback starts no work and retains no timer", "[Async][Deadline][Allocation]")
{
    Executor    executor;
    Resource    resource;
    TaskContext context(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(executor), .resource = &resource});
    std::size_t allocations = 0;
    bool        entered     = false;
    auto        leaf        = [&]() -> Task<int> { entered = true; allocations = resource.calls; co_return 42; };
    auto        run         = [&](std::size_t offset) -> Task<int> {
        resource.failAt   = offset == (std::numeric_limits<std::size_t>::max)() ? offset : resource.calls + offset;
        const auto before = resource.calls;
        auto       result = co_await WithDeadline(Future(), leaf()).AsCompletion();
        if (entered)
            allocations -= before;
        co_return result;
    };
    {
        auto operationTask = run((std::numeric_limits<std::size_t>::max)());

        TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        executor.scheduler.RunUntilIdle();
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().Succeeded());
    }
    REQUIRE(resource.live == 0);
    const auto total = allocations;
    REQUIRE(total > 0);
    for (std::size_t offset = 0; offset != total; ++offset)
    {
        entered         = false;
        resource.failAt = (std::numeric_limits<std::size_t>::max)();
        {
            auto operationTask = run(offset);

            TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

            auto operation = operationRoot.Spawn(std::move(operationTask)).value();
            executor.scheduler.RunUntilIdle();
            REQUIRE(operationRoot.TryJoin());
            auto result = operation.TakeResult();
            REQUIRE(result.HasError());
            REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
            REQUIRE_FALSE(entered);
        }
        REQUIRE(resource.live == 0);
        REQUIRE(executor.scheduler.PendingTimers() == 0);
    }
}

TEST_CASE("Parent cancellation retains its reason and removes the deadline timer", "[Async][Deadline]")
{
    Executor           executor;
    CancellationSource source;
    TaskContext        context(executor, source.GetToken());
    bool               entered       = false;
    auto               operationTask = WithDeadline(Future(), Waiting(entered));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    REQUIRE(entered);
    source.Cancel(StopReason::Requested);
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.IsStopped());
    REQUIRE(result.StopReason() == StopReason::Requested);
    REQUIRE(executor.scheduler.PendingTimers() == 0);
}

TEST_CASE("Discarded deadline delivery requests shutdown stop and still joins work", "[Async][Deadline][Lifetime]")
{
    ClaimedExecutor           executor;
    TaskContext               context(executor);
    NGIN::Execution::WorkItem release;
    auto                      inner = [&]() -> Task<void> {
        const auto environment = co_await CurrentEnvironment();
        co_await Gate {release};
        if (environment.cancellation.IsCancellationRequested())
            co_await Stopped(environment.cancellation.GetStopReason());
    };
    auto operationTask = WithDeadline(Future(), inner());

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.scheduler.RunUntilIdle();
    executor.timer = {};
    REQUIRE_FALSE(operation.IsCompleted());
    release.Invoke();
    release = {};
    executor.scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.IsStopped());
    REQUIRE(result.StopReason() == StopReason::RuntimeShutdown);
}
