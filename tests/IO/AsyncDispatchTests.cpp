#include <catch2/catch_test_macros.hpp>

#include "../../src/NGIN/IO/AsyncDispatch.hpp"
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <semaphore>
#include <thread>

namespace
{
    class WorkerGate final
    {
    public:
        ~WorkerGate() { release.release(); }
        std::binary_semaphore entered {0};
        std::binary_semaphore release {0};
    };

    void Barrier(NGIN::Execution::ThreadPoolScheduler& scheduler)
    {
        std::promise<void> reached;
        auto               ready = reached.get_future();
        REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem([&] { reached.set_value(); })));
        ready.get();
    }

    template<typename Predicate>
    bool PumpUntil(NGIN::Execution::ThreadPoolScheduler& scheduler, Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            Barrier(scheduler);
            if (predicate())
                return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    template<typename Job>
    NGIN::Async::Task<NGIN::IO::detail::DriverCompletion<int>> Dispatch(
            NGIN::Async::TaskContext& ctx, NGIN::IO::detail::FileSystemDriver& driver, Job job)
    {
        co_return co_await NGIN::IO::detail::DispatchToDriver(driver, ctx, std::move(job));
    }
}// namespace

TEST_CASE("IO worker cancellation waits for the final access to borrowed memory", "[IO][Runtime][Lifetime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    WorkerGate                           gate;
    NGIN::Async::CancellationSource      cancellation;
    NGIN::Async::TaskContext             ctx(scheduler, cancellation.GetToken());
    int                                  borrowed  = 0;
    auto                                 operation = NGIN::Async::Spawn(ctx, Dispatch(ctx, driver, [&] {
                                            gate.entered.release();
                                            gate.release.acquire();
                                            borrowed = 42;
                                            return borrowed;
                                        }));
    Barrier(scheduler);
    REQUIRE(gate.entered.try_acquire_for(std::chrono::seconds(5)));
    cancellation.Cancel();
    Barrier(scheduler);
    const bool completedBeforeWorkerReturned = operation.IsCompleted();
    gate.release.release();
    REQUIRE(PumpUntil(scheduler, [&] { return operation.IsCompleted(); }));
    REQUIRE_FALSE(completedBeforeWorkerReturned);
    REQUIRE(borrowed == 42);
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().IsCanceled());
}

TEST_CASE("IO cancellation before a queued worker starts skips its side effects", "[IO][Runtime][Lifetime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    WorkerGate                           gate;
    REQUIRE(driver.GetExecutor().Execute([&] {
        gate.entered.release();
        gate.release.acquire();
    }));
    REQUIRE(gate.entered.try_acquire_for(std::chrono::seconds(5)));
    NGIN::Async::CancellationSource cancellation;
    NGIN::Async::TaskContext        ctx(scheduler, cancellation.GetToken());
    std::atomic<bool>               executed {false};
    auto                            operation = NGIN::Async::Spawn(ctx, Dispatch(ctx, driver, [&] {
                                            executed.store(true);
                                            return 42;
                                        }));
    Barrier(scheduler);
    cancellation.Cancel();
    gate.release.release();
    REQUIRE(PumpUntil(scheduler, [&] { return operation.IsCompleted(); }));
    REQUIRE_FALSE(executed.load());
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().IsCanceled());
}

TEST_CASE("IO worker success publishes its result on the selected executor", "[IO][Runtime][Lifetime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    NGIN::Async::TaskContext             ctx(scheduler);
    std::promise<std::thread::id>        ownerPromise;
    auto                                 ownerFuture = ownerPromise.get_future();
    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem([&] { ownerPromise.set_value(std::this_thread::get_id()); })));
    const std::thread::id owner = ownerFuture.get();
    std::thread::id       completionThread;
    auto                  work = [&](NGIN::Async::TaskContext& context) -> NGIN::Async::Task<int> {
        auto result      = co_await NGIN::IO::detail::DispatchToDriver(driver, context, [] { return 42; });
        completionThread = std::this_thread::get_id();
        co_return *result.result;
    };
    auto operation = NGIN::Async::Spawn(ctx, work(ctx));
    REQUIRE(PumpUntil(scheduler, [&] { return operation.IsCompleted(); }));
    REQUIRE(completionThread == owner);
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("IO rejects an unsupported completion executor before running worker code", "[IO][Runtime][Admission]")
{
    NGIN::Execution::InlineScheduler   scheduler;
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    NGIN::Async::TaskContext           ctx(scheduler);
    bool                               executed = false;
    auto                               result   = NGIN::Async::SyncWait(ctx, Dispatch(ctx, driver, [&] {
                                            executed = true;
                                            return 42;
                                        }));
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().IsFault());
    REQUIRE(result.Value().fault->native == static_cast<int>(NGIN::Execution::ScheduleError::Rejected));
    REQUIRE_FALSE(executed);
}

TEST_CASE("IO rejects exhausted completion capacity before running worker code", "[IO][Runtime][Admission]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1, 2);
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    NGIN::Async::TaskContext             ctx(scheduler);
    auto                                 ticket = scheduler.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE(ticket);
    bool executed = false;
    auto result   = NGIN::Async::SyncWait(ctx, Dispatch(ctx, driver, [&] {
                                            executed = true;
                                            return 42;
                                        }));
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().IsFault());
    REQUIRE(result.Value().fault->native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
    REQUIRE_FALSE(executed);
    ticket->Reset();
    auto recovered = NGIN::Async::SyncWait(ctx, Dispatch(ctx, driver, [] { return 42; }));
    REQUIRE(recovered.Succeeded());
    REQUIRE(recovered.Value().IsResult());
    REQUIRE(*recovered.Value().result == 42);
}

TEST_CASE("File workers start lazily and reject jobs beyond their budget", "[IO][Runtime][Admission]")
{
    NGIN::IO::Runtime runtime({.files = {.queueDepthHint    = 1,
                                               .backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    REQUIRE_FALSE(driver.HasWorkers());
    auto executor = driver.GetExecutor();
    REQUIRE_FALSE(driver.HasWorkers());
    WorkerGate         gate;
    std::promise<void> completed;
    auto               completion = completed.get_future();
    REQUIRE(executor.Execute([&] {
        gate.entered.release();
        gate.release.acquire();
        completed.set_value();
    }));
    REQUIRE(gate.entered.try_acquire_for(std::chrono::seconds(5)));
    REQUIRE(driver.HasWorkers());
    auto saturated = executor.Execute([] {});
    driver.Stop();
    auto stopped = executor.Execute([] {});
    gate.release.release();
    completion.get();
    REQUIRE_FALSE(saturated);
    REQUIRE(saturated.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    REQUIRE_FALSE(stopped);
    REQUIRE(stopped.error() == NGIN::Execution::ScheduleError::Stopped);
}

TEST_CASE("Runtime shutdown waits for a worker then transfers an external completion", "[IO][Runtime][Shutdown]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner runner(runtime);
    NGIN::IO::detail::FileSystemDriver driver(runtime);
    WorkerGate gate;
    NGIN::Async::TaskContext context(scheduler);
    int borrowed = 0;
    auto operation = NGIN::Async::Spawn(context, Dispatch(context, driver, [&] {
        gate.entered.release();
        gate.release.acquire();
        borrowed = 42;
        return borrowed;
    }));
    scheduler.RunUntilIdle();
    REQUIRE(gate.entered.try_acquire_for(std::chrono::seconds(5)));
    runtime.RequestStop();
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    REQUIRE_FALSE(operation.IsCompleted());
    gate.release.release();
    runtime.Shutdown();
    REQUIRE(runtime.IsStopped());
    REQUIRE(borrowed == 42);
    REQUIRE_FALSE(operation.IsCompleted());
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().Value().IsCanceled());
}
