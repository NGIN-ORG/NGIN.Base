/// @file AtomicConditionTest.cpp
/// @brief Tests for NGIN::Async::AtomicCondition using boost::ut

#include <NGIN/Async/AtomicCondition.hpp>
#include <boost/ut.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace boost::ut;
using namespace std::chrono_literals;

suite<"NGIN::Async::AtomicCondition"> atomicConditionTests = [] {
    "SingleThread_NotifyOne"_test = [] {
        // Test that a single waiting thread is awakened by NotifyOne().
        NGIN::Async::AtomicCondition ac;
        std::atomic<int> counter {0};

        std::thread t([&]() {
            ac.Wait();// Thread blocks until notification.
            counter.fetch_add(1, std::memory_order_relaxed);
        });

        // Give the thread time to block.
        std::this_thread::sleep_for(50ms);
        ac.NotifyOne();// Wake one waiting thread.
        t.join();

        expect(counter.load() == 1);
    };

    "MultipleThreads_NotifyOne"_test = [] {
        // Test that NotifyOne() wakes only one thread per call.
        NGIN::Async::AtomicCondition ac;
        std::atomic<int> counter {0};
        std::vector<std::thread> threads;

        // Create 2 threads that wait on the condition.
        threads.reserve(2);
        for (int i = 0; i < 2; ++i)
        {
            threads.emplace_back([&]() {
                ac.Wait();
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        // Allow threads to enter Wait().
        std::this_thread::sleep_for(50ms);
        ac.NotifyOne();// Should wake one thread.
        std::this_thread::sleep_for(50ms);
        expect(counter.load() == 1);

        ac.NotifyOne();// Wake the remaining thread.
        for (auto& t: threads)
        {
            t.join();
        }
        expect(counter.load() == 2);
    };

    "MultipleThreads_NotifyAll"_test = [](int numThreads) {
        // Test that NotifyAll() wakes all waiting threads.
        NGIN::Async::AtomicCondition ac;
        std::atomic<int> counter {0};
        std::vector<std::thread> threads;

        // Spawn several threads waiting on the condition.
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back([&]() {
                ac.Wait();
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(50ms);
        ac.NotifyAll();// Wake all waiting threads.
        for (auto& t: threads)
        {
            t.join();
        }
        expect(counter.load() == numThreads);
    } | std::vector {1, 2, 4, 8};

    "SingleThread_MultipleWaitCycles_ShouldNotifyTwice"_test = [] {
        // Test that a single thread can call Wait() multiple times.
        NGIN::Async::AtomicCondition ac;
        std::atomic<int> counter {0};

        std::thread t([&]() {
            // First wait cycle.
            ac.Wait();
            counter.fetch_add(1, std::memory_order_relaxed);
            // Second wait cycle.
            ac.Wait();
            counter.fetch_add(1, std::memory_order_relaxed);
        });

        std::this_thread::sleep_for(50ms);
        ac.NotifyAll();// Wake the first wait.
        std::this_thread::sleep_for(50ms);
        ac.NotifyAll();// Wake the second wait.
        t.join();

        expect(counter.load() == 2);
    };

#ifdef _DEBUG
    "Debug_Counters_ShouldReflectCorrectStateAfterWait"_test = [] {
        NGIN::Async::AtomicCondition ac;
        expect(ac.GetGeneration() == 0_u);
        expect(ac.GetWaitingThreadCount() == 0_u);
        expect(!ac.HasWaitingThreads());

        std::thread t([&]() {
            ac.Wait();
        });

        std::this_thread::sleep_for(50ms);
        expect(ac.GetWaitingThreadCount() == 1_u);
        expect(ac.HasWaitingThreads());

        ac.NotifyOne();
        t.join();

        expect(ac.GetGeneration() == 1_u);
        expect(ac.GetWaitingThreadCount() == 0_u);
        expect(!ac.HasWaitingThreads());
    };

    "Debug_MultipleWaiters_ShouldNotifyAllInDebugMode"_test = [] {
        NGIN::Async::AtomicCondition ac;
        constexpr int numThreads = 3;
        std::vector<std::thread> threads;

        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back([&]() {
                ac.Wait();
            });
        }

        std::this_thread::sleep_for(50ms);
        expect(ac.GetWaitingThreadCount() == 3_u);
        expect(ac.HasWaitingThreads());

        ac.NotifyAll();
        for (auto& t: threads)
        {
            t.join();
        }

        expect(ac.GetGeneration() == 1_u);
        expect(ac.GetWaitingThreadCount() == 0_u);
        expect(!ac.HasWaitingThreads());
    };
#endif// _DEBUG
};
