#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Utilities/Callable.hpp>

TEST_CASE("CooperativeScheduler executes ready work")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    int                                   count = 0;

    scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { ++count; })));

    REQUIRE(scheduler.RunOne());
    REQUIRE(count == 1);
    REQUIRE_FALSE(scheduler.RunOne());
}

TEST_CASE("CooperativeScheduler executes timers up to a given timepoint")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    std::vector<int>                      order;
    order.reserve(3);

    scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(2); })),
            NGIN::Time::TimePoint::FromNanoseconds(20));
    scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(1); })),
            NGIN::Time::TimePoint::FromNanoseconds(10));
    scheduler.ExecuteAt(
            NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&]() noexcept { order.push_back(3); })),
            NGIN::Time::TimePoint::FromNanoseconds(30));

    scheduler.RunUntilIdleAt(NGIN::Time::TimePoint::FromNanoseconds(25));

    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
    REQUIRE(scheduler.PendingTimers() == 1);
}

