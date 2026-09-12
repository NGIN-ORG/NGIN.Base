#include <catch2/catch_test_macros.hpp>

#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/RunTask.hpp>

#include <stdexcept>
#include <thread>

namespace
{
    NGIN::Async::Task<void, int> PropagatedRootFailure(NGIN::Async::TaskContext&)
    {
        co_await NGIN::Async::DomainFailure(23);
    }
}// namespace

TEST_CASE("RunTask returns a root value and completes runtime shutdown", "[IO][Runtime][Root]")
{
    NGIN::IO::Runtime runtime;
    auto              owner    = std::this_thread::get_id();
    bool              affinity = false;
    auto              result   = NGIN::IO::RunTask(runtime, [&](NGIN::Async::TaskContext& ctx, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<int> {
        co_await ctx.YieldNow();
        co_await ctx.Delay(NGIN::Units::Milliseconds(1));
        affinity = std::this_thread::get_id() == owner && runtime.GetExecutor().IsCurrent();
        co_return 42;
    });
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value() == 42);
    REQUIRE(affinity);
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("RunTask cancels and joins children before returning a root failure", "[IO][Runtime][Root]")
{
    NGIN::IO::Runtime runtime;
    int               childDestroyed = 0;
    int               rootDestroyed  = 0;
    bool              order          = false;
    struct RootGuard
    {
        int& count;
        ~RootGuard() { ++count; }
    };
    struct ChildGuard
    {
        int&  count;
        int&  root;
        bool& order;
        ~ChildGuard()
        {
            ++count;
            order = root == 0;
        }
    };
    auto result = NGIN::IO::RunTask<int>(runtime, [&](NGIN::Async::TaskContext& ctx, NGIN::Async::TaskScope<int>& scope) -> NGIN::Async::Task<int, int> {
        RootGuard  guard {rootDestroyed};
        const auto started = scope.Spawn([&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void, int> {
            ChildGuard guard {childDestroyed, rootDestroyed, order};
            co_await child.Delay(NGIN::Units::Seconds(60));
        });
        if (!started)
            co_await NGIN::Async::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed));
        co_await ctx.YieldNow();
        co_await PropagatedRootFailure(ctx);
        co_return 0;
    });
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError() == 23);
    REQUIRE(childDestroyed == 1);
    REQUIRE(rootDestroyed == 1);
    REQUIRE(order);
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("RunTask preserves a child failure when the root succeeds", "[IO][Runtime][Root]")
{
    NGIN::IO::Runtime runtime;
    auto              result = NGIN::IO::RunTask<int>(runtime, [](NGIN::Async::TaskContext&, NGIN::Async::TaskScope<int>& scope) -> NGIN::Async::Task<int, int> {
        const auto started = scope.Spawn([](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void, int> {
            co_await NGIN::Async::DomainFailure(19);
        });
        if (!started)
            co_await NGIN::Async::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed));
        (void) co_await scope.Join();
        co_return 42;
    });
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError() == 19);
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("RunTask joins external children after stop without new admission", "[IO][Runtime][Root]")
{
    NGIN::IO::Runtime                    runtime;
    NGIN::Execution::ThreadPoolScheduler worker(1);
    int                                  destroyed = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    auto result = NGIN::IO::RunTask(runtime, [&](NGIN::Async::TaskContext&, NGIN::Async::TaskScope<>& scope) -> NGIN::Async::Task<void> {
        const auto started = scope.SpawnOn(NGIN::Execution::ExecutorRef::From(worker),
                                           [&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void> {
                                               Guard guard {destroyed};
                                               co_await child.YieldNow();
                                           });
        if (!started)
            co_await NGIN::Async::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed));
        runtime.RequestStop();
        (void) co_await scope.Join();
        co_return;
    });
    REQUIRE(result.Succeeded());
    REQUIRE(destroyed == 1);
    REQUIRE(runtime.IsStopped());
}

TEST_CASE("RunTask converts factory exceptions and admission exhaustion into root faults", "[IO][Runtime][Root]")
{
    SECTION("throwing factory")
    {
        NGIN::IO::Runtime runtime;
        auto              result = NGIN::IO::RunTask(runtime, [](NGIN::Async::TaskContext&, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<void> {
            throw std::runtime_error("root factory failed");
        });
        REQUIRE(result.IsFault());
        REQUIRE(runtime.IsStopped());
    }
    SECTION("root admission exhaustion")
    {
        NGIN::IO::Runtime runtime({.completionCapacity = 1});
        bool              entered = false;
        auto              result  = NGIN::IO::RunTask(runtime, [&](NGIN::Async::TaskContext&, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<void> { entered = true; co_return; });
        REQUIRE(result.IsFault());
        REQUIRE_FALSE(entered);
        REQUIRE(result.Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
        REQUIRE(runtime.IsStopped());
    }
}

TEST_CASE("RunTask returns cancellation and does not take over another runtime owner", "[IO][Runtime][Root]")
{
    SECTION("already canceled root")
    {
        NGIN::IO::Runtime               runtime;
        NGIN::Async::CancellationSource cancellation;
        cancellation.Cancel();
        auto result = NGIN::IO::RunTask(runtime, [](NGIN::Async::TaskContext& ctx, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<void> { co_await ctx.YieldNow(); }, cancellation.GetToken());
        REQUIRE(result.IsCanceled());
        REQUIRE(runtime.IsStopped());
    }
    SECTION("different owner")
    {
        NGIN::IO::Runtime runtime;
        (void) runtime.PollOnce();
        bool        rejected = false;
        std::thread other([&] {
            try
            {
                (void) NGIN::IO::RunTask(runtime, [](NGIN::Async::TaskContext&, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<void> { co_return; });
            } catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
        other.join();
        REQUIRE(rejected);
        REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Running);
        runtime.Shutdown();
    }
}
