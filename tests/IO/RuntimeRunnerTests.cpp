#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Race.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>

#include <NGIN/Time/MonotonicClock.hpp>
#include <atomic>
#include <future>
#include <thread>

namespace
{
    NGIN::Async::Task<int> TimedRoot(NGIN::Async::TaskContext& context, NGIN::IO::Runtime& runtime)
    {
        co_await context.YieldNow();
        co_await context.Delay(NGIN::Units::Milliseconds(1));
        runtime.RequestStop();
        co_return 42;
    }
}// namespace

TEST_CASE("Runtime executor drives yields and timers on the application thread", "[IO][Runtime]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    auto                     resultTask = TimedRoot(context, runtime);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(resultTask)::ErrorType> resultRoot(rootContext.GetEnvironment(), 1);

    auto result = resultRoot.SpawnOn(context.GetExecutor(), std::move(resultTask)).value();
    runtime.Run();
    rootExecutor.RunUntilIdle();
    REQUIRE(runtime.IsStopped());
    REQUIRE(result.IsCompleted());
    REQUIRE(resultRoot.TryJoin());
    REQUIRE(result.TakeResult().Value() == 42);
    REQUIRE_FALSE(runtime.HasFileBackend());
    REQUIRE_FALSE(runtime.HasNetworkBackend());
}

TEST_CASE("Runtime runner establishes ownership before construction returns", "[IO][Runtime][Ownership]")
{
    NGIN::IO::Runtime       runtime;
    NGIN::IO::RuntimeRunner runner(runtime);
    REQUIRE_THROWS_AS(runtime.PollOnce(), std::logic_error);
    std::promise<std::thread::id> executed;
    auto                          result   = executed.get_future();
    bool                          affinity = false;
    REQUIRE(runtime.GetExecutor().Execute([&] {
        affinity = runtime.GetExecutor().IsCurrent();
        executed.set_value(std::this_thread::get_id());
    }));
    REQUIRE(result.get() != std::this_thread::get_id());
    REQUIRE(affinity);
    runtime.Shutdown();
    runner.Shutdown();
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("Runtime runner cannot take over a manually driven runtime", "[IO][Runtime][Ownership]")
{
    NGIN::IO::Runtime runtime;
    REQUIRE_FALSE(runtime.PollOnce());
    REQUIRE_THROWS_AS(NGIN::IO::RuntimeRunner(runtime), std::logic_error);
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Running);
    runtime.Shutdown();
}

TEST_CASE("Runtime runner callbacks can request stop and reject blocking shutdown", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime       runtime;
    NGIN::IO::RuntimeRunner runner(runtime);
    std::promise<bool>      completed;
    auto                    result = completed.get_future();
    REQUIRE(runtime.GetExecutor().Execute([&] {
        bool runtimeRejected = false;
        bool runnerRejected  = false;
        try
        {
            runtime.Shutdown();
        } catch (const std::logic_error&)
        {
            runtimeRejected = true;
        }
        try
        {
            runner.Shutdown();
        } catch (const std::logic_error&)
        {
            runnerRejected = true;
        }
        runtime.RequestStop();
        completed.set_value(runtimeRejected && runnerRejected);
    }));
    REQUIRE(result.get());
    runner.Shutdown();
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("Runtime stopping preserves admitted task continuations and subsequent child joins", "[IO][Runtime][Shutdown]")
{
    // One reservation is enough for this root's lifetime and all of its child
    // resumptions. Neither saturation nor Stopping may reject a later join.
    NGIN::IO::Runtime                     runtime({.completionCapacity = 1, .batchSize = 1});
    NGIN::Execution::CooperativeScheduler firstExecutor;
    NGIN::Execution::CooperativeScheduler secondExecutor;
    NGIN::Async::TaskContext              context(runtime.GetExecutor());
    NGIN::Async::TaskContext              firstContext(firstExecutor);
    NGIN::Async::TaskContext              secondContext(secondExecutor);
    auto                                  child     = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  firstTask = child(firstContext);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(firstContext.GetEnvironment(), 1);

    auto first      = firstRoot.Spawn(std::move(firstTask)).value();
    auto secondTask = child(secondContext);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(secondContext.GetEnvironment(), 1);

    auto second   = secondRoot.Spawn(std::move(secondTask)).value();
    int  resumes  = 0;
    bool affinity = true;
    auto root     = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
        const auto firstResult = co_await first;
        affinity               = affinity && runtime.GetExecutor().IsCurrent();
        ++resumes;
        const auto secondResult = co_await second;
        affinity                = affinity && runtime.GetExecutor().IsCurrent();
        ++resumes;
        co_return firstResult.Value() + secondResult.Value();
    };
    auto resultTask = root(context);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(resultTask)::ErrorType> resultRoot(rootContext.GetEnvironment(), 1);

    auto result = resultRoot.SpawnOn(context.GetExecutor(), std::move(resultTask)).value();
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    runtime.RequestStop();
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    REQUIRE_FALSE(result.IsCompleted());
    firstExecutor.RunUntilIdle();
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(resumes == 0);
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    REQUIRE(resumes == 1);
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    secondExecutor.RunUntilIdle();
    REQUIRE(secondRoot.TryJoin());
    REQUIRE(resumes == 1);
    runtime.Shutdown();
    rootExecutor.RunUntilIdle();
    REQUIRE(runtime.IsStopped());
    REQUIRE(result.IsCompleted());
    REQUIRE(resultRoot.TryJoin());
    REQUIRE(result.TakeResult().Value() == 42);
    REQUIRE(resumes == 2);
    REQUIRE(affinity);
}

TEST_CASE("Runtime task reservation exhaustion rejects execution before its body starts", "[IO][Runtime][Admission]")
{
    NGIN::IO::Runtime        runtime({.completionCapacity = 1});
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    auto                     occupied = runtime.GetExecutor().ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE(occupied);
    bool entered    = false;
    auto root       = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> { entered = true; co_return; };
    auto resultTask = root(context);

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(resultTask)::ErrorType> resultRoot(rootContext.GetEnvironment(), 1);

    auto result = resultRoot.SpawnOn(context.GetExecutor(), std::move(resultTask));
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    REQUIRE_FALSE(entered);
    REQUIRE(resultRoot.TryJoin());
    occupied->Reset();
    runtime.Shutdown();
    rootExecutor.RunUntilIdle();
}

TEST_CASE("WhenAll joins every admitted child after runtime stop without new task admission", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime                     runtime({.completionCapacity = 5, .batchSize = 1});
    NGIN::Execution::CooperativeScheduler firstExecutor;
    NGIN::Execution::CooperativeScheduler secondExecutor;
    NGIN::Async::TaskContext              context(runtime.GetExecutor());
    NGIN::Async::TaskContext              firstContext(firstExecutor);
    NGIN::Async::TaskContext              secondContext(secondExecutor);
    auto                                  external  = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  firstTask = external(firstContext);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(firstContext.GetEnvironment(), 1);

    auto first      = firstRoot.Spawn(std::move(firstTask)).value();
    auto secondTask = external(secondContext);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(secondContext.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    auto child  = [](NGIN::Async::TaskContext&, NGIN::Async::Operation<int>& operation) -> NGIN::Async::Task<int> {
        auto result = co_await operation;
        co_return result.Value();
    };
    auto operationTask = NGIN::Async::WhenAll(context, child(context, first), child(context, second));

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(context.GetExecutor(), std::move(operationTask)).value();
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    runtime.RequestStop();
    firstExecutor.RunUntilIdle();
    REQUIRE(firstRoot.TryJoin());
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    const bool completedBeforeSecondChild = operation.IsCompleted();
    secondExecutor.RunUntilIdle();
    REQUIRE(secondRoot.TryJoin());
    runtime.Shutdown();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeSecondChild);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(std::get<0>(result.Value()) == 21);
    REQUIRE(std::get<1>(result.Value()) == 21);
}

TEST_CASE("Race drains losing external work after runtime admission closes", "[IO][Runtime][Shutdown][Race]")
{
    // Exactly the parent, two notifications, and two child task slots.
    NGIN::IO::Runtime                     runtime({.completionCapacity = 5, .batchSize = 1});
    NGIN::Execution::CooperativeScheduler firstExecutor;
    NGIN::Execution::CooperativeScheduler secondExecutor;
    NGIN::Async::TaskContext              context(runtime.GetExecutor());
    NGIN::Async::TaskContext              firstContext(firstExecutor);
    NGIN::Async::TaskContext              secondContext(secondExecutor);
    auto                                  external  = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  firstTask = external(firstContext);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(firstContext.GetEnvironment(), 1);

    auto first      = firstRoot.Spawn(std::move(firstTask)).value();
    auto secondTask = external(secondContext);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(secondContext.GetEnvironment(), 1);

    auto second       = secondRoot.Spawn(std::move(secondTask)).value();
    bool firstResumed = false;
    bool affinity     = false;
    int  destroyed    = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    auto operationTask = NGIN::Async::Race(context, [&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<int> {
            Guard guard {destroyed};
            auto result = co_await first;
            firstResumed = true;
            affinity = child.GetExecutor().IsCurrent();
            co_return result.Value(); }, [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
            Guard guard {destroyed};
            auto result = co_await second;
            co_return result; });

    NGIN::Execution::CooperativeScheduler rootExecutor;

    NGIN::Async::TaskContext rootContext(rootExecutor);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(context.GetExecutor(), std::move(operationTask)).value();
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    runtime.RequestStop();
    firstExecutor.RunUntilIdle();
    REQUIRE(firstRoot.TryJoin());
    const bool ranOnExternal = firstResumed;
    while (runtime.PollOnce()) {}
    rootExecutor.RunUntilIdle();
    const bool completedBeforeLoser = operation.IsCompleted();
    const bool stillStopping        = runtime.GetState() == NGIN::IO::Runtime::State::Stopping;
    secondExecutor.RunUntilIdle();
    REQUIRE(secondRoot.TryJoin());
    const bool completedOutsideRuntime = operation.IsCompleted();
    runtime.Shutdown();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(ranOnExternal);
    REQUIRE(firstResumed);
    REQUIRE(affinity);
    REQUIRE_FALSE(completedBeforeLoser);
    REQUIRE(stillStopping);
    REQUIRE_FALSE(completedOutsideRuntime);
    REQUIRE(operation.IsCompleted());
    REQUIRE(destroyed == 2);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value().index == 0);
}

TEST_CASE("Timed runtime shutdown retains reserved cleanup for later owner drainage", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime runtime;
    bool delivered = false;
    auto completion = runtime.GetExecutor().ReserveCompletion(NGIN::Execution::WorkItem([&] { delivered = true; }));
    REQUIRE(completion);
    const auto expired = NGIN::Time::TimePoint {};
    REQUIRE(runtime.ShutdownUntil(expired) == NGIN::IO::Runtime::ShutdownStatus::Incomplete);
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    REQUIRE_FALSE(delivered);
    REQUIRE_FALSE(runtime.GetExecutor().Execute([] {}));
    completion->Dispatch();
    const auto deadline = NGIN::Time::TimePoint::FromNanoseconds(
            NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 1000000000);
    const auto finished = runtime.ShutdownUntil(deadline);
    runtime.Shutdown();
    REQUIRE(finished == NGIN::IO::Runtime::ShutdownStatus::Stopped);
    REQUIRE(delivered);
    REQUIRE(runtime.ShutdownUntil(expired) == NGIN::IO::Runtime::ShutdownStatus::Stopped);
}

TEST_CASE("Timed runtime shutdown leaves a runner driving admitted completion", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime runtime;
    std::atomic<bool> delivered {false};
    auto completion = runtime.GetExecutor().ReserveCompletion(NGIN::Execution::WorkItem([&] { delivered.store(true); }));
    REQUIRE(completion);
    NGIN::IO::RuntimeRunner runner(runtime);
    const auto deadline = NGIN::Time::TimePoint::FromNanoseconds(
            NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 1000000);
    const auto status = runtime.ShutdownUntil(deadline);
    // Release the held reservation before assertions, including on a failing status.
    completion->Dispatch();
    runner.Shutdown();
    REQUIRE(status == NGIN::IO::Runtime::ShutdownStatus::Incomplete);
    REQUIRE(delivered.load());
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("Timed runtime shutdown rejects callback and inactive foreign-owner waits", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime runtime;
    bool rejected = false;
    REQUIRE(runtime.GetExecutor().Execute([&] {
        try { (void) runtime.ShutdownUntil(NGIN::Time::TimePoint {}); }
        catch (const std::logic_error&) { rejected = true; }
    }));
    runtime.PollOnce();
    REQUIRE(rejected);
    auto foreign = std::async(std::launch::async, [&] {
        try { (void) runtime.ShutdownUntil(NGIN::Time::TimePoint {}); }
        catch (const std::logic_error&) { return true; }
        return false;
    });
    REQUIRE(foreign.get());
    runtime.Shutdown();
}
