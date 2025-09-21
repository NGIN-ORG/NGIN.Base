/// @file AtomicConditionTest.cpp
/// @brief Tests for NGIN::Async::AtomicCondition.

#include <NGIN/Async/AtomicCondition.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

using namespace NGIN::Async;
using namespace std::chrono_literals;

TEST_CASE("AtomicCondition wakes one waiting thread", "[Async][AtomicCondition]")
{
    AtomicCondition  condition;
    std::atomic<int> counter {0};

    std::thread worker([&] {
        condition.Wait();
        counter.fetch_add(1, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(50ms);
    condition.NotifyOne();
    worker.join();

    CHECK(counter.load() == 1);
}

TEST_CASE("AtomicCondition NotifyOne wakes threads individually", "[Async][AtomicCondition]")
{
    AtomicCondition          condition;
    std::atomic<int>         counter {0};
    std::vector<std::thread> workers;
    workers.reserve(2);

    for (int i = 0; i < 2; ++i)
    {
        workers.emplace_back([&] {
            condition.Wait();
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(50ms);
    condition.NotifyOne();
    std::this_thread::sleep_for(50ms);
    CHECK(counter.load() == 1);

    condition.NotifyOne();
    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(counter.load() == 2);
}

TEST_CASE("AtomicCondition NotifyAll wakes arbitrary thread counts", "[Async][AtomicCondition]")
{
    for (int threadCount: {1, 2, 4, 8})
    {
        DYNAMIC_SECTION("threads=" << threadCount)
        {
            AtomicCondition          condition;
            std::atomic<int>         counter {0};
            std::vector<std::thread> workers;
            workers.reserve(threadCount);

            for (int i = 0; i < threadCount; ++i)
            {
                workers.emplace_back([&] {
                    condition.Wait();
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(50ms);
            condition.NotifyAll();
            for (auto& worker: workers)
            {
                worker.join();
            }

            CHECK(counter.load() == threadCount);
        }
    }
}

TEST_CASE("AtomicCondition allows repeated wait cycles", "[Async][AtomicCondition]")
{
    AtomicCondition  condition;
    std::atomic<int> counter {0};

    std::thread worker([&] {
        condition.Wait();
        counter.fetch_add(1, std::memory_order_relaxed);
        condition.Wait();
        counter.fetch_add(1, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(50ms);
    condition.NotifyAll();
    std::this_thread::sleep_for(50ms);
    condition.NotifyAll();
    worker.join();

    CHECK(counter.load() == 2);
}

#ifdef _DEBUG
TEST_CASE("AtomicCondition debug counters track state", "[Async][AtomicCondition]")
{
    AtomicCondition condition;
    CHECK(condition.GetGeneration() == 0U);
    CHECK(condition.GetWaitingThreadCount() == 0U);
    CHECK_FALSE(condition.HasWaitingThreads());

    std::thread worker([&] {
        condition.Wait();
    });

    std::this_thread::sleep_for(50ms);
    CHECK(condition.GetWaitingThreadCount() == 1U);
    CHECK(condition.HasWaitingThreads());

    condition.NotifyOne();
    worker.join();

    CHECK(condition.GetGeneration() == 1U);
    CHECK(condition.GetWaitingThreadCount() == 0U);
    CHECK_FALSE(condition.HasWaitingThreads());
}

TEST_CASE("AtomicCondition NotifyAll clears debug counters", "[Async][AtomicCondition]")
{
    AtomicCondition          condition;
    constexpr int            threadCount = 3;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        workers.emplace_back([&] {
            condition.Wait();
        });
    }

    std::this_thread::sleep_for(50ms);
    CHECK(condition.GetWaitingThreadCount() == 3U);
    CHECK(condition.HasWaitingThreads());

    condition.NotifyAll();
    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(condition.GetGeneration() == 1U);
    CHECK(condition.GetWaitingThreadCount() == 0U);
    CHECK_FALSE(condition.HasWaitingThreads());
}
#endif
