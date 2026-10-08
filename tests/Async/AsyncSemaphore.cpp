#include <NGIN/Async/AsyncSemaphore.hpp>
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/SyncWait.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <future>
#include <memory_resource>
#include <thread>
#include <vector>

namespace
{
    using namespace NGIN::Async;
    using namespace NGIN::Execution;

    struct AdmissionGate
    {
        CooperativeScheduler scheduler;
        bool                 open {true};
        void (*onReserve)(void*) noexcept {};
        void* callbackContext {};

        ScheduleResult Execute(WorkItem work) noexcept
        {
            if (!open)
                return std::unexpected(ScheduleError::Stopped);
            return scheduler.Execute(std::move(work));
        }
        ScheduleResult ExecuteAt(WorkItem work, NGIN::Time::TimePoint time) noexcept
        {
            if (!open)
                return std::unexpected(ScheduleError::Stopped);
            return scheduler.ExecuteAt(std::move(work), time);
        }
        std::expected<CompletionReservation, ScheduleError> ReserveCompletion(WorkItem work) noexcept
        {
            if (!open)
                return std::unexpected(ScheduleError::Stopped);
            auto result = scheduler.ReserveCompletion(std::move(work));
            if (auto callback = std::exchange(onReserve, nullptr))
                callback(callbackContext);
            return result;
        }
        bool IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    };

    template<typename T, typename E>
    Completion<T, E> Run(CooperativeScheduler& scheduler, TaskContext& context, Task<T, E> task)
    {
        TaskSupervisor<E> root(context.GetEnvironment(), 1);
        auto              operation = root.Spawn(std::move(task)).value();
        scheduler.RunUntilIdle();
        REQUIRE(root.TryJoin());
        return operation.TakeResult();
    }

    Task<void> Record(AsyncSemaphore& semaphore, int value, std::vector<int>& order)
    {
        co_await semaphore.WaitAsync();
        order.push_back(value);
    }

    struct Resource final : std::pmr::memory_resource
    {
        bool     fail {false};
        unsigned live {0};
        void*    do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (fail)
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
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };
}// namespace

TEST_CASE("AsyncSemaphore counts permits and its wait tasks are cold", "[Async][Semaphore]")
{
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    AsyncSemaphore       semaphore(2, 2);
    auto                 cold = semaphore.WaitAsync();
    REQUIRE(semaphore.CurrentCount() == 2);
    REQUIRE(Run(scheduler, context, std::move(cold)).Succeeded());
    REQUIRE(semaphore.CurrentCount() == 1);
    REQUIRE(semaphore.TryAcquire());
    REQUIRE_FALSE(semaphore.TryAcquire());
    semaphore.Release();
    semaphore.Release();
    REQUIRE(semaphore.CurrentCount() == 2);
}

TEST_CASE("AsyncSemaphore hands permits to FIFO waiters without running them inline", "[Async][Semaphore]")
{
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    AsyncSemaphore       semaphore(0, 1);
    TaskSupervisor<>     root(context.GetEnvironment(), 3);
    std::vector<int>     order;
    auto                 first = root.Spawn(Record(semaphore, 1, order)).value();
    scheduler.RunUntilIdle();
    auto second = root.Spawn(Record(semaphore, 2, order)).value();
    scheduler.RunUntilIdle();
    auto third = root.Spawn(Record(semaphore, 3, order)).value();
    scheduler.RunUntilIdle();
    REQUIRE(order.empty());
    for (int expected = 1; expected <= 3; ++expected)
    {
        semaphore.Release();
        REQUIRE(order.size() == static_cast<std::size_t>(expected - 1));
        REQUIRE_FALSE(semaphore.TryAcquire());
        scheduler.RunUntilIdle();
        REQUIRE(order.back() == expected);
    }
    REQUIRE(root.TryJoin());
    REQUIRE(first.TakeResult().Succeeded());
    REQUIRE(second.TakeResult().Succeeded());
    REQUIRE(third.TakeResult().Succeeded());
}

TEST_CASE("AsyncSemaphore cancellation removes head middle and tail waiters", "[Async][Semaphore][Cancellation]")
{
    for (int canceled = 0; canceled != 3; ++canceled)
    {
        CooperativeScheduler scheduler;
        TaskContext          context(scheduler);
        AsyncSemaphore       semaphore(0, 1);
        CancellationSource   cancellation;
        TaskSupervisor<>     root(context.GetEnvironment(), 2);
        auto                 canceledEnvironment = context.GetEnvironment();
        canceledEnvironment.cancellation         = cancellation.GetToken();
        TaskSupervisor<>             canceledRoot(canceledEnvironment, 1);
        std::vector<Operation<void>> operations;
        std::vector<int>             order;
        for (int index = 0; index != 3; ++index)
        {
            operations.push_back((index == canceled ? canceledRoot : root).Spawn(Record(semaphore, index, order)).value());
            scheduler.RunUntilIdle();
        }
        cancellation.Cancel(StopReason::Deadline);
        scheduler.RunUntilIdle();
        REQUIRE(operations[canceled].TakeResult().StopReason() == StopReason::Deadline);
        for (int index = 0; index != 3; ++index)
        {
            if (index == canceled)
                continue;
            semaphore.Release();
            scheduler.RunUntilIdle();
            REQUIRE(order.back() == index);
            REQUIRE(operations[index].TakeResult().Succeeded());
        }
        REQUIRE(root.TryJoin());
        REQUIRE(canceledRoot.TryJoin());
        REQUIRE(semaphore.CurrentCount() == 0);
        semaphore.Release();
        REQUIRE(semaphore.TryAcquire());
    }
}

TEST_CASE("AsyncSemaphore preserves a grant when cancellation arrives before delivery", "[Async][Semaphore][Cancellation]")
{
    CooperativeScheduler scheduler;
    CancellationSource   cancellation;
    TaskContext          context(scheduler, cancellation.GetToken());
    AsyncSemaphore       semaphore(0, 1);
    TaskSupervisor<>     root(context.GetEnvironment(), 1);
    auto                 operation = root.Spawn(semaphore.WaitAsync()).value();
    scheduler.RunUntilIdle();
    semaphore.Release();
    cancellation.Cancel();
    scheduler.RunUntilIdle();
    REQUIRE(root.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(semaphore.CurrentCount() == 0);
    semaphore.Release();
}

TEST_CASE("AsyncSemaphore observes cancellation before acquiring an available permit", "[Async][Semaphore][Cancellation]")
{
    CooperativeScheduler scheduler;
    CancellationSource   cancellation;
    TaskContext          context(scheduler, cancellation.GetToken());
    AsyncSemaphore       semaphore(1, 1);
    auto                 body = [&]() -> Task<void> {
        cancellation.Cancel(StopReason::Deadline);
        co_await semaphore.WaitAsync();
        FAIL("Canceled wait resumed");
    };
    REQUIRE(Run(scheduler, context, body()).StopReason() == StopReason::Deadline);
    REQUIRE(semaphore.CurrentCount() == 1);
}

TEST_CASE("AsyncSemaphore guards transfer ownership and release exactly once", "[Async][Semaphore]")
{
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    AsyncSemaphore       semaphore(2, 2);
    {
        auto firstResult  = Run(scheduler, context, semaphore.AcquireAsync());
        auto secondResult = Run(scheduler, context, semaphore.AcquireAsync());
        REQUIRE(firstResult.Succeeded());
        REQUIRE(secondResult.Succeeded());
        auto first = std::move(firstResult).Value();
        auto moved = std::move(first);
        REQUIRE_FALSE(first.OwnsPermit());
        REQUIRE(moved.OwnsPermit());
        REQUIRE(semaphore.CurrentCount() == 0);
        moved = std::move(secondResult).Value();
        REQUIRE(semaphore.CurrentCount() == 1);
        moved.Release();
        moved.Release();
        REQUIRE(semaphore.CurrentCount() == 2);
    }
    REQUIRE(semaphore.CurrentCount() == 2);
    {
        auto result = Run(scheduler, context, semaphore.AcquireAsync());
    }
    REQUIRE(semaphore.CurrentCount() == 2);
}

TEST_CASE("AsyncSemaphore guard releases on propagated task failure and cancellation", "[Async][Semaphore]")
{
    for (bool cancel: {false, true})
    {
        CooperativeScheduler scheduler;
        TaskContext          context(scheduler);
        AsyncSemaphore       semaphore(1, 1);
        auto                 fail = [cancel]() -> Task<void> {
            if (cancel)
                co_await Stopped();
            else
                co_await Faulted(MakeAsyncFault(AsyncFaultCode::UnknownRuntimeFailure));
        };
        auto body = [&]() -> Task<void> {
            auto permit = co_await semaphore.AcquireAsync();
            co_await fail();
        };
        auto result = Run(scheduler, context, body());
        REQUIRE((cancel ? result.IsStopped() : result.HasError()));
        REQUIRE(semaphore.CurrentCount() == 1);
    }
}

TEST_CASE("AsyncSemaphore allocation and cancellation registration failures consume no permit", "[Async][Semaphore][Allocation]")
{
    for (bool registrationFailure: {false, true})
    {
        AdmissionGate  gate;
        Resource       resource;
        TaskContext    context(AsyncEnvironment {.executor = ExecutorRef::From(gate), .resource = &resource});
        AsyncSemaphore semaphore(0, 1);
        auto           body = [&]() -> Task<void> {
            if (registrationFailure)
            {
                // The task owner creates the inherited cancellation source from
                // this resource. Fail registration after pending-state allocation.
                gate.callbackContext = &resource;
                gate.onReserve       = +[](void* pointer) noexcept { static_cast<Resource*>(pointer)->fail = true; };
            }
            else
                resource.fail = true;
            auto result   = co_await semaphore.WaitAsync<int>().AsCompletion();
            resource.fail = false;
            REQUIRE(result.HasError());
            REQUIRE(result.Error().Fault().code == (registrationFailure ? AsyncFaultCode::CancellationRegistrationFailed : AsyncFaultCode::SchedulerDispatchFailed));
        };
        REQUIRE(Run(gate.scheduler, context, body()).Succeeded());
        REQUIRE(resource.live == 0);
        semaphore.Release();
        REQUIRE(semaphore.TryAcquire());
    }
}

TEST_CASE("AsyncSemaphore uncontended waits need no pending allocation or delivery slot", "[Async][Semaphore][Allocation]")
{
    // The root and its owner occupy both completion slots. Its direct child can still
    // acquire an available permit with allocation disabled after root startup.
    CooperativeScheduler scheduler(2);
    Resource             resource;
    TaskContext          context(AsyncEnvironment {.executor = ExecutorRef::From(scheduler), .resource = &resource});
    AsyncSemaphore       semaphore(1, 1);
    auto                 body = [&]() -> Task<void> {
        resource.fail = true;
        co_await semaphore.WaitAsync();
        resource.fail = false;
        semaphore.Release();
    };
    REQUIRE(Run(scheduler, context, body()).Succeeded());
    REQUIRE(resource.live == 0);
}

TEST_CASE("AsyncSemaphore rejected delivery admission leaves no queued waiter", "[Async][Semaphore]")
{
    CooperativeScheduler scheduler(2);
    TaskContext          context(scheduler);
    AsyncSemaphore       semaphore(0, 1);
    auto                 result = Run(scheduler, context, semaphore.WaitAsync());
    REQUIRE(result.HasError());
    REQUIRE(result.Error().Fault().code == AsyncFaultCode::SchedulerDispatchFailed);
    semaphore.Release();
    REQUIRE(semaphore.TryAcquire());
}

TEST_CASE("AsyncSemaphore cancellation racing release conserves permits", "[Async][Semaphore][Concurrency]")
{
    CooperativeScheduler scheduler;
    for (int iteration = 0; iteration != 200; ++iteration)
    {
        AsyncSemaphore     semaphore(0, 1);
        CancellationSource cancellation;
        TaskContext        context(scheduler, cancellation.GetToken());
        TaskSupervisor<>   root(context.GetEnvironment(), 1);
        auto               operation = root.Spawn(semaphore.WaitAsync()).value();
        scheduler.RunUntilIdle();
        std::barrier start(3);
        std::thread  release([&] { start.arrive_and_wait(); semaphore.Release(); });
        std::thread  cancel([&] { start.arrive_and_wait(); cancellation.Cancel(); });
        start.arrive_and_wait();
        release.join();
        cancel.join();
        scheduler.RunUntilIdle();
        REQUIRE(root.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE((result.Succeeded() || result.IsStopped()));
        REQUIRE(semaphore.CurrentCount() == (result.Succeeded() ? 0 : 1));
    }
}

TEST_CASE("AsyncSemaphore serializes critical sections across coroutine suspension on worker threads", "[Async][Semaphore][Concurrency]")
{
    ThreadPoolScheduler scheduler(4);
    TaskContext         context(scheduler);
    AsyncSemaphore      semaphore(1, 1);
    int                 counter = 0;
    std::atomic<bool>   correct {true};
    const auto          executor = context.GetExecutor();
    auto                body     = [&]() -> Task<void> {
        for (int index = 0; index != 100; ++index)
        {
            auto       permit = co_await semaphore.AcquireAsync();
            const auto before = counter;
            co_await YieldNow();
            if (counter != before || !executor.IsCurrent())
                correct = false;
            counter = before + 1;
        }
    };
    std::vector<std::future<bool>> results;
    for (int index = 0; index != 8; ++index)
        results.push_back(std::async(std::launch::async, [&] { return SyncWait(context, body()).Succeeded(); }));
    for (auto& result: results)
        REQUIRE(result.get());
    REQUIRE(correct.load());
    REQUIRE(counter == 800);
    REQUIRE(semaphore.CurrentCount() == 1);
}

TEST_CASE("AsyncSemaphore handles release and cancellation during wait setup", "[Async][Semaphore][Cancellation]")
{
    for (bool cancel: {false, true})
    {
        AdmissionGate      gate;
        CancellationSource cancellation;
        TaskContext        context(gate, cancellation.GetToken());
        AsyncSemaphore     semaphore(0, 1);
        auto               body = [&]() -> Task<void> {
            if (cancel)
            {
                gate.callbackContext = &cancellation;
                gate.onReserve       = +[](void* pointer) noexcept { static_cast<CancellationSource*>(pointer)->Cancel(); };
            }
            else
            {
                gate.callbackContext = &semaphore;
                gate.onReserve       = +[](void* pointer) noexcept { static_cast<AsyncSemaphore*>(pointer)->Release(); };
            }
            co_await semaphore.WaitAsync();
        };
        auto result = Run(gate.scheduler, context, body());
        REQUIRE((cancel ? result.IsStopped() : result.Succeeded()));
        REQUIRE(semaphore.CurrentCount() == 0);
        semaphore.Release();
        REQUIRE(semaphore.TryAcquire());
    }
}

TEST_CASE("AsyncSemaphore delivers admitted waits after executor admission closes", "[Async][Semaphore][Shutdown]")
{
    for (bool cancel: {false, true})
    {
        AdmissionGate       gate;
        CancellationSource  cancellation;
        TaskContext         context(gate, cancellation.GetToken());
        AsyncSemaphore      semaphore(0, 1);
        TaskSupervisor<int> root(context.GetEnvironment(), 1);
        auto                operation = root.Spawn(semaphore.AcquireAsync<int>()).value();
        gate.scheduler.RunUntilIdle();
        REQUIRE_FALSE(operation.IsCompleted());
        gate.open = false;
        if (cancel)
            cancellation.Cancel();
        else
            semaphore.Release();
        gate.scheduler.RunUntilIdle();
        REQUIRE(root.TryJoin());
        {
            auto result = operation.TakeResult();
            REQUIRE((cancel ? result.IsStopped() : result.Succeeded()));
        }
        REQUIRE(semaphore.CurrentCount() == (cancel ? 0 : 1));
    }
}

TEST_CASE("AsyncSemaphore guards compose with typed domain failures", "[Async][Semaphore]")
{
    CooperativeScheduler scheduler;
    TaskContext          context(scheduler);
    AsyncSemaphore       semaphore(1, 1);
    auto                 body = [&]() -> Task<void, int> {
        auto permit = co_await semaphore.AcquireAsync<int>();
        co_await DomainFailure(17);
    };
    auto result = Run(scheduler, context, body());
    REQUIRE(result.HasError());
    REQUIRE(result.Error().DomainError() == 17);
    REQUIRE(semaphore.CurrentCount() == 1);
}

TEST_CASE("AsyncSemaphore setup delivery and cancellation can overlap on workers", "[Async][Semaphore][Concurrency]")
{
    ThreadPoolScheduler scheduler(4);
    for (int iteration = 0; iteration != 200; ++iteration)
    {
        AsyncSemaphore     semaphore(0, 1);
        CancellationSource cancellation;
        TaskContext        context(scheduler, cancellation.GetToken());
        std::barrier       start(3);
        auto               body = [&]() -> Task<void> {
            start.arrive_and_wait();
            auto permit = co_await semaphore.AcquireAsync();
        };
        auto        result = std::async(std::launch::async, [&] { return SyncWait(context, body()); });
        std::thread cancel([&] { start.arrive_and_wait(); cancellation.Cancel(); });
        start.arrive_and_wait();
        semaphore.Release();
        cancel.join();
        auto completion = result.get();
        REQUIRE((completion.Succeeded() || completion.IsStopped()));
        REQUIRE(semaphore.CurrentCount() == 1);
    }
}
