#include <NGIN/Async/AsyncSemaphore.hpp>
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/SyncWait.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>

namespace
{
    using namespace NGIN::Async;

    Task<void> Increment(AsyncSemaphore& semaphore, int& count)
    {
        auto       permit   = co_await semaphore.AcquireAsync();
        const auto previous = count;
        co_await YieldNow();
        count = previous + 1;
        // permit releases automatically, including on failure or cancellation.
    }

    Task<int> Run(TaskContext& context)
    {
        AsyncSemaphore semaphore(1, 1);
        int            count = 0;
        co_await WhenAll(context, Increment(semaphore, count), Increment(semaphore, count));
        co_return count;
    }
}// namespace

int main()
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    TaskContext                          context(scheduler);
    auto                                 result = SyncWait(context, Run(context));
    return result.Succeeded() && result.Value() == 2 ? 0 : 1;
}
