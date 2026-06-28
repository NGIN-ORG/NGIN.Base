#include <iostream>
#include <tuple>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Units.hpp>

using namespace NGIN::Async;
using namespace NGIN::Execution;
using namespace NGIN::Units;

#undef Yield

Task<void> SimpleTask(TaskContext& ctx, int id)
{
    std::cout << "[SimpleTask " << id << "] starting\n";
    co_await ctx.YieldNow();
    std::cout << "[SimpleTask " << id << "] resumed after Yield\n";
    co_return;
}

Task<int> DelayedValue(TaskContext& ctx, int value, Milliseconds delay)
{
    std::cout << "[DelayedValue] waiting " << delay.GetValue() << "ms for value " << value << "\n";
    co_await ctx.Delay(delay);
    std::cout << "[DelayedValue] done: " << value << "\n";
    co_return value;
}

Task<void> SimpleContinuation(TaskContext& ctx)
{
    co_await SimpleTask(ctx, 99);
    std::cout << "[Continuation] SimpleTask finished, running continuation!\n";
    co_await ctx.Delay(Milliseconds {500.0});
    std::cout << "[Continuation] Done after delay.\n";
    co_return;
}

Task<void> DelayedValueContinuation(TaskContext& ctx)
{
    const auto result = co_await DelayedValue(ctx, 456, Milliseconds {1000.0});
    std::cout << "[Continuation] DelayedValue result: " << result << ", running continuation!\n";
    co_await ctx.Delay(Milliseconds {300.0});
    std::cout << "[Continuation] Done after delay.\n";
    co_return;
}

Task<void> WhenAllCombinator(TaskContext& ctx)
{
    std::cout << "[WhenAllCombinator] scheduling owned tasks...\n";
    auto results = co_await WhenAll(ctx,
                                    DelayedValue(ctx, 1, Milliseconds {500.0}),
                                    DelayedValue(ctx, 2, Milliseconds {1000.0}),
                                    DelayedValue(ctx, 3, Milliseconds {1500.0}));

    std::cout << "[WhenAllCombinator] results = {" << std::get<0>(results) << ", " << std::get<1>(results) << ", "
              << std::get<2>(results) << "}\n";
    co_return;
}

template<typename SchedulerT>
void RunAllSchedulerTests(const char* schedulerName, int numThreadsOrFibers = 2)
{
    std::cout << "=== Scheduler Test (" << schedulerName << ") Start ===\n\n";
    SchedulerT  scheduler(numThreadsOrFibers);
    TaskContext ctx {scheduler};

    std::cout << "-- Test: SimpleTask --\n";
    auto simple = SyncWait(ctx, SimpleTask(ctx, 42));
    if (!simple)
    {
        std::cout << "-- SimpleTask failed --\n\n";
        return;
    }
    std::cout << "-- SimpleTask Done --\n\n";

    std::cout << "-- Test: SimpleTask continuation --\n";
    auto simpleContinuation = SyncWait(ctx, SimpleContinuation(ctx));
    if (!simpleContinuation)
    {
        std::cout << "-- SimpleTask continuation failed --\n\n";
        return;
    }
    std::cout << "-- SimpleTask continuation Done --\n\n";

    std::cout << "-- Test: DelayedValue --\n";
    auto value = SyncWait(ctx, DelayedValue(ctx, 123, Milliseconds {1500.0}));
    if (!value)
    {
        std::cout << "-- DelayedValue failed --\n\n";
        return;
    }
    std::cout << "-- DelayedValue Result: " << *value << "\n\n";

    std::cout << "-- Test: DelayedValue continuation --\n";
    auto valueContinuation = SyncWait(ctx, DelayedValueContinuation(ctx));
    if (!valueContinuation)
    {
        std::cout << "-- DelayedValue continuation failed --\n\n";
        return;
    }
    std::cout << "-- DelayedValue continuation Done --\n\n";

    std::cout << "-- Test: WhenAllCombinator --\n";
    auto all = SyncWait(ctx, WhenAllCombinator(ctx));
    if (!all)
    {
        std::cout << "-- WhenAllCombinator failed --\n\n";
        return;
    }

    std::cout << "\n=== Scheduler Test (" << schedulerName << ") End ===\n\n";
}

int main()
{
    RunAllSchedulerTests<ThreadPoolScheduler>("ThreadPool", 2);
    RunAllSchedulerTests<FiberScheduler>("Fiber", 1);

    return 0;
}
