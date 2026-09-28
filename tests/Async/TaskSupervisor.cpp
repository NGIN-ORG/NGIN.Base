#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <latch>

namespace
{
    using namespace NGIN::Async;
    template<typename E>
    Task<ScopeReport<E>> Join(TaskSupervisor<E>& owner)
    {
        co_return co_await owner.Join();
    }
    template<typename E>
    Task<ScopeReport<E>> Shutdown(TaskSupervisor<E>& owner)
    {
        co_return co_await owner.Shutdown();
    }
    struct Resource final : std::pmr::memory_resource
    {
        std::size_t live {};
        void*       do_allocate(std::size_t bytes, std::size_t alignment) override
        {
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

TEST_CASE("Supervisor recycles retired slots without retaining successful task storage", "[Async][Supervisor][Allocation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    Resource                              resource;
    {
        TaskSupervisor owner(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(scheduler), .resource = &resource}, 1);
        const auto     baseline  = resource.live;
        int            completed = 0;
        for (int iteration = 0; iteration != 128; ++iteration)
        {
            auto admitted = owner.Transfer([&](TaskContext&) -> Task<void> { ++completed; co_return; });
            REQUIRE(admitted);
            scheduler.RunUntilIdle();
            REQUIRE(owner.Pending() == 0);
            REQUIRE(resource.live == baseline);
        }
        REQUIRE(completed == 128);
        auto joinedTask = Join(owner);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE_FALSE(joined.TakeResult().Value().HasError());
    }
    REQUIRE(resource.live == 0);
}

TEST_CASE("Supervisor result handles survive slot reuse and owner destruction", "[Async][Supervisor][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    Operation<std::unique_ptr<int>>       result;
    {
        TaskSupervisor owner(context.GetEnvironment(), 1);
        result = std::move(owner.Spawn([](TaskContext&) -> Task<std::unique_ptr<int>> { co_return std::make_unique<int>(42); }).value());
        scheduler.RunUntilIdle();
        REQUIRE(owner.Transfer([](TaskContext&) -> Task<void> { co_return; }));
        auto joinedTask = Join(owner);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE_FALSE(joined.TakeResult().Value().HasError());
    }
    REQUIRE(*result.TakeResult().Value() == 42);
}

TEST_CASE("Supervisor accepts move-only error reports and continues admitting work", "[Async][Supervisor][Diagnostics]")
{
    using E = std::unique_ptr<int>;
    struct Sink
    {
        std::array<std::optional<AsyncError<E>>, 16> errors;
        std::size_t                                  count {};
        static bool                                  Report(void* context, AsyncError<E> error) noexcept
        {
            auto& sink = *static_cast<Sink*>(context);
            if (sink.count == sink.errors.size())
                return false;
            sink.errors[sink.count++].emplace(std::move(error));
            return true;
        }
    } sink;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    {
        TaskSupervisor<E> owner(context.GetEnvironment(), 1, {&Sink::Report, &sink});
        for (int iteration = 0; iteration != 16; ++iteration)
        {
            REQUIRE(owner.Transfer([iteration](TaskContext&) -> Task<void, E> {
                co_await DomainFailure(std::make_unique<int>(iteration));
            }));
            scheduler.RunUntilIdle();
            REQUIRE_FALSE(owner.GetCancellationToken().IsCancellationRequested());
            REQUIRE(owner.Pending() == 0);
        }
        auto joinedTask = Shutdown(owner);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE_FALSE(joined.TakeResult().Value().HasError());
    }
    REQUIRE(sink.count == 16);
    for (std::size_t index = 0; index != sink.count; ++index)
        REQUIRE(*sink.errors[index]->DomainError() == static_cast<int>(index));
}

TEST_CASE("A missing supervisor sink retains errors and stops admitted siblings", "[Async][Supervisor][Diagnostics]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    std::optional<ScopeReport<int>>       report;
    {
        TaskSupervisor<int> owner(context.GetEnvironment(), 2);
        REQUIRE(owner.Transfer([](TaskContext& child) -> Task<void, int> { co_await child.Delay(NGIN::Units::Seconds(60)); }));
        REQUIRE(owner.Transfer([](TaskContext&) -> Task<void, int> { co_await DomainFailure(23); }));
        scheduler.RunUntilIdle();
        REQUIRE(owner.GetCancellationToken().GetStopReason() == StopReason::OwnerClosed);
        REQUIRE(owner.Pending() == 0);
        REQUIRE(scheduler.PendingTimers() == 0);
        bool invoked  = false;
        auto rejected = owner.Transfer([&](TaskContext&) -> Task<void, int> { invoked = true; co_return; });
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error() == NGIN::Execution::ScheduleError::Stopped);
        REQUIRE_FALSE(invoked);
        auto joinedTask = Join(owner);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        report.emplace(std::move(joined.TakeResult()).Value());
    }
    REQUIRE(report->Errors().size() == 1);
    REQUIRE(report->Error().DomainError() == 23);
}

TEST_CASE("Supervisor reporting waits for frame retirement and rejection retains the error", "[Async][Supervisor][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    struct Sink
    {
        TaskSupervisor<int>* owner {};
        bool                 retired {}, invoked {}, afterRetirement {};
        static bool          Report(void* context, AsyncError<int>) noexcept
        {
            auto& sink           = *static_cast<Sink*>(context);
            sink.invoked         = true;
            sink.afterRetirement = sink.retired && sink.owner->Pending() == 1;
            return false;
        }
    } sink;
    struct Guard
    {
        Sink& sink;
        ~Guard() { sink.retired = true; }
    };
    struct HoldFrame
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        bool                       await_suspend(std::coroutine_handle<Task<void, int>::promise_type> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };
    NGIN::Execution::WorkItem release;
    auto                      failed = [](std::unique_ptr<Guard>, NGIN::Execution::WorkItem& release) -> Task<void, int> {
        co_await HoldFrame {release};
        co_await DomainFailure(47);
    };
    TaskSupervisor<int> owner(context.GetEnvironment(), 1, {&Sink::Report, &sink});
    sink.owner = &owner;
    REQUIRE(owner.Transfer(failed(std::make_unique<Guard>(sink), release)));
    scheduler.RunUntilIdle();
    const bool earlyReport = sink.invoked;
    release.Invoke();
    release         = {};
    auto joinedTask = Join(owner);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(earlyReport);
    REQUIRE(sink.afterRetirement);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE(joined.TakeResult().Value().Error().DomainError() == 47);
    REQUIRE(owner.GetCancellationToken().GetStopReason() == StopReason::OwnerClosed);
}

TEST_CASE("Supervisor shutdown cancels and joins transferred work", "[Async][Supervisor][Shutdown]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    TaskSupervisor                        owner(context.GetEnvironment(), 1);
    bool                                  entered = false;
    REQUIRE(owner.Transfer([&](TaskContext& child) -> Task<void> {
        entered = true;
        co_await child.Delay(NGIN::Units::Seconds(60));
    }));
    scheduler.RunUntilIdle();
    REQUIRE(entered);
    auto joinedTask = Shutdown(owner);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(joinedRoot.TryJoin());
    auto report = std::move(joined.TakeResult()).Value();
    REQUIRE(report.IsStopped());
    REQUIRE(report.StopReason() == StopReason::RuntimeShutdown);
    REQUIRE(owner.Pending() == 0);
    REQUIRE(scheduler.PendingTimers() == 0);
}

TEST_CASE("Supervisor handles concurrent reported failures without stopping siblings", "[Async][Supervisor][Concurrency]")
{
    NGIN::Execution::ThreadPoolScheduler executor(4);
    TaskContext                          context(executor);
    std::atomic<int>                     reports {0};
    TaskSupervisor<int>                  owner(context.GetEnvironment(), 64, {+[](void* context, AsyncError<int> error) noexcept {
                                                                 if (error.IsDomainError())
                                                                     ++*static_cast<std::atomic<int>*>(context);
                                                                 return true;
                                                             },
                                                                              &reports});
    for (int index = 0; index != 64; ++index)
        REQUIRE(owner.Transfer([index](TaskContext&) -> Task<void, int> { co_await DomainFailure(index); }));
    auto joined = SyncWait(context, Join(owner));
    REQUIRE_FALSE(joined.Value().HasError());
    REQUIRE(reports.load() == 64);
    REQUIRE_FALSE(owner.GetCancellationToken().IsCancellationRequested());
}

TEST_CASE("Supervisor join waits for recycled record deallocation on another thread", "[Async][Supervisor][Lifetime]")
{
    struct BlockingResource final : std::pmr::memory_resource
    {
        Operation<void>*  operation {};
        std::latch        entered {1}, released {1};
        std::atomic<bool> blocked {false};
        void*             do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
        {
            if (operation && operation->IsCompleted() && !blocked.exchange(true))
            {
                entered.count_down();
                released.wait();
            }
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    } resource;
    NGIN::Execution::CooperativeScheduler scheduler, observer;
    TaskContext                           context(observer);
    TaskSupervisor                        owner(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(scheduler), .resource = &resource}, 1);
    auto                                  operation = owner.Spawn([](TaskContext&) -> Task<void> { co_return; }).value();
    resource.operation                              = &operation;
    std::thread worker([&] { scheduler.RunUntilIdle(); });
    resource.entered.wait();
    auto joinedTask = Join(owner);

    TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

    auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
    observer.RunUntilIdle();
    const bool early = joined.IsCompleted();
    resource.released.count_down();
    worker.join();
    observer.RunUntilIdle();
    REQUIRE_FALSE(early);
    REQUIRE(joinedRoot.TryJoin());
    REQUIRE_FALSE(joined.TakeResult().Value().HasError());
    resource.operation = nullptr;
}

TEST_CASE("Supervisor polling join closes admission without installing a pending waiter", "[Async][Supervisor][Shutdown]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    TaskSupervisor                        owner(context.GetEnvironment(), 1);
    REQUIRE(owner.Transfer([](TaskContext& child) -> Task<void> { co_await child.YieldNow(); }));
    REQUIRE_FALSE(owner.TryJoin());
    REQUIRE_FALSE(owner.TryJoin());
    REQUIRE_FALSE(owner.Transfer([](TaskContext&) -> Task<void> { co_return; }));
    scheduler.RunUntilIdle();
    auto report = owner.TryJoin();
    REQUIRE(report);
    REQUIRE_FALSE(report->HasError());
    REQUIRE(owner.TryJoin());
}
