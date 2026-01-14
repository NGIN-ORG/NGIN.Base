/// @file FiberTest.cpp
/// @brief Tests for NGIN::Execution::Fiber.

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Execution/Fiber.hpp>
#include <NGIN/Execution/Config.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <exception>
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

#if NGIN_ASYNC_HAS_EXCEPTIONS
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
#endif

#if defined(__linux__) && defined(__x86_64__) && (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
TEST_CASE("Fiber CUSTOM_ASM preserves mxcsr and x87 control word across Yield/Resume", "[Execution][Fiber]")
{
    auto loadMxcsr = [](std::uint32_t value) noexcept {
        asm volatile("ldmxcsr %0" : : "m"(value) : "memory");
    };
    auto storeMxcsr = []() noexcept -> std::uint32_t {
        std::uint32_t value {};
        asm volatile("stmxcsr %0" : "=m"(value) : : "memory");
        return value;
    };
    auto loadFpuCw = [](std::uint16_t value) noexcept {
        asm volatile("fldcw %0" : : "m"(value) : "memory");
    };
    auto storeFpuCw = []() noexcept -> std::uint16_t {
        std::uint16_t value {};
        asm volatile("fnstcw %0" : "=m"(value) : : "memory");
        return value;
    };

    const auto mxcsrOriginal = storeMxcsr();
    const auto fpuOriginal   = storeFpuCw();

    const auto mxcsrCaller = static_cast<std::uint32_t>((mxcsrOriginal ^ (1u << 15)) & 0xFFFFu);
    const auto fpuCaller   = static_cast<std::uint16_t>(fpuOriginal ^ (1u << 10));
    loadMxcsr(mxcsrCaller);
    loadFpuCw(fpuCaller);

    std::uint32_t mxcsrFiberAfterYield = 0;
    std::uint16_t fpuFiberAfterYield   = 0;

    Fiber fiber(FiberOptions {.stackSize = 64 * 1024});
    fiber.Assign([&] {
        const auto mxcsrBefore = storeMxcsr();
        const auto fpuBefore   = storeFpuCw();

        const auto mxcsrFiber = static_cast<std::uint32_t>((mxcsrBefore ^ (1u << 6)) & 0xFFFFu);
        const auto fpuFiber   = static_cast<std::uint16_t>(fpuBefore ^ (1u << 11));
        loadMxcsr(mxcsrFiber);
        loadFpuCw(fpuFiber);

        Fiber::YieldNow();

        mxcsrFiberAfterYield = storeMxcsr();
        fpuFiberAfterYield   = storeFpuCw();
    });

    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(storeMxcsr() == mxcsrCaller);
    CHECK(storeFpuCw() == fpuCaller);

    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(mxcsrFiberAfterYield != 0u);
    CHECK(fpuFiberAfterYield != 0u);
    CHECK(mxcsrFiberAfterYield != mxcsrCaller);
    CHECK(fpuFiberAfterYield != fpuCaller);

    loadMxcsr(mxcsrOriginal);
    loadFpuCw(fpuOriginal);
}
#endif

#if defined(__linux__) && defined(__aarch64__) && (NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
TEST_CASE("Fiber CUSTOM_ASM preserves fpcr across Yield/Resume", "[Execution][Fiber]")
{
    auto readFpcr = []() noexcept -> std::uint64_t {
        std::uint64_t value = 0;
        asm volatile("mrs %0, fpcr" : "=r"(value));
        return value;
    };
    auto writeFpcr = [](std::uint64_t value) noexcept {
        asm volatile("msr fpcr, %0" : : "r"(value) : "memory");
    };

    const auto fpcrOriginal = readFpcr();
    const auto fpcrCaller   = fpcrOriginal ^ (1ull << 24); // toggle FZ (flush-to-zero)
    writeFpcr(fpcrCaller);

    std::uint64_t fpcrFiberAfterYield = 0;

    Fiber fiber(FiberOptions {.stackSize = 64 * 1024});
    fiber.Assign([&] {
        const auto fpcrBefore = readFpcr();
        const auto fpcrFiber  = fpcrBefore ^ (1ull << 24);
        writeFpcr(fpcrFiber);

        Fiber::YieldNow();

        fpcrFiberAfterYield = readFpcr();
    });

    CHECK(fiber.Resume() == FiberResumeResult::Yielded);
    CHECK(readFpcr() == fpcrCaller);

    CHECK(fiber.Resume() == FiberResumeResult::Completed);
    CHECK(fpcrFiberAfterYield != 0u);
    CHECK(fpcrFiberAfterYield != fpcrCaller);

    writeFpcr(fpcrOriginal);
}
#endif

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
