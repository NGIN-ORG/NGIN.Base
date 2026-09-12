#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>

#include <memory>
#include <stdexcept>

namespace
{
    template<typename E>
    NGIN::Async::Task<bool, E> JoinScope(NGIN::Async::TaskContext&, NGIN::Async::TaskScope<E>& scope)
    {
        const auto& result = co_await scope.Join();
        co_return result.Succeeded();
    }
}// namespace

TEST_CASE("TaskScope joins child values and releases owned coroutine factories", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler executor;
    auto                                  exec = NGIN::Execution::ExecutorRef::From(executor);
    NGIN::Async::TaskContext              context(exec);
    NGIN::Async::TaskScope                scope(exec);
    auto                                  owner    = std::make_shared<int>(42);
    std::weak_ptr<int>                    lifetime = owner;
    int                                   observed = 0;
    REQUIRE(scope.Spawn([owner = std::move(owner), &observed](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<int> {
        co_await child.YieldNow();
        observed = *owner;
        co_return observed;
    }));
    REQUIRE_FALSE(lifetime.expired());
    auto joined = NGIN::Async::Spawn(context, JoinScope(context, scope));
    executor.RunUntilIdle();
    REQUIRE(joined.IsCompleted());
    REQUIRE(joined.TakeResult().Value());
    REQUIRE(scope.Pending() == 0);
    REQUIRE(observed == 42);
    REQUIRE(lifetime.expired());
    REQUIRE(scope.TakeResult().Succeeded());
    REQUIRE_THROWS_AS(scope.TakeResult(), std::logic_error);
}

TEST_CASE("TaskScope records the first failure and joins canceled siblings", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler executor;
    auto                                  exec = NGIN::Execution::ExecutorRef::From(executor);
    NGIN::Async::TaskContext              context(exec);
    NGIN::Async::TaskScope<int>           scope(exec);
    int                                   destroyed = 0;
    struct Guard
    {
        int& destroyed;
        ~Guard() { ++destroyed; }
    };
    REQUIRE(scope.Spawn([&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void, int> {
        Guard guard {destroyed};
        co_await child.Delay(NGIN::Units::Seconds(60));
        co_return;
    }));
    REQUIRE(scope.Spawn([](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void, int> {
        co_await NGIN::Async::DomainFailure(23);
    }));
    auto joined = NGIN::Async::Spawn(context, JoinScope(context, scope));
    executor.RunUntilIdle();
    REQUIRE(joined.IsCompleted());
    REQUIRE_FALSE(joined.TakeResult().Value());
    REQUIRE(scope.GetCancellationToken().IsCancellationRequested());
    REQUIRE(destroyed == 1);
    auto result = scope.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError() == 23);
}

TEST_CASE("TaskScope rejects capacity before invoking another child factory", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler executor;
    auto                                  exec = NGIN::Execution::ExecutorRef::From(executor);
    NGIN::Async::TaskContext              context(exec);
    NGIN::Async::TaskScope                scope(exec, {}, 1);
    REQUIRE(scope.Spawn([](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> { co_return; }));
    bool invoked  = false;
    auto rejected = scope.Spawn([&](NGIN::Async::TaskContext& child) {
        invoked = true;
        return [](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> { co_return; }(child);
    });
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    REQUIRE_FALSE(invoked);
    auto joined = NGIN::Async::Spawn(context, JoinScope(context, scope));
    executor.RunUntilIdle();
    REQUIRE(joined.TakeResult().Value());
    auto closed = scope.Spawn([](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> { co_return; });
    REQUIRE_FALSE(closed);
    REQUIRE(closed.error() == NGIN::Execution::ScheduleError::Stopped);
}

TEST_CASE("TaskScope reports a throwing factory and joins existing children", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler executor;
    auto                                  exec = NGIN::Execution::ExecutorRef::From(executor);
    NGIN::Async::TaskContext              context(exec);
    NGIN::Async::TaskScope                scope(exec);
    REQUIRE(scope.Spawn([](NGIN::Async::TaskContext&) -> NGIN::Async::Task<void> {
        throw std::runtime_error("factory failure");
        // This function deliberately is not a coroutine.
    }));
    auto joined = NGIN::Async::Spawn(context, JoinScope(context, scope));
    executor.RunUntilIdle();
    REQUIRE_FALSE(joined.TakeResult().Value());
    auto result = scope.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == NGIN::Async::AsyncFaultCode::UnhandledException);
}

TEST_CASE("TaskScope can join independently executed children more than once", "[Async][Scope]")
{
    NGIN::Execution::CooperativeScheduler owner;
    NGIN::Execution::ThreadPoolScheduler  worker(1);
    auto                                  exec = NGIN::Execution::ExecutorRef::From(owner);
    NGIN::Async::TaskContext              context(exec);
    NGIN::Async::TaskScope                scope(exec);
    REQUIRE(scope.SpawnOn(NGIN::Execution::ExecutorRef::From(worker),
                          [](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void> {
                              co_await child.YieldNow();
                          }));
    auto joined = NGIN::Async::Spawn(context, JoinScope(context, scope));
    while (!joined.IsCompleted())
    {
        owner.RunUntilIdle();
        std::this_thread::yield();
    }
    REQUIRE(joined.TakeResult().Value());
    auto again = NGIN::Async::Spawn(context, JoinScope(context, scope));
    owner.RunUntilIdle();
    REQUIRE(again.TakeResult().Value());
    REQUIRE(scope.TakeResult().Succeeded());
}

TEST_CASE("TaskScope joining waits for the last canceled child frame lease", "[Async][Scope][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler executor;
    NGIN::Async::TaskContext              context(executor);
    NGIN::Async::TaskScope                scope(context.GetExecutor());
    NGIN::Execution::WorkItem             release;
    int                                   destroyed = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    struct HoldFrame
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        bool                       await_suspend(std::coroutine_handle<NGIN::Async::Task<void>::promise_type> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };
    REQUIRE(scope.Spawn([&](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void> {
        Guard guard {destroyed};
        co_await HoldFrame {release};
        scope.RequestCancel();
        co_await child.YieldNow();
    }));
    auto joining = NGIN::Async::Spawn(context, JoinScope(context, scope));
    executor.RunUntilIdle();
    const bool completedWithLease = joining.IsCompleted();
    const int  destroyedWithLease = destroyed;
    release.Invoke();
    release = {};
    const bool completedOutsideExecutor = joining.IsCompleted();
    executor.RunUntilIdle();
    REQUIRE_FALSE(completedWithLease);
    REQUIRE(destroyedWithLease == 0);
    REQUIRE_FALSE(completedOutsideExecutor);
    REQUIRE(joining.IsCompleted());
    REQUIRE(destroyed == 1);
    REQUIRE(scope.TakeResult().IsCanceled());
}
