#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Race.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>

#include <atomic>
#include <memory_resource>
#include <stdexcept>

namespace
{
    using namespace NGIN;

    class FailingResource final : public std::pmr::memory_resource
    {
    public:
        std::size_t calls {};
        std::size_t failAt {static_cast<std::size_t>(-1)};
        std::size_t outstanding {};

    private:
        void* do_allocate(std::size_t size, std::size_t alignment) override
        {
            if (calls++ == failAt)
                throw std::bad_alloc();
            void* allocation = std::pmr::new_delete_resource()->allocate(size, alignment);
            ++outstanding;
            return allocation;
        }
        void do_deallocate(void* allocation, std::size_t size, std::size_t alignment) override
        {
            --outstanding;
            std::pmr::new_delete_resource()->deallocate(allocation, size, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };

    class Executor final
    {
    public:
        Executor(std::size_t capacity, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : completions(capacity, nullptr, nullptr, resource) {}
        auto                      ReserveCompletion(Execution::WorkItem work) noexcept { return completions.Reserve(std::move(work)); }
        Execution::ScheduleResult Execute(Execution::WorkItem work) noexcept
        {
            if (stopped)
                return std::unexpected(Execution::ScheduleError::Stopped);
            return ordinary.Execute(std::move(work));
        }
        auto ExecuteAt(Execution::WorkItem work, Time::TimePoint) noexcept { return Execute(std::move(work)); }
        bool IsCurrent() const noexcept { return current; }
        bool RunOne()
        {
            current        = true;
            const bool ran = completions.RunOne() || ordinary.RunOne();
            current        = false;
            return ran;
        }
        void RunUntilIdle()
        {
            while (RunOne()) {}
        }
        void Stop()
        {
            stopped = true;
            completions.Close();
        }
        std::size_t Outstanding() const { return completions.Outstanding(); }

    private:
        Execution::CooperativeScheduler    ordinary;
        Execution::detail::CompletionQueue completions;
        bool                               current {};
        bool                               stopped {};
    };

    Async::Task<int> Immediate(Async::TaskContext&, int& entered)
    {
        ++entered;
        co_return 7;
    }
}// namespace

TEST_CASE("Race reserves every notification before invoking a factory", "[Async][Race][Admission]")
{
    FailingResource memory;
    std::size_t     capacity = 8;
    SECTION("capacity exhaustion")
    {
        capacity = 2;
    }
    SECTION("allocation failure")
    {
        memory.failAt = 2;
    }
    Executor                        executor(capacity, &memory);
    Async::TaskContext              ctx(executor);
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);
    int                             factories = 0;
    int                             entered   = 0;
    auto                            child     = [&](Async::TaskContext& context) {
        ++factories;
        return Immediate(context, entered);
    };
    auto operationTask = Async::Race(ctx, child, child);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    REQUIRE(result.Error().Fault().native == static_cast<int>(Execution::ScheduleError::ResourceExhausted));
    REQUIRE(factories == 0);
    REQUIRE(entered == 0);
    REQUIRE(executor.Outstanding() == 0);
    REQUIRE(memory.outstanding == 0);
    auto recoveredTask = Immediate(ctx, entered);

    NGIN::Async::TaskSupervisor<typename decltype(recoveredTask)::ErrorType> recoveredRoot(rootContext.GetEnvironment(), 1);

    auto recovered = recoveredRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(recoveredTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(recovered.IsCompleted());
    REQUIRE(recoveredRoot.TryJoin());
    REQUIRE(recovered.TakeResult().Value() == 7);
    REQUIRE(entered == 1);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("Race rejects a partial execution admission before any factory runs", "[Async][Race][Admission]")
{
    Executor                        executor(3);// Parent plus one prepared child; no room for the second child.
    Async::TaskContext              ctx(executor);
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);
    int                             entered       = 0;
    auto                            child         = [&](Async::TaskContext& context) { return Immediate(context, entered); };
    auto                            operationTask = Async::Race(ctx, child, child);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().native == static_cast<int>(Execution::ScheduleError::ResourceExhausted));
    REQUIRE(entered == 0);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("Race joins existing children when another factory fails", "[Async][Race][Lifetime]")
{
    Executor                        executor(16);
    Async::TaskContext              ctx(executor);
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);
    bool                            empty = false;
    SECTION("throwing factory") {}
    SECTION("empty task")
    {
        empty = true;
    }
    int destroyed = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    auto loser = [&](Async::TaskContext& child) -> Async::Task<int> {
        Guard guard {destroyed};
        co_await child.YieldNow();
        co_return 9;
    };
    auto failed = [&](Async::TaskContext&) -> Async::Task<int> {
        if (empty)
            return {};
        throw std::runtime_error("factory failed");
    };
    auto operationTask = Async::Race(ctx, loser, failed);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().Primary().Fault().code == (empty ? Async::AsyncFaultCode::InvalidTaskUsage : Async::AsyncFaultCode::UnhandledException));
    REQUIRE(destroyed == 1);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("Race releases coroutine factory captures before parent completion", "[Async][Race][Lifetime]")
{
    Executor                        executor(16);
    Async::TaskContext              ctx(executor);
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);
    auto                            owner         = std::make_shared<int>(42);
    std::weak_ptr<int>              lifetime      = owner;
    int                             observed      = 0;
    auto                            operationTask = Async::Race(ctx,
                                                                [owner = std::move(owner), &observed](Async::TaskContext& child) -> Async::Task<int> {
                                         co_await child.YieldNow();
                                         observed = *owner;
                                         co_return observed;
                                     });

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value().index == 0);
    REQUIRE(observed == 42);
    REQUIRE(lifetime.expired());
}

TEST_CASE("Race reports mixed value and void child domain failures", "[Async][Race]")
{
    Executor                        executor(16);
    Async::TaskContext              ctx(executor);
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);
    auto                            first = [](Async::TaskContext&) -> Async::Task<void, int> {
        co_await Async::DomainFailure(47);
        co_return;
    };
    auto second = [](Async::TaskContext& child) -> Async::Task<int, int> {
        co_await child.YieldNow();
        co_return 8;
    };
    // Both children are admitted before either starts.
    auto operationTask = Async::Race(ctx, second, first);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().Primary().DomainError() == 47);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("Race joins simultaneous completions and releases factory captures", "[Async][Race][Race]")
{
    Execution::ThreadPoolScheduler executor(4);
    Async::TaskContext             ctx(executor);
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        std::atomic<int> destroyed {0};
        struct Guard
        {
            std::atomic<int>& count;
            ~Guard() { ++count; }
        };
        auto               owner    = std::make_shared<int>(42);
        std::weak_ptr<int> lifetime = owner;
        auto               factory  = [owner = std::move(owner), &destroyed](Async::TaskContext& child) -> Async::Task<int> {
            Guard guard {destroyed};
            co_await child.YieldNow();
            co_return *owner;
        };
        auto second = factory;
        auto third  = factory;
        auto result = Async::SyncWait(ctx, Async::Race(ctx, std::move(factory), std::move(second), std::move(third)));
        REQUIRE(result.Succeeded());
        REQUIRE(result.Value().index < 3);
        REQUIRE(destroyed == 3);
        REQUIRE(lifetime.expired());
    }
}

TEST_CASE("Race root admission rejects an executor without completion reservations", "[Async][Race][Admission]")
{
    struct UntrackedExecutor
    {
        Execution::CooperativeScheduler queue;
        auto                            Execute(Execution::WorkItem work) noexcept { return queue.Execute(std::move(work)); }
        auto                            ExecuteAt(Execution::WorkItem work, Time::TimePoint when) noexcept { return queue.ExecuteAt(std::move(work), when); }
    } executor;
    Async::TaskContext              context(executor);
    int                             entered = 0;
    int                             invoked = 0;
    auto                            task    = Async::Race(context,
                                                          [&](Async::TaskContext& child) { ++invoked; return Immediate(child, entered); });
    Execution::CooperativeScheduler rootExecutor;
    Async::TaskContext              rootContext(rootExecutor);

    Async::TaskSupervisor<typename decltype(task)::ErrorType> root(rootContext.GetEnvironment(), 1);

    auto admitted = root.SpawnOn(Execution::ExecutorRef::From(executor), std::move(task));
    REQUIRE_FALSE(admitted);
    REQUIRE(admitted.error() == Execution::ScheduleError::Rejected);
    executor.queue.RunUntilIdle();
    REQUIRE(root.TryJoin());
    REQUIRE(invoked == 0);
    REQUIRE(entered == 0);
}

TEST_CASE("Race final join uses eligible inline delivery after frame retirement", "[Async][Race][Lifetime]")
{
    Execution::CooperativeScheduler executor;
    Async::TaskContext              ctx(executor);
    Execution::WorkItem             release;
    bool                            limitWork = false;
    SECTION("eligible join") {}
    SECTION("join defers after work exhaustion")
    {
        limitWork = true;
    }
    struct HoldFrame
    {
        Execution::WorkItem& release;
        bool                 await_ready() const noexcept { return false; }
        bool                 await_suspend(std::coroutine_handle<Async::Task<int>::promise_type> handle)
        {
            handle.promise().RetainFrameReference();
            release = Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };
    auto factory = [&](Async::TaskContext&) -> Async::Task<int> {
        co_await HoldFrame {release};
        co_return 42;
    };
    bool raceResumed = false;
    auto observeRace = [&](Async::TaskContext& context) -> Async::Task<std::size_t> {
        auto result = co_await Async::Race(context, factory).AsCompletion();
        raceResumed = true;
        co_return result.Value().index;
    };
    auto operationTask = observeRace(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    const bool completedWithLease = operation.IsCompleted();
    release.Invoke();
    release                             = {};
    const bool completedOutsideExecutor = operation.IsCompleted();
    bool       completedAtRetirement;
    {
        Execution::detail::WorkBudget budget(limitWork ? 1 : 64);
        REQUIRE(executor.RunOne());
        completedAtRetirement = raceResumed;
    }
    executor.RunUntilIdle();
    REQUIRE_FALSE(completedWithLease);
    REQUIRE_FALSE(completedOutsideExecutor);
    REQUIRE(completedAtRetirement == !limitWork);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 0);
}

TEST_CASE("Race competitors inherit parent cancellation through their owned source", "[Async][Race][Cancellation]")
{
    Execution::CooperativeScheduler executor;
    Async::CancellationSource       parent;
    Async::TaskContext              context(executor, parent.GetToken());
    int                             entered = 0;
    auto                            factory = [&](Async::TaskContext& child) -> Async::Task<void> {
        ++entered;
        co_await child.Delay(Units::Seconds(60));
    };
    auto operationTask = Async::Race(context, factory, factory);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    REQUIRE(entered == 2);
    REQUIRE_FALSE(operation.IsCompleted());
    parent.Cancel();
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().IsStopped());
    REQUIRE(executor.PendingTimers() == 0);
}

TEST_CASE("Race preserves successful heterogeneous winners and loser errors", "[Async][Race][Diagnostics]")
{
    using namespace NGIN::Async;
    using E = std::unique_ptr<int>;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           context(executor);
    auto                                  first = [](TaskContext&) {
        return Task<std::unique_ptr<int>, E>::FromCompletion(Completion<std::unique_ptr<int>, E>::Success(std::make_unique<int>(42)));
    };
    auto failed = [](TaskContext&) {
        return Task<void, E>::FromCompletion(Completion<void, E>::DomainFailure(std::make_unique<int>(23)));
    };
    auto operationTask = Race(context, first, failed);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError());
    const auto& failure = result.Error().DomainError();
    REQUIRE(failure.winner.index == 0);
    REQUIRE(failure.winner.kind == CompletionKind::Value);
    REQUIRE(failure.value);
    REQUIRE(*std::get<0>(failure.value->value) == 42);
    REQUIRE(failure.primaryIndex == 1);
    REQUIRE(*failure.Primary().DomainError() == 23);
    REQUIRE_FALSE(failure.errors[0]);
}

TEST_CASE("Race retains every error and keeps an error winner primary", "[Async][Race][Diagnostics]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           context(executor);
    auto                                  first         = [](TaskContext&) -> Task<int, int> { co_return Completion<int, int>::DomainFailure(11); };
    auto                                  winner        = [](TaskContext&) { return Task<void, int>::FromCompletion(Completion<void, int>::DomainFailure(22)); };
    auto                                  third         = [](TaskContext&) { return Task<int, int>::FromCompletion(Completion<int, int>::DomainFailure(33)); };
    auto                                  operationTask = Race(context, first, winner, third);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto        result  = operation.TakeResult();
    const auto& failure = result.Error().DomainError();
    REQUIRE(failure.winner.index == 1);
    REQUIRE(failure.winner.kind == CompletionKind::Error);
    REQUIRE(failure.primaryIndex == 1);
    REQUIRE(failure.Primary().DomainError() == 22);
    REQUIRE(failure.errors[0]->DomainError() == 11);
    REQUIRE(failure.errors[2]->DomainError() == 33);
}

TEST_CASE("Race retains a stopped winner when a loser fails", "[Async][Race][Diagnostics]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           context(executor);
    auto                                  stopped       = [](TaskContext&) { return Task<void, int>::FromCompletion(Completion<void, int>::Stopped(StopReason::Deadline)); };
    auto                                  failed        = [](TaskContext&) { return Task<int, int>::FromCompletion(Completion<int, int>::DomainFailure(23)); };
    auto                                  operationTask = Race(context, stopped, failed);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto        result  = operation.TakeResult();
    const auto& failure = result.Error().DomainError();
    REQUIRE(failure.winner.index == 0);
    REQUIRE(failure.winner.kind == CompletionKind::Stopped);
    REQUIRE(failure.stopped == StopReason::Deadline);
    REQUIRE(failure.Primary().DomainError() == 23);
}

TEST_CASE("Race supports void and duplicate value alternatives", "[Async][Race]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           context(executor);
    auto                                  ready     = [](TaskContext&) { return Task<void>::FromCompletion(Completion<void, NoError>::Success()); };
    auto                                  value     = [](TaskContext&) { return Task<int>::FromCompletion(Completion<int, NoError>::Success(42)); };
    auto                                  firstTask = Race(context, ready, value);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first      = firstRoot.Spawn(std::move(firstTask)).value();
    auto secondTask = Race(context, value, value);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    executor.RunUntilIdle();
    REQUIRE(firstRoot.TryJoin());
    auto voidResult = first.TakeResult();
    REQUIRE(secondRoot.TryJoin());
    auto valueResult = second.TakeResult();
    REQUIRE(voidResult.Value().index == 0);
    REQUIRE(std::holds_alternative<std::monostate>(voidResult.Value().value));
    REQUIRE(valueResult.Value().index == 0);
    REQUIRE(std::get<0>(valueResult.Value().value) == 42);
}

TEST_CASE("Race terminal selection precedes final frame retirement", "[Async][Race][Lifetime]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           context(executor);
    NGIN::Execution::WorkItem             release;
    struct HoldFrame
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        bool                       await_suspend(std::coroutine_handle<Task<int>::promise_type> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };
    auto first         = [&](TaskContext&) -> Task<int> { co_await HoldFrame {release}; co_return 11; };
    auto second        = [](TaskContext&) -> Task<int> { co_return 22; };
    auto operationTask = Race(context, first, second);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    executor.RunUntilIdle();
    const bool completedBeforeRetirement = operation.IsCompleted();
    release.Invoke();
    release = {};
    executor.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeRetirement);
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.Value().index == 0);
    REQUIRE(std::get<0>(result.Value().value) == 11);
}

TEST_CASE("Race rolls back every environment allocation before invoking factories", "[Async][Race][Allocation]")
{
    using namespace NGIN::Async;
    NGIN::Execution::CooperativeScheduler executor;
    TaskContext                           rootContext(executor);
    FailingResource                       resource;
    TaskContext                           context(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(executor), .resource = &resource});
    std::size_t                           admissionAllocations = 0;
    int                                   invoked              = 0;
    auto                                  factory              = [&](TaskContext&) {
        if (invoked++ == 0)
            admissionAllocations = resource.calls;
        return Task<int>::FromCompletion(Completion<int, NoError>::Success(42));
    };
    {
        auto operationTask = Race(context, factory, factory);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        executor.RunUntilIdle();
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().Succeeded());
    }
    REQUIRE(resource.outstanding == 0);
    REQUIRE(invoked == 2);
    const auto allocations = admissionAllocations;
    REQUIRE(allocations > 0);
    for (std::size_t offset = 0; offset != allocations; ++offset)
    {
        invoked            = 0;
        resource.failAt    = resource.calls + offset;
        auto operationTask = Race(context, factory, factory);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        executor.RunUntilIdle();
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE(result.HasError());
        REQUIRE(result.Error().IsFault());
        REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
        REQUIRE(invoked == 0);
        REQUIRE(resource.outstanding == 0);
    }
}
