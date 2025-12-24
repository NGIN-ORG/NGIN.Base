#include <NGIN/Benchmark.hpp>
#include <NGIN/Execution/Fiber.hpp>

#include <iostream>
#include <stdexcept>

int main()
{
    using namespace NGIN;
    using namespace NGIN::Execution;

    Benchmark::Register([](BenchmarkContext& ctx) {
        Fiber fiber(FiberOptions {.stackSize = 64uz * 1024uz});

        ctx.start();
        fiber.Assign([] { Fiber::YieldNow(); });
        (void) fiber.Resume();
        (void) fiber.Resume();
        ctx.stop();
    },
                        "Fiber Assign + Resume(Yield) + Resume(Complete)");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Fiber fiber(FiberOptions {.stackSize = 64uz * 1024uz});

        ctx.start();
        fiber.Assign([] { Fiber::YieldNow(); });
        (void) fiber.Resume();
        ctx.stop();
    },
                        "Fiber Assign + Resume(Yield)");

    Benchmark::Register([](BenchmarkContext& ctx) {
        Fiber fiber(FiberOptions {.stackSize = 64uz * 1024uz});

        ctx.start();
        fiber.Assign([] {
            throw std::runtime_error("boom");
        });
        (void) fiber.Resume();
        (void) fiber.TakeException();
        ctx.stop();
    },
                        "Fiber Assign + Resume(Fault) + TakeException");

    const auto results = Benchmark::RunAll<Units::Nanoseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
