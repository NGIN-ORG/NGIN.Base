/// @file AtomicCondition.cpp
/// @brief Tests for NGIN::Sync::AtomicCondition.

#include <NGIN/Sync/AtomicCondition.hpp>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <latch>
#include <limits>
#include <thread>
#include <vector>

namespace NGIN::Sync
{
    TEST_CASE("AtomicCondition wakes a waiter after the observed generation changes", "[Sync][AtomicCondition]")
    {
        AtomicCondition   condition;
        std::latch        ready {1};
        std::atomic<bool> woke {false};

        const auto  observedGeneration = condition.Load();
        std::thread worker([&] {
            ready.count_down();
            condition.Wait(observedGeneration);
            woke.store(true, std::memory_order_release);
        });

        ready.wait();
        condition.NotifyOne();
        worker.join();

        CHECK(woke.load(std::memory_order_acquire));
        CHECK(condition.Load() != observedGeneration);
    }

    TEST_CASE("AtomicCondition NotifyAll wakes arbitrary waiter counts", "[Sync][AtomicCondition]")
    {
        for (const int threadCount: {1, 2, 4, 8})
        {
            DYNAMIC_SECTION("threads=" << threadCount)
            {
                AtomicCondition  condition;
                std::latch       ready {threadCount};
                std::atomic<int> woke {0};
                const auto       observedGeneration = condition.Load();

                std::vector<std::thread> workers;
                workers.reserve(static_cast<std::size_t>(threadCount));
                for (int index = 0; index < threadCount; ++index)
                {
                    workers.emplace_back([&] {
                        ready.count_down();
                        condition.Wait(observedGeneration);
                        woke.fetch_add(1, std::memory_order_relaxed);
                    });
                }

                ready.wait();
                condition.NotifyAll();
                for (auto& worker: workers)
                {
                    worker.join();
                }

                CHECK(woke.load(std::memory_order_relaxed) == threadCount);
            }
        }
    }

    TEST_CASE("AtomicCondition supports repeated generation-aware waits", "[Sync][AtomicCondition]")
    {
        AtomicCondition  condition;
        std::latch       firstReady {1};
        std::latch       secondReady {1};
        std::atomic<int> wakeCount {0};

        std::thread worker([&] {
            auto observedGeneration = condition.Load();
            firstReady.count_down();
            condition.Wait(observedGeneration);
            wakeCount.fetch_add(1, std::memory_order_relaxed);

            observedGeneration = condition.Load();
            secondReady.count_down();
            condition.Wait(observedGeneration);
            wakeCount.fetch_add(1, std::memory_order_relaxed);
        });

        firstReady.wait();
        condition.NotifyAll();
        secondReady.wait();
        condition.NotifyAll();
        worker.join();

        CHECK(wakeCount.load(std::memory_order_relaxed) == 2);
    }

    TEST_CASE("AtomicCondition notification without a waiter still releases a later stale wait", "[Sync][AtomicCondition]")
    {
        AtomicCondition condition;
        const auto      observedGeneration = condition.Load();

        // Neither notification has a blocked waiter, so neither needs a platform wake.
        condition.NotifyOne();
        condition.NotifyAll();

        condition.Wait(observedGeneration);
        CHECK(condition.Load() == observedGeneration + 2U);
        CHECK(condition.WaitFor(observedGeneration, NGIN::Units::Milliseconds {1.0}));
    }

    TEST_CASE("AtomicCondition ping-pong never loses a skipped wake", "[Sync][AtomicCondition]")
    {
        // Each side checks its predicate after observing the generation and before
        // blocking, so a lost wake would hang this exchange.
        constexpr int    rounds = 20000;
        AtomicCondition  request;
        AtomicCondition  response;
        std::atomic<int> turn {0};
        const auto       await = [&turn](AtomicCondition& condition, int expected) {
            for (;;)
            {
                const auto generation = condition.Load();
                if (turn.load(std::memory_order_acquire) == expected)
                    return;
                condition.Wait(generation);
            }
        };

        std::thread peer([&] {
            for (int round = 0; round != rounds; ++round)
            {
                await(request, 2 * round + 1);
                turn.store(2 * round + 2, std::memory_order_release);
                response.NotifyOne();
            }
        });
        for (int round = 0; round != rounds; ++round)
        {
            turn.store(2 * round + 1, std::memory_order_release);
            request.NotifyOne();
            await(response, 2 * round + 2);
        }
        peer.join();

        CHECK(turn.load(std::memory_order_acquire) == 2 * rounds);
    }

    TEST_CASE("AtomicCondition timed wait reports timeout", "[Sync][AtomicCondition]")
    {
        AtomicCondition condition;

        CHECK_FALSE(condition.WaitFor(NGIN::Units::Milliseconds {10.0}));
    }

    TEST_CASE("AtomicCondition rejects non-positive and NaN wait durations", "[Sync][AtomicCondition]")
    {
        AtomicCondition condition;

        CHECK_FALSE(condition.WaitFor(NGIN::Units::Milliseconds {0.0}));
        CHECK_FALSE(condition.WaitFor(NGIN::Units::Milliseconds {-1.0}));
        CHECK_FALSE(condition.WaitFor(
                NGIN::Units::Milliseconds {(std::numeric_limits<double>::quiet_NaN)()}));
    }

    TEST_CASE("AtomicCondition timed wait reports a notification", "[Sync][AtomicCondition]")
    {
        AtomicCondition   condition;
        std::latch        ready {1};
        std::atomic<bool> notified {false};

        const auto  observedGeneration = condition.Load();
        std::thread worker([&] {
            ready.count_down();
            notified.store(
                    condition.WaitFor(observedGeneration, NGIN::Units::Seconds {2.0}),
                    std::memory_order_release);
        });

        ready.wait();
        condition.NotifyOne();
        worker.join();

        CHECK(notified.load(std::memory_order_acquire));
    }

#ifdef _DEBUG
    TEST_CASE("AtomicCondition debug counters track active waiters", "[Sync][AtomicCondition]")
    {
        AtomicCondition condition;
        std::latch      ready {1};

        std::thread worker([&] {
            ready.count_down();
            condition.Wait();
        });

        ready.wait();
        while (!condition.HasWaitingThreads())
        {
            std::this_thread::yield();
        }

        CHECK(condition.GetWaitingThreadCount() == 1U);
        CHECK(condition.HasWaitingThreads());

        condition.NotifyOne();
        worker.join();

        CHECK(condition.GetGeneration() == 1U);
        CHECK(condition.GetWaitingThreadCount() == 0U);
        CHECK_FALSE(condition.HasWaitingThreads());
    }
#endif
}// namespace NGIN::Sync
