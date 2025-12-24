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
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
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

    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(entered.load());
    CHECK_FALSE(yielded.load());

    CHECK(fiber.Resume() == FiberResumeResult::Completed);
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

    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(counter == 1);
    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(counter == 2);
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(counter == 3);
}

TEST_CASE("Fiber TryAssign only succeeds when idle", "[Execution][Fiber]")
{
    bool entered = false;
    bool finished = false;

    Fiber fiber(FiberOptions {.stackSize = 64 * 1024});
    REQUIRE(fiber.TryAssign([&] {
        entered = true;
        Fiber::YieldNow();
        finished = true;
    }));

    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(entered);
    CHECK_FALSE(finished);

    CHECK_FALSE(fiber.TryAssign([] {}));

    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(finished);

    REQUIRE(fiber.TryAssign([] {}));
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
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
        (void) inner.Resume();
        afterInner1 = true;
        Fiber::YieldNow();
        outerYielded = true;
        (void) inner.Resume();
        afterInner2 = true;
    },
                64 * 1024);

    CHECK(outer.Resume() == FiberResumeResult::Yielded);
    CHECK(outerEntered);
    CHECK(innerEntered);
    CHECK(afterInner1);
    CHECK_FALSE(outerYielded);
    CHECK_FALSE(innerFinished);
    CHECK_FALSE(afterInner2);

    CHECK(outer.Resume() == FiberResumeResult::Completed);
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

    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(counter == 1);
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(counter == 1);
}

TEST_CASE("Fiber respects configured stack size", "[Execution][Fiber]")
{
    Fiber fiber([] {}, 128 * 1024);
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
}

TEST_CASE("Fiber forwards exceptions", "[Execution][Fiber]")
{
    Fiber fiber(
            [&] {
                throw std::runtime_error("boom");
            },
            64 * 1024);
    CHECK(fiber.Resume() == FiberResumeResult::Faulted);
    auto ex = fiber.TakeException();
    REQUIRE(ex != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(ex), std::runtime_error);
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
        CHECK(fiber.Resume() == FiberResumeResult::Completed);
    }

    CHECK(destroyed);
}

TEST_CASE("Concurrent fibers run independently", "[Async][Fiber]")
{
    std::atomic<int> counter {0};
    Fiber            fiberA([&] { counter++; }, 64 * 1024);
    Fiber            fiberB([&] { counter += 2; }, 64 * 1024);

    CHECK(fiberA.Resume() == FiberResumeResult::Completed);
    CHECK(fiberB.Resume() == FiberResumeResult::Completed);
    CHECK(counter.load() == 3);
}

TEST_CASE("Resuming a completed fiber is harmless", "[Async][Fiber]")
{
    Fiber fiber([] {}, 64 * 1024);
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(fiber.Resume() == FiberResumeResult::Completed);
}
