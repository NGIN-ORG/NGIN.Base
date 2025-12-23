/// @file WorkItem.cpp
/// @brief Tests for NGIN::Execution::WorkItem scheduling.

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Units.hpp>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("WorkItem executes an inline lambda job", "[Execution][WorkItem]")
{
    int value = 0;
    auto item = NGIN::Execution::WorkItem([&]() noexcept { value = 42; });
    item.Invoke();
    REQUIRE(value == 42);
}

TEST_CASE("ThreadPoolScheduler executes a WorkItem job", "[Execution][ThreadPoolScheduler][WorkItem]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    std::atomic<int> completed {0};

    scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&] {
        completed.store(1, std::memory_order_release);
    })));

    for (int i = 0; i < 200 && completed.load(std::memory_order_acquire) == 0; ++i)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(completed.load(std::memory_order_acquire) == 1);
}

TEST_CASE("ExecutorRef schedules a job on a scheduler", "[Execution][ExecutorRef][WorkItem]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    const auto executor = NGIN::Execution::ExecutorRef::From(scheduler);

    std::atomic<int> completed {0};
    executor.Execute(NGIN::Utilities::Callable<void()>([&] {
        completed.store(1, std::memory_order_release);
    }));

    for (int i = 0; i < 200 && completed.load(std::memory_order_acquire) == 0; ++i)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(completed.load(std::memory_order_acquire) == 1);
}

TEST_CASE("ExecutorRef ExecuteAfter(0) schedules a job immediately", "[Execution][ExecutorRef]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    const auto executor = NGIN::Execution::ExecutorRef::From(scheduler);

    std::atomic<int> completed {0};
    executor.ExecuteAfter([&]() noexcept { completed.fetch_add(1, std::memory_order_relaxed); }, NGIN::Units::Nanoseconds(0.0));

    REQUIRE(scheduler.RunOne());
    REQUIRE(completed.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("WorkItem rejects an empty job", "[Execution][WorkItem]")
{
    NGIN::Utilities::Callable<void()> empty;
    REQUIRE_THROWS_AS(NGIN::Execution::WorkItem(std::move(empty)), std::invalid_argument);
}
