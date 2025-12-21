// main.cpp
#include <iostream>
#include <thread>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>

using namespace NGIN::Async;
using namespace NGIN::Execution;
using namespace NGIN::Units;
#undef Yield
// Simple coroutine that yields once and then prints a message
Task<void> SimpleTask(TaskContext& ctx, int id)
{
    std::cout << "[SimpleTask " << id << "] starting\n";
    co_await ctx.Yield();
    std::cout << "[SimpleTask " << id << "] resumed after Yield\n";
}

// Returns `value` after the specified delay
Task<int> DelayedValue(TaskContext& ctx, int value, Milliseconds delay)
{
    std::cout << "[DelayedValue] waiting " << delay.GetValue() << "ms for value " << value << "\n";
    co_await ctx.Delay(delay);
    std::cout << "[DelayedValue] done: " << value << "\n";
    co_return value;
}

// Run three DelayedValues in parallel, await all, print results
Task<void> WhenAllCombinator(TaskContext& ctx)
{
    std::cout << "[WhenAllCombinator] scheduling parallel tasks...\n";
    auto t1 = ctx.Run(&DelayedValue, 1, Milliseconds {500.0});
    auto t2 = ctx.Run(&DelayedValue, 2, Milliseconds {1000.0});
    auto t3 = ctx.Run(&DelayedValue, 3, Milliseconds {1500.0});

    int r1 = co_await t1;
    int r2 = co_await t2;
    int r3 = co_await t3;

    std::cout << "[WhenAllCombinator] results = {" << r1 << ", " << r2 << ", " << r3 << "}\n";
    co_return;
}

// A helper to run all tests against a scheduler type
template<typename SchedulerT>
void RunAllSchedulerTests(const char* schedulerName, int numThreadsOrFibers = 2)
{
    std::cout << "=== Scheduler Test (" << schedulerName << ") Start ===\n\n";
    SchedulerT scheduler(numThreadsOrFibers);
    TaskContext ctx {scheduler};

    // --- Test: SimpleTask ---
    std::cout << "-- Test: SimpleTask --\n";
    auto s = SimpleTask(ctx, 42);
    s.Start(ctx);
    s.Wait();
    std::cout << "-- SimpleTask Done --\n\n";

    // --- Test: Task<void>::Then() ---
    std::cout << "-- Test: SimpleTask with Then() --\n";
    auto s2 = SimpleTask(ctx, 99);
    s2.Start(ctx);
    auto cont = s2.Then([&ctx]() -> Task<void> {
        std::cout << "[Continuation] SimpleTask finished, running continuation!\n";
        co_await ctx.Delay(Milliseconds {500.0});
        std::cout << "[Continuation] Done after delay.\n";
        co_return;
    });
    cont.parent.Start(ctx);// Start the original task if not already started
    cont.parent.Wait();    // Wait for the original task
    std::cout << "-- SimpleTask with Then() Done --\n\n";

    // --- Test: DelayedValue ---
    std::cout << "-- Test: DelayedValue --\n";
    auto d  = ctx.Run(DelayedValue(ctx, 123, Milliseconds {1500.0}));
    int val = d.Get();
    std::cout << "-- DelayedValue Result: " << val << "\n\n";

    // --- Test: Task<int>::Then() ---
    std::cout << "-- Test: DelayedValue with Then() --\n";
    auto d2    = ctx.Run(DelayedValue(ctx, 456, Milliseconds {1000.0}));
    auto cont2 = d2.Then([&ctx](int result) -> Task<void> {
        std::cout << "[Continuation] DelayedValue result: " << result << ", running continuation!\n";
        co_await ctx.Delay(Milliseconds {300.0});
        std::cout << "[Continuation] Done after delay.\n";
        co_return;
    });
    cont2.parent.Start(ctx);
    cont2.parent.Wait();
    std::cout << "-- DelayedValue with Then() Done --\n\n";

    // --- Test: WhenAllCombinator ---
    std::cout << "-- Test: WhenAllCombinator --\n";
    auto allTask = WhenAllCombinator(ctx);
    allTask.Start(ctx);
    while (!allTask.IsCompleted())
    {
        std::cout << "[Main] waiting for tasks to complete...\n";
        NGIN::Time::SleepFor(Milliseconds {100.0});
    }

    std::cout << "\n=== Scheduler Test (" << schedulerName << ") End ===\n\n";
}

int main()
{
    RunAllSchedulerTests<ThreadPoolScheduler>("ThreadPool", 2);
    RunAllSchedulerTests<FiberScheduler>("Fiber", 1);

    return 0;
}
