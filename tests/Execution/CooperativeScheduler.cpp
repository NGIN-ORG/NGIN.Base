#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <vector>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Utilities/Callable.hpp>

TEST_CASE("CooperativeScheduler executes ready work")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    int                                   count = 0;

    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { ++count; }))));

    REQUIRE(scheduler.RunOne());
    REQUIRE(count == 1);
    REQUIRE_FALSE(scheduler.RunOne());
}

TEST_CASE("CooperativeScheduler executes timers up to a given timepoint")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    std::vector<int>                      order;
    order.reserve(3);

    REQUIRE(scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(2); })),
            NGIN::Time::TimePoint::FromNanoseconds(20)));
    REQUIRE(scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(1); })),
            NGIN::Time::TimePoint::FromNanoseconds(10)));
    REQUIRE(scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(3); })),
            NGIN::Time::TimePoint::FromNanoseconds(30)));

    scheduler.RunUntilIdleAt(NGIN::Time::TimePoint::FromNanoseconds(25));

    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
    REQUIRE(scheduler.PendingTimers() == 1);
}

TEST_CASE("CooperativeScheduler rotates continuously runnable completion timer and ready sources")
{
    using namespace NGIN::Execution;
    CooperativeScheduler  scheduler(1);
    const auto            now = NGIN::Time::TimePoint::FromNanoseconds(10);
    std::vector<int>      order;
    bool                  active = true;
    CompletionReservation completion;
    auto                  reserved = scheduler.ReserveCompletion(WorkItem([&] {
        order.push_back(0);
        if (active)
            completion.Schedule();
    }));
    REQUIRE(reserved);
    completion = std::move(*reserved);
    NGIN::Utilities::Callable<void()> timer, ready;
    timer = [&] {
        order.push_back(1);
        if (active)
            REQUIRE(scheduler.ExecuteAt(WorkItem(timer), now));
    };
    ready = [&] {
        order.push_back(2);
        if (active)
            REQUIRE(scheduler.Execute(WorkItem(ready)));
    };
    completion.Schedule();
    REQUIRE(scheduler.ExecuteAt(WorkItem(timer), now));
    REQUIRE(scheduler.Execute(WorkItem(ready)));
    for (int turn = 0; turn != 30; ++turn)
        REQUIRE(scheduler.RunOneAt(now));
    for (int turn = 0; turn != 30; ++turn)
        REQUIRE(order[turn] == turn % 3);
    active = false;
    scheduler.RunUntilIdleAt(now);
    completion.Reset();
}

TEST_CASE("CooperativeScheduler preserves FIFO order across consumed prefix reuse")
{
    using namespace NGIN::Execution;
    CooperativeScheduler scheduler;
    std::vector<int>     order;
    auto                 enqueue = [&](int value) { REQUIRE(scheduler.Execute(WorkItem([&, value] { order.push_back(value); }))); };
    for (int value = 0; value != 600; ++value)
        enqueue(value);
    for (int turn = 0; turn != 400; ++turn)
        REQUIRE(scheduler.RunOne());
    REQUIRE(scheduler.PendingReady() == 200);
    for (int value = 600; value != 1000; ++value)
        enqueue(value);
    scheduler.RunUntilIdle();
    REQUIRE(scheduler.PendingReady() == 0);
    REQUIRE(order.size() == 1000);
    for (int value = 0; value != 1000; ++value)
        REQUIRE(order[value] == value);
}

TEST_CASE("CooperativeScheduler bounds recursive pumps and leaves deferred work runnable")
{
    using namespace NGIN::Execution;
    CooperativeScheduler              scheduler;
    int                               depth = 0, maximumDepth = 0, invoked = 0;
    NGIN::Utilities::Callable<void()> work;
    work = [&] {
        ++depth;
        maximumDepth = std::max(maximumDepth, depth);
        ++invoked;
        if (invoked < 100)
        {
            REQUIRE(scheduler.Execute(WorkItem(work)));
            (void) scheduler.RunOne();
        }
        --depth;
    };
    REQUIRE(scheduler.Execute(WorkItem(work)));
    scheduler.RunUntilIdle();
    REQUIRE(invoked == 100);
    REQUIRE(maximumDepth <= 16);
    REQUIRE(scheduler.PendingReady() == 0);
}

TEST_CASE("CooperativeScheduler nested dispatch shares the enclosing work allowance")
{
    using namespace NGIN::Execution;
    CooperativeScheduler first, second;
    int                  invoked = 0;
    for (int item = 0; item != 10; ++item)
        REQUIRE(second.Execute(WorkItem([&] { ++invoked; })));
    REQUIRE(first.Execute(WorkItem([&] { second.RunUntilIdle(); })));
    {
        detail::WorkBudget budget(3);
        REQUIRE(first.RunOne());
        REQUIRE(invoked == 2);
        REQUIRE_FALSE(second.RunOne());
    }
    second.RunUntilIdle();
    REQUIRE(invoked == 10);
}

TEST_CASE("Cooperative timer cancellation releases captures outside the timer lock", "[Execution][Timer]")
{
    using namespace NGIN::Execution;
    CooperativeScheduler scheduler;
    struct Probe final
    {
        CooperativeScheduler& scheduler;
        bool&                 destroyed;
        ~Probe() { destroyed = scheduler.PendingTimers() == 0; }
    };
    bool destroyed = false;
    auto probe     = std::make_shared<Probe>(scheduler, destroyed);
    auto timer     = scheduler.ScheduleTimer(WorkItem([probe] {}), NGIN::Time::TimePoint::FromNanoseconds(100));
    REQUIRE(timer);
    probe.reset();
    bool        canceled = false;
    std::thread cancel([&] { canceled = timer->Cancel(); });
    cancel.join();
    REQUIRE(canceled);
    REQUIRE(destroyed);
    REQUIRE_FALSE(scheduler.RunOneAt(NGIN::Time::TimePoint::FromNanoseconds(100)));
}

TEST_CASE("Cooperative timer registration preserves other deadlines when removed", "[Execution][Timer]")
{
    using namespace NGIN::Execution;
    CooperativeScheduler scheduler;
    std::vector<int>     order;
    auto                 canceled = scheduler.ScheduleTimer(WorkItem([&] { order.push_back(1); }), NGIN::Time::TimePoint::FromNanoseconds(10));
    auto                 retained = scheduler.ScheduleTimer(WorkItem([&] { order.push_back(2); }), NGIN::Time::TimePoint::FromNanoseconds(20));
    REQUIRE(canceled);
    REQUIRE(retained);
    retained->Detach();
    REQUIRE(canceled->Cancel());
    scheduler.RunUntilIdleAt(NGIN::Time::TimePoint::FromNanoseconds(30));
    REQUIRE(order == std::vector<int> {2});
}
