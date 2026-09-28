#include <NGIN/Async/SyncWait.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <future>

namespace
{
    using namespace NGIN::Async;
    struct Resource final : std::pmr::memory_resource
    {
        std::atomic<unsigned> live {};
        bool                  fail {};
        void*                 do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (fail)
                throw std::bad_alloc();
            auto pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
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
    Task<int> Value(bool& started)
    {
        started = true;
        co_return 42;
    }
    Task<int> Leased(std::shared_ptr<int> owned, std::promise<NGIN::Execution::WorkItem>& lease)
    {
        struct Hold
        {
            std::promise<NGIN::Execution::WorkItem>& lease;
            bool                                     await_ready() const noexcept { return false; }
            bool                                     await_suspend(std::coroutine_handle<Task<int>::promise_type> handle)
            {
                handle.promise().RetainFrameReference();
                lease.set_value(NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); }));
                return false;
            }
            void await_resume() const noexcept {}
        };
        co_await Hold {lease};
        co_return *owned;
    }
}// namespace

TEST_CASE("SyncWait joins a root's final frame lease before returning", "[Async][Root][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler   scheduler;
    TaskContext                             context(scheduler);
    auto                                    owned = std::make_shared<int>(42);
    std::weak_ptr<int>                      weak  = owned;
    std::promise<NGIN::Execution::WorkItem> lease;
    auto                                    releaseFuture = lease.get_future();
    auto                                    result        = std::async(std::launch::async, [&context, &lease, owned = std::move(owned)]() mutable {
        return SyncWait(context, Leased(std::move(owned), lease));
    });
    while (releaseFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        scheduler.RunUntilIdle();
        std::this_thread::yield();
    }
    scheduler.RunUntilIdle();
    const bool returnedEarly = result.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready;
    const bool releasedEarly = weak.expired();
    auto       release       = releaseFuture.get();
    release.Invoke();
    release = {};
    scheduler.RunUntilIdle();
    REQUIRE(result.get().Value() == 42);
    REQUIRE_FALSE(returnedEarly);
    REQUIRE_FALSE(releasedEarly);
    REQUIRE(weak.expired());
}

TEST_CASE("SyncWait releases root ownership allocated from the caller resource", "[Async][Root][Allocation]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    Resource                             resource;
    TaskContext                          context(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(scheduler), .resource = &resource});
    for (int iteration = 0; iteration != 64; ++iteration)
    {
        bool started = false;
        REQUIRE(SyncWait(context, Value(started)).Value() == 42);
        REQUIRE(started);
        REQUIRE(resource.live.load() == 0);
    }
    resource.fail = true;
    bool started  = false;
    auto rejected = SyncWait(context, Value(started));
    REQUIRE(rejected.HasError());
    REQUIRE(rejected.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
    REQUIRE_FALSE(started);
    REQUIRE(resource.live.load() == 0);
}

TEST_CASE("SyncWait retains move-only root errors and rejects canceled cold roots", "[Async][Root]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    TaskContext                          context(scheduler);
    auto                                 failed = []() -> Task<int, std::unique_ptr<int>> {
        co_return Completion<int, std::unique_ptr<int>>::DomainFailure(std::make_unique<int>(23));
    };
    auto result = SyncWait(context, failed());
    REQUIRE(*result.Error().DomainError() == 23);
    CancellationSource cancellation;
    cancellation.Cancel(StopReason::Deadline);
    TaskContext canceled(NGIN::Execution::ExecutorRef::From(scheduler), cancellation.GetToken());
    bool        started = false;
    REQUIRE(SyncWait(canceled, Value(started)).StopReason() == StopReason::Deadline);
    REQUIRE_FALSE(started);
}
