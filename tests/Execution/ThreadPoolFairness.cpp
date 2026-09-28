#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

#include <future>
#include <latch>
#include <memory>

namespace
{
    using namespace NGIN::Execution;
    struct Workload final : std::enable_shared_from_this<Workload>
    {
        ThreadPoolScheduler&  scheduler;
        CompletionReservation completion;
        std::promise<bool>    result;
        std::latch            started {1}, release {1};
        unsigned              callbacks {}, localCalls {}, completionCalls {};
        bool                  injected {}, oldest {}, finished {};
        explicit Workload(ThreadPoolScheduler& pool) : scheduler(pool) {}

        bool Check()
        {
            if (finished)
                return false;
            ++callbacks;
            const bool progressed = injected && oldest && localCalls != 0 && completionCalls != 0;
            if (progressed || callbacks == 512)
            {
                finished = true;
                completion.Reset();
                result.set_value(progressed);
            }
            return !finished;
        }
        void Local()
        {
            ++localCalls;
            if (Check())
                QueueLocal();
        }
        void QueueLocal()
        {
            if (!scheduler.Execute(WorkItem([self = shared_from_this()] { self->Local(); })))
                std::terminate();
        }
        void Complete()
        {
            ++completionCalls;
            if (Check())
                completion.Schedule();
        }
    };
}// namespace

TEST_CASE("Thread pool makes bounded progress across completion injected and local work", "[Execution][ThreadPoolScheduler][Fairness]")
{
    ThreadPoolScheduler scheduler(1);
    auto                workload = std::make_shared<Workload>(scheduler);
    auto                result   = workload->result.get_future();
    auto                reserved = scheduler.ReserveCompletion(WorkItem([workload] { workload->Complete(); }));
    REQUIRE(reserved);
    workload->completion = std::move(*reserved);
    const auto seeded    = scheduler.Execute(WorkItem([workload] {
        if (!workload->scheduler.Execute(WorkItem([workload] {
                workload->oldest = true;
                (void) workload->Check();
            })))
            std::terminate();
        workload->QueueLocal();
        workload->started.count_down();
        workload->release.wait();
    }));
    if (!seeded)
        workload->completion.Reset();
    REQUIRE(seeded);
    workload->started.wait();
    // No assertions while the worker is parked: always release before teardown.
    const auto injected = scheduler.Execute(WorkItem([workload] {
        workload->injected = true;
        (void) workload->Check();
    }));
    workload->completion.Schedule();
    workload->release.count_down();
    const bool progressed = result.get();
    REQUIRE(injected);
    REQUIRE(progressed);
}

TEST_CASE("Thread pool RunOne can help the sole worker's local queue", "[Execution][ThreadPoolScheduler][Fairness]")
{
    std::atomic<bool>   invoked {false};
    std::latch          started {1}, release {1};
    ThreadPoolScheduler scheduler(1);
    const auto          seeded = scheduler.Execute(WorkItem([&] {
        (void) scheduler.Execute(WorkItem([&] { invoked.store(scheduler.IsCurrent(), std::memory_order_release); }));
        started.count_down();
        release.wait();
    }));
    REQUIRE(seeded);
    started.wait();
    const bool helped = scheduler.RunOne();
    const bool affine = invoked.load(std::memory_order_acquire);
    release.count_down();
    REQUIRE(helped);
    REQUIRE(affine);
}

TEST_CASE("Thread pool recursive helping defers beyond the dispatch depth limit", "[Execution][ThreadPoolScheduler][Budget]")
{
    ThreadPoolScheduler               scheduler(1);
    int                               depth = 0, maximumDepth = 0, invoked = 0;
    bool                              accepted = true;
    std::promise<bool>                done;
    auto                              result = done.get_future();
    NGIN::Utilities::Callable<void()> work;
    work = [&] {
        ++depth;
        maximumDepth = std::max(maximumDepth, depth);
        ++invoked;
        if (invoked < 100)
        {
            accepted &= bool(scheduler.Execute(WorkItem(work)));
            (void) scheduler.RunOne();
        }
        --depth;
        if (depth == 0 && (invoked == 100 || !accepted))
            done.set_value(accepted && maximumDepth <= 16 && invoked == 100);
    };
    REQUIRE(scheduler.Execute(WorkItem(work)));
    REQUIRE(result.get());
}

TEST_CASE("Thread pool helping charges reserved and ordinary work to its caller budget", "[Execution][ThreadPoolScheduler][Budget]")
{
    std::atomic<int>    invoked {0};
    std::latch          started {1}, release {1};
    ThreadPoolScheduler scheduler(1);
    REQUIRE(scheduler.Execute(WorkItem([&] {
        started.count_down();
        release.wait();
    })));
    started.wait();
    // The scope releases the parked worker even when a subsequent assertion fails.
    struct Release final
    {
        std::latch& latch;
        ~Release() { latch.count_down(); }
    } guard {release};
    auto completion = scheduler.ReserveCompletion(WorkItem([&] { ++invoked; }));
    REQUIRE(completion);
    completion->Dispatch();
    for (int item = 0; item != 5; ++item)
        REQUIRE(scheduler.Execute(WorkItem([&] { ++invoked; })));
    {
        detail::WorkBudget budget(2);
        scheduler.RunUntilIdle();
        REQUIRE(invoked.load() == 2);
        REQUIRE_FALSE(scheduler.RunOne());
    }
    scheduler.RunUntilIdle();
    REQUIRE(invoked.load() == 6);
}

TEST_CASE("Thread pool timer registration removes queued work and preserves detached timers", "[Execution][ThreadPoolScheduler][Timer]")
{
    ThreadPoolScheduler scheduler(1);
    auto                state    = std::make_shared<int>(42);
    std::weak_ptr<int>  released = state;
    auto                deadline = NGIN::Time::TimePoint::FromNanoseconds(NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 3600'000'000'000ULL);
    auto                timer    = scheduler.ScheduleTimer(WorkItem([state] {}), deadline);
    REQUIRE(timer);
    state.reset();
    REQUIRE_FALSE(released.expired());
    REQUIRE(timer->Cancel());
    REQUIRE(released.expired());
    REQUIRE_FALSE(timer->Cancel());
    std::promise<void> done;
    auto               finished = done.get_future();
    auto               detached = scheduler.ScheduleTimer(WorkItem([&] { done.set_value(); }), NGIN::Time::MonotonicClock::Now());
    REQUIRE(detached);
    detached->Detach();
    REQUIRE(finished.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
}
