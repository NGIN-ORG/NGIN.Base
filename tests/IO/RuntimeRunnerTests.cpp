#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/WhenAny.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>

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
    auto                     result = NGIN::Async::Spawn(context, TimedRoot(context, runtime));
    runtime.Run();
    REQUIRE(runtime.IsStopped());
    REQUIRE(result.IsCompleted());
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
    auto                                  child    = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  first    = NGIN::Async::Spawn(firstContext, child(firstContext));
    auto                                  second   = NGIN::Async::Spawn(secondContext, child(secondContext));
    int                                   resumes  = 0;
    bool                                  affinity = true;
    auto                                  root     = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
        const auto firstResult = co_await first;
        affinity               = affinity && runtime.GetExecutor().IsCurrent();
        ++resumes;
        const auto secondResult = co_await second;
        affinity                = affinity && runtime.GetExecutor().IsCurrent();
        ++resumes;
        co_return firstResult.Value() + secondResult.Value();
    };
    auto result = NGIN::Async::Spawn(context, root(context));
    while (runtime.PollOnce()) {}
    runtime.RequestStop();
    while (runtime.PollOnce()) {}
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    REQUIRE_FALSE(result.IsCompleted());
    firstExecutor.RunUntilIdle();
    REQUIRE(resumes == 0);
    while (runtime.PollOnce()) {}
    REQUIRE(resumes == 1);
    REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Stopping);
    secondExecutor.RunUntilIdle();
    REQUIRE(resumes == 1);
    runtime.Shutdown();
    REQUIRE(runtime.IsStopped());
    REQUIRE(result.IsCompleted());
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
    bool entered = false;
    auto root    = [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> { entered = true; co_return; };
    auto result  = NGIN::Async::Spawn(context, root(context));
    REQUIRE(result.IsCompleted());
    REQUIRE_FALSE(entered);
    auto completion = result.TakeResult();
    REQUIRE(completion.IsFault());
    REQUIRE(completion.Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
    occupied->Reset();
    runtime.Shutdown();
}

TEST_CASE("WhenAll joins every admitted child after runtime stop without new task admission", "[IO][Runtime][Shutdown]")
{
    NGIN::IO::Runtime                     runtime({.completionCapacity = 3, .batchSize = 1});
    NGIN::Execution::CooperativeScheduler firstExecutor;
    NGIN::Execution::CooperativeScheduler secondExecutor;
    NGIN::Async::TaskContext              context(runtime.GetExecutor());
    NGIN::Async::TaskContext              firstContext(firstExecutor);
    NGIN::Async::TaskContext              secondContext(secondExecutor);
    auto                                  external = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  first    = NGIN::Async::Spawn(firstContext, external(firstContext));
    auto                                  second   = NGIN::Async::Spawn(secondContext, external(secondContext));
    auto                                  child    = [](NGIN::Async::TaskContext&, NGIN::Async::Operation<int>& operation) -> NGIN::Async::Task<int> {
        auto result = co_await operation;
        co_return result.Value();
    };
    auto operation = NGIN::Async::Spawn(context, NGIN::Async::WhenAll(context, child(context, first), child(context, second)));
    while (runtime.PollOnce()) {}
    runtime.RequestStop();
    firstExecutor.RunUntilIdle();
    while (runtime.PollOnce()) {}
    const bool completedBeforeSecondChild = operation.IsCompleted();
    secondExecutor.RunUntilIdle();
    runtime.Shutdown();
    REQUIRE_FALSE(completedBeforeSecondChild);
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(std::get<0>(result.Value()) == 21);
    REQUIRE(std::get<1>(result.Value()) == 21);
}

TEST_CASE("WhenAny drains losing external work after runtime admission closes", "[IO][Runtime][Shutdown][WhenAny]")
{
    // Exactly the parent, two notifications, and two child task slots.
    NGIN::IO::Runtime                     runtime({.completionCapacity = 5, .batchSize = 1});
    NGIN::Execution::CooperativeScheduler firstExecutor;
    NGIN::Execution::CooperativeScheduler secondExecutor;
    NGIN::Async::TaskContext              context(runtime.GetExecutor());
    NGIN::Async::TaskContext              firstContext(firstExecutor);
    NGIN::Async::TaskContext              secondContext(secondExecutor);
    auto                                  external     = [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> { co_return 21; };
    auto                                  first        = NGIN::Async::Spawn(firstContext, external(firstContext));
    auto                                  second       = NGIN::Async::Spawn(secondContext, external(secondContext));
    bool                                  firstResumed = false;
    bool                                  affinity     = false;
    int                                   destroyed    = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    auto operation = NGIN::Async::Spawn(context, NGIN::Async::WhenAny(context, [&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<int> {
            Guard guard {destroyed};
            auto result = co_await first;
            firstResumed = true;
            affinity = child.GetExecutor().IsCurrent();
            co_return result.Value(); }, [&](NGIN::Async::TaskContext&) -> NGIN::Async::Task<int> {
            Guard guard {destroyed};
            auto result = co_await second;
            co_return result; }));
    while (runtime.PollOnce()) {}
    runtime.RequestStop();
    firstExecutor.RunUntilIdle();
    const bool ranOnExternal = firstResumed;
    while (runtime.PollOnce()) {}
    const bool completedBeforeLoser = operation.IsCompleted();
    const bool stillStopping        = runtime.GetState() == NGIN::IO::Runtime::State::Stopping;
    secondExecutor.RunUntilIdle();
    const bool completedOutsideRuntime = operation.IsCompleted();
    runtime.Shutdown();
    REQUIRE_FALSE(ranOnExternal);
    REQUIRE(firstResumed);
    REQUIRE(affinity);
    REQUIRE_FALSE(completedBeforeLoser);
    REQUIRE(stillStopping);
    REQUIRE_FALSE(completedOutsideRuntime);
    REQUIRE(operation.IsCompleted());
    REQUIRE(destroyed == 2);
    REQUIRE(operation.TakeResult().Value() == 0);
}
