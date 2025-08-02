/// @file FiberTest.cpp
/// @brief Tests for NGIN::Async::Fiber using boost::ut

#include <NGIN/Async/Fiber.hpp>
#include <boost/ut.hpp>
#include <atomic>
#include <stdexcept>

using namespace boost::ut;
using namespace NGIN::Async;

suite<"NGIN::Async::Fiber"> fiberTests = [] {
    "BasicConstruction"_test = [] {
        Fiber fib([] {}, 64 * 1024);
        expect(true);
    };

    "SingleResumeYield"_test = [] {
        std::atomic<bool> entered {false}, yielded {false};
        Fiber fib([&] {
            std::cout << "Fiber started\n";
            entered = true;
            std::cout << "Fiber yielding\n";
            Fiber::Yield();
            yielded = true;
        },
                  64 * 1024);
        fib.Resume();
        std::cout << "Main resumed after fiber yield\n";
        expect(entered.load());
        expect(!yielded.load());
        fib.Resume();
        expect(yielded.load());
    };

    "MultipleResumeYield"_test = [] {
        int counter = 0;
        Fiber fib([&] {
            counter++;
            Fiber::Yield();
            counter++;
            Fiber::Yield();
            counter++;
        },
                  64 * 1024);
        fib.Resume();
        expect(counter == 1);
        fib.Resume();
        expect(counter == 2);
        fib.Resume();
        expect(counter == 3);
    };

    "CompletionBehavior"_test = [] {
        int counter = 0;
        Fiber fib([&] { counter++; }, 64 * 1024);
        fib.Resume();
        expect(counter == 1);
        fib.Resume();// Should not increment again
        expect(counter == 1);
    };

    "StackSizeHandling"_test = [] {
        Fiber fib([] {}, 128 * 1024);
        expect(true);
    };

    "ExceptionHandling"_test = [] {
        expect(throws([] {
            Fiber fib([] { throw std::runtime_error("fiber error"); }, 64 * 1024);
            fib.Resume();
        }));
    };

    "ResourceCleanup"_test = [] {
        bool destroyed = false;
        struct TestFiber : Fiber
        {
            TestFiber(std::function<void()> f, std::size_t s, bool& d)
                : Fiber(f, s), destroyedRef(d) {}
            ~TestFiber()
            {
                destroyedRef = true;
            }
            bool& destroyedRef;
        };
        bool flag = false;
        {
            TestFiber fib([] {}, 64 * 1024, flag);
        }
        expect(flag);
    };

    "ConcurrentFibers"_test = [] {
        std::atomic<int> count {0};
        Fiber fib1([&] { count++; }, 64 * 1024);
        Fiber fib2([&] { count += 2; }, 64 * 1024);
        fib1.Resume();
        fib2.Resume();
        expect(count.load() == 3);
    };

    "EdgeCases"_test = [] {
        Fiber fib([] {}, 64 * 1024);
        fib.Resume();
        fib.Resume();// Should not crash or re-execute
        expect(true);
    };
};
