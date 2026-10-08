#include <NGIN/Async/AsyncSemaphore.hpp>
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <chrono>
#include <iostream>

namespace
{
    using namespace NGIN::Async;
    constexpr int iterations = 10000;

    Task<void> AcquireLoop(AsyncSemaphore& semaphore, bool yield)
    {
        for (int index = 0; index != iterations; ++index)
        {
            auto permit = co_await semaphore.AcquireAsync();
            if (yield)
                co_await YieldNow();
        }
    }

    Task<void> Contended(TaskContext& context, AsyncSemaphore& semaphore)
    {
        co_await WhenAll(context, AcquireLoop(semaphore, true), AcquireLoop(semaphore, true));
    }
}// namespace

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    AsyncSemaphore                        semaphore(1, 1);
    for (bool contended: {false, true})
    {
        const auto       start = std::chrono::steady_clock::now();
        auto             task  = contended ? Contended(context, semaphore) : AcquireLoop(semaphore, false);
        TaskSupervisor<> root(context.GetEnvironment(), 1);
        auto             operation = root.Spawn(std::move(task)).value();
        scheduler.RunUntilIdle();
        if (!root.TryJoin() || !operation.TakeResult().Succeeded() || semaphore.CurrentCount() != 1)
            return 1;
        const auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        std::cout << (contended ? "contended (includes yield): " : "uncontended: ")
                  << elapsed / (iterations * (contended ? 2 : 1)) << " ns/acquisition\n";
    }
}
