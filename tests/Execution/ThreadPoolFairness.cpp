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
