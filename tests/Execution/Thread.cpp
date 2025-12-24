/// @file Thread.cpp
/// @brief Tests for NGIN::Execution::Thread and ThisThread.

#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/Thread.hpp>

#include <atomic>
#include <catch2/catch_test_macros.hpp>

namespace NGIN::Execution
{
    TEST_CASE("ThisThread basic utilities", "[Execution][Thread]")
    {
        CHECK(ThisThread::GetId() != 0);
        ThisThread::YieldNow();
        ThisThread::RelaxCpu();
        (void) ThisThread::SetName("ngin-test");
    }

    TEST_CASE("Thread starts and joins", "[Execution][Thread]")
    {
        std::atomic<bool> ran {false};

        Thread t;
        Thread::Options options {};
        options.name = ThreadName("ngin-thread");
        options.onDestruct = Thread::OnDestruct::Terminate;

        t.Start([&] { ran.store(true, std::memory_order_release); }, options);
        t.Join();
        CHECK(ran.load(std::memory_order_acquire));
    }

    TEST_CASE("WorkerThread joins on destruction", "[Execution][Thread]")
    {
        std::atomic<bool> ran {false};
        {
            WorkerThread t([&] { ran.store(true, std::memory_order_release); });
        }
        CHECK(ran.load(std::memory_order_acquire));
    }
}// namespace NGIN::Execution

