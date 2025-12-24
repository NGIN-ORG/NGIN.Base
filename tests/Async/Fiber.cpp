/// @file FiberTest.cpp
/// @brief Tests for NGIN::Execution::Fiber.

#include <NGIN/Execution/Fiber.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <stdexcept>

using namespace NGIN::Execution;

TEST_CASE("Fiber constructs with default behavior", "[Execution][Fiber]")
{
    Fiber fiber([] {}, 64 * 1024);
    CHECK_NOTHROW(fiber.Resume());
}

TEST_CASE("Fiber yields and resumes", "[Execution][Fiber]")
{
    std::atomic<bool> entered {false};
    std::atomic<bool> yielded {false};

    Fiber fiber([&] {
        entered = true;
        Fiber::YieldNow();
        yielded = true;
    },
                64 * 1024);

    fiber.Resume();
    CHECK(entered.load());
    CHECK_FALSE(yielded.load());

    fiber.Resume();
    CHECK(yielded.load());
}

TEST_CASE("Fiber supports multiple yield/resume cycles", "[Execution][Fiber]")
{
    int   counter = 0;
    Fiber fiber([&] {
        counter++;
        Fiber::YieldNow();
        counter++;
        Fiber::YieldNow();
        counter++;
    },
                64 * 1024);

    fiber.Resume();
    CHECK(counter == 1);
    fiber.Resume();
    CHECK(counter == 2);
    fiber.Resume();
    CHECK(counter == 3);
}

TEST_CASE("Fiber yields back to resumer (nested resume)", "[Execution][Fiber]")
{
    bool outerEntered  = false;
    bool innerEntered  = false;
    bool afterInner1   = false;
    bool outerYielded  = false;
    bool afterOuter    = false;
    bool innerFinished = false;
    bool afterInner2   = false;

    Fiber inner([&] {
        innerEntered = true;
        Fiber::YieldNow();
        innerFinished = true;
    },
                64 * 1024);

    Fiber outer([&] {
        outerEntered = true;
        inner.Resume();
        afterInner1 = true;
        Fiber::YieldNow();
        outerYielded = true;
        inner.Resume();
        afterInner2 = true;
    },
                64 * 1024);

    outer.Resume();
    CHECK(outerEntered);
    CHECK(innerEntered);
    CHECK(afterInner1);
    CHECK_FALSE(outerYielded);
    CHECK_FALSE(innerFinished);
    CHECK_FALSE(afterInner2);

    outer.Resume();
    afterOuter = true;
    CHECK(afterOuter);
    CHECK(outerYielded);
    CHECK(innerFinished);
    CHECK(afterInner2);
}

TEST_CASE("Fiber completes once", "[Execution][Fiber]")
{
    int   counter = 0;
    Fiber fiber([&] { counter++; }, 64 * 1024);

    fiber.Resume();
    CHECK(counter == 1);
    fiber.Resume();
    CHECK(counter == 1);
}

TEST_CASE("Fiber respects configured stack size", "[Execution][Fiber]")
{
    Fiber fiber([] {}, 128 * 1024);
    CHECK_NOTHROW(fiber.Resume());
}

TEST_CASE("Fiber forwards exceptions", "[Execution][Fiber]")
{
    Fiber fiber(
            [&] {
                throw std::runtime_error("boom");
            },
            64 * 1024);
    REQUIRE_THROWS_AS(fiber.Resume(), std::runtime_error);
}

TEST_CASE("Fiber cleans up derived resources", "[Execution][Fiber]")
{
    bool destroyed = false;
    struct TestFiber : Fiber
    {
        TestFiber(std::function<void()> fn, std::size_t stack, bool& destroyedFlag)
            : Fiber(std::move(fn), stack), flag(destroyedFlag) {}
        ~TestFiber() { flag = true; }
        bool& flag;
    };

    {
        TestFiber fiber([] {}, 64 * 1024, destroyed);
        fiber.Resume();
    }

    CHECK(destroyed);
}

TEST_CASE("Concurrent fibers run independently", "[Async][Fiber]")
{
    std::atomic<int> counter {0};
    Fiber            fiberA([&] { counter++; }, 64 * 1024);
    Fiber            fiberB([&] { counter += 2; }, 64 * 1024);

    fiberA.Resume();
    fiberB.Resume();
    CHECK(counter.load() == 3);
}

TEST_CASE("Resuming a completed fiber is harmless", "[Async][Fiber]")
{
    Fiber fiber([] {}, 64 * 1024);
    fiber.Resume();
    CHECK_NOTHROW(fiber.Resume());
}
