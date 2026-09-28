#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <stdexcept>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

namespace
{
    NGIN::Async::Task<int> Chain(int depth, int& entered)
    {
        ++entered;
        if (depth > 1)
            co_return 1 + co_await Chain(depth - 1, entered);
        co_return 1;
    }

    NGIN::Async::Task<void> VoidChain(int depth, int& entered)
    {
        ++entered;
        if (depth > 1)
            co_await VoidChain(depth - 1, entered);
    }

    NGIN::Async::Task<int> FailedChain(int depth, int& entered)
    {
        ++entered;
        if (depth > 1)
            co_return co_await FailedChain(depth - 1, entered);
        co_return NGIN::Async::Completion<int, NGIN::Async::NoError>::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
    }
}// namespace

TEST_CASE("Sequential children reuse one admitted completion slot", "[Async][Sequential]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    int                                   entered = 0;
    auto                                  task    = Chain(64, entered);
    REQUIRE(entered == 0);
    auto operationTask = std::move(task);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value() == 64);
    REQUIRE(entered == 64);
}

TEST_CASE("Sequential void children reuse one admitted completion slot", "[Async][Sequential]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    int                                   entered       = 0;
    auto                                  operationTask = VoidChain(64, entered);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(entered == 64);
}

TEST_CASE("Long direct chains defer so unrelated work progresses", "[Async][Sequential][Fairness]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    int                                   entered       = 0;
    int                                   observed      = 0;
    auto                                  operationTask = Chain(10000, entered);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    REQUIRE(executor.RunOne());
    REQUIRE(executor.Execute(NGIN::Execution::WorkItem([&] { observed = entered; })));
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 10000);
    REQUIRE(observed > 0);
    REQUIRE(observed < 10000);
}

TEST_CASE("Sequential child failure remains observable with one completion slot", "[Async][Sequential]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const int                             depth         = GENERATE(64, 10000);
    int                                   entered       = 0;
    auto                                  operationTask = FailedChain(depth, entered);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure);
    REQUIRE(entered == depth);
}

namespace
{
    struct ThrowingMove
    {
        bool* bodyCompleted;
        explicit ThrowingMove(bool& completed) noexcept : bodyCompleted(&completed) {}
        ThrowingMove(ThrowingMove&& other) : bodyCompleted(other.bodyCompleted)
        {
            if (*bodyCompleted)
                throw std::runtime_error("move after child final suspension");
        }
        ThrowingMove(const ThrowingMove&) = delete;
    };

    NGIN::Async::Task<ThrowingMove> MovingChild(bool& completed)
    {
        struct OnReturn
        {
            bool& completed;
            ~OnReturn() { completed = true; }
        } onReturn {completed};
        co_return ThrowingMove(completed);
    }

    NGIN::Async::Task<void> MovingParent(bool& completed)
    {
        [[maybe_unused]] auto value = co_await MovingChild(completed);
    }
}// namespace

TEST_CASE("A throwing sequential result move becomes an observable parent fault", "[Async][Sequential]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    bool                                  completed     = false;
    auto                                  operationTask = MovingParent(completed);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(completed);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnhandledException);
}

namespace
{
    struct HoldFailedFrame
    {
        NGIN::Execution::WorkItem& release;
        bool                       await_ready() const noexcept { return false; }
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle)
        {
            handle.promise().RetainFrameReference();
            release = NGIN::Execution::WorkItem([handle] { handle.promise().ReleaseFrameReference(handle); });
            return false;
        }
        void await_resume() const noexcept {}
    };

    NGIN::Async::Task<void> VoidFailure()
    {
        co_await NGIN::Async::Faulted(
                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
    }

    NGIN::Async::Task<void> LeasedFailure(NGIN::Execution::WorkItem& release, bool& parentAlive,
                                          bool& childRetired, bool& parentObserved)
    {
        struct Guard
        {
            bool& parentAlive;
            bool& retired;
            bool& observed;
            ~Guard()
            {
                observed = parentAlive;
                retired  = true;
            }
        } guard {parentAlive, childRetired, parentObserved};
        co_await HoldFailedFrame {release};
        co_await VoidFailure();
    }

    NGIN::Async::Task<void> BorrowingParent(NGIN::Execution::WorkItem& release, bool& parentAlive,
                                            bool& childRetired, bool& parentObserved)
    {
        struct Guard
        {
            bool& alive;
            explicit Guard(bool& value) : alive(value) { alive = true; }
            ~Guard() { alive = false; }
        } guard {parentAlive};
        co_await LeasedFailure(release, parentAlive, childRetired, parentObserved);
    }
}// namespace

TEST_CASE("Sequential failure waits for child frame borrowers before completing its parent", "[Async][Sequential][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    NGIN::Execution::WorkItem             release;
    bool                                  parentAlive    = false;
    bool                                  childRetired   = false;
    bool                                  parentObserved = false;
    auto                                  operationTask  = BorrowingParent(release, parentAlive, childRetired, parentObserved);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    const bool completedBeforeRelease = operation.IsCompleted();
    const bool retiredBeforeRelease   = childRetired;
    release.Invoke();
    release                             = {};
    const bool completedOutsideExecutor = operation.IsCompleted();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeRelease);
    REQUIRE_FALSE(retiredBeforeRelease);
    REQUIRE_FALSE(completedOutsideExecutor);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Error().IsFault());
    operation = {};
    REQUIRE(childRetired);
    REQUIRE(parentObserved);
    REQUIRE_FALSE(parentAlive);
}

TEST_CASE("Awaiting an lvalue task consumes it once", "[Async][Sequential][Consumption]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const bool                            useVoid           = GENERATE(false, true);
    const bool                            cancellationAware = GENERATE(false, true);
    const bool                            cancelSecond      = GENERATE(false, true);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              waitContext(executor, cancellation.GetToken());
    int                                   entered         = 0;
    bool                                  firstCompleted  = false;
    bool                                  secondCompleted = false;
    auto                                  parent          = [&]<typename T>(NGIN::Async::Task<T>& child) -> NGIN::Async::Task<void> {
        if (cancellationAware)
            (void) co_await child.WithCancellation(waitContext);
        else
            (void) co_await child;
        firstCompleted = true;
        if (cancelSecond)
            cancellation.Cancel();
        if (cancellationAware)
            (void) co_await child.WithCancellation(waitContext);
        else
            (void) co_await child;
        secondCompleted = true;
    };
    auto check = [&]<typename T>(NGIN::Async::Task<T> child) {
        auto operationTask = parent(child);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

        auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
        executor.RunUntilIdle();
        rootExecutor.RunUntilIdle();
        REQUIRE(firstCompleted);
        REQUIRE_FALSE(secondCompleted);
        REQUIRE(entered == 1);
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE((result.HasError() && result.Error().IsFault()));
        REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
    };
    if (useVoid)
        check(VoidChain(1, entered));
    else
        check(Chain(1, entered));
}

TEST_CASE("Consuming a pending task again cannot replace its first parent", "[Async][Sequential][Consumption]")
{
    NGIN::Execution::CooperativeScheduler executor;
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const bool                            useVoid           = GENERATE(false, true);
    const bool                            cancellationAware = GENERATE(false, true);
    int                                   entered           = 0;
    int                                   completed         = 0;
    auto                                  makeChild         = [&]<typename T>() -> NGIN::Async::Task<T> {
        ++entered;
        co_await context.Delay(NGIN::Units::Seconds(1));
        if constexpr (!std::is_void_v<T>)
            co_return 42;
    };
    auto parent = [&]<typename T>(NGIN::Async::Task<T>& child) -> NGIN::Async::Task<void> {
        if (cancellationAware)
            (void) co_await child.WithCancellation(context);
        else
            (void) co_await child;
        ++completed;
    };
    auto check = [&]<typename T>(NGIN::Async::Task<T> child) {
        auto firstTask = parent(child);

        NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(rootContext.GetEnvironment(), 1);

        auto first = firstRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(firstTask)).value();
        executor.RunUntilIdle();
        rootExecutor.RunUntilIdle();
        auto secondTask = parent(child);

        NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(rootContext.GetEnvironment(), 1);

        auto second = secondRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(secondTask)).value();
        executor.RunUntilIdle();
        rootExecutor.RunUntilIdle();
        const bool firstPending   = !first.IsCompleted();
        const bool secondFinished = second.IsCompleted();
        executor.RunUntilIdleAt(NGIN::Time::TimePoint::FromNanoseconds(
                NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 2'000'000'000ULL));
        rootExecutor.RunUntilIdle();
        REQUIRE(firstPending);
        REQUIRE(secondFinished);
        REQUIRE(first.IsCompleted());
        REQUIRE(firstRoot.TryJoin());
        REQUIRE(first.TakeResult().Succeeded());
        REQUIRE(secondRoot.TryJoin());
        auto result = second.TakeResult();
        REQUIRE((result.HasError() && result.Error().IsFault()));
        REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
        REQUIRE(entered == 1);
        REQUIRE(completed == 1);
    };
    if (useVoid)
        check(makeChild.template operator()<void>());
    else
        check(makeChild.template operator()<int>());
}

namespace
{
    struct BorrowedFrameParameter
    {
        bool* parentAlive;
        bool* retired;
        bool* observed;
        BorrowedFrameParameter(bool& alive, bool& done, bool& seen)
            : parentAlive(&alive), retired(&done), observed(&seen) {}
        BorrowedFrameParameter(BorrowedFrameParameter&& other) noexcept
            : parentAlive(std::exchange(other.parentAlive, nullptr)), retired(other.retired), observed(other.observed) {}
        ~BorrowedFrameParameter()
        {
            if (parentAlive)
            {
                *observed = *parentAlive;
                *retired  = true;
            }
        }
    };

    template<typename T>
    NGIN::Async::Task<T> LeasedSuccess(NGIN::Execution::WorkItem& release, BorrowedFrameParameter)
    {
        co_await HoldFailedFrame {release};
        if constexpr (!std::is_void_v<T>)
            co_return 42;
    }

    template<typename T>
    NGIN::Async::Task<int> BorrowingSuccessfulParent(NGIN::Execution::WorkItem& release, bool& parentAlive,
                                                     bool& childRetired, bool& parentObserved)
    {
        struct Guard
        {
            bool& alive;
            explicit Guard(bool& value) : alive(value) { alive = true; }
            ~Guard() { alive = false; }
        } guard {parentAlive};
        if constexpr (std::is_void_v<T>)
        {
            co_await LeasedSuccess<T>(release, BorrowedFrameParameter(parentAlive, childRetired, parentObserved));
            co_return 42;
        }
        else
            co_return co_await LeasedSuccess<T>(release, BorrowedFrameParameter(parentAlive, childRetired, parentObserved));
    }
}// namespace

TEST_CASE("Sequential success waits for frame borrowers before resuming its parent", "[Async][Sequential][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    NGIN::Execution::WorkItem             release;
    bool                                  parentAlive    = false;
    bool                                  childRetired   = false;
    bool                                  parentObserved = false;
    const bool                            useVoid        = GENERATE(false, true);
    auto                                  task           = useVoid ? BorrowingSuccessfulParent<void>(release, parentAlive, childRetired, parentObserved)
                                                                   : BorrowingSuccessfulParent<int>(release, parentAlive, childRetired, parentObserved);
    auto                                  operationTask  = std::move(task);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    const bool completedBeforeRelease = operation.IsCompleted();
    const bool retiredBeforeRelease   = childRetired;
    release.Invoke();
    release                             = {};
    const bool completedOutsideExecutor = operation.IsCompleted();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeRelease);
    REQUIRE_FALSE(retiredBeforeRelease);
    REQUIRE_FALSE(completedOutsideExecutor);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
    REQUIRE(childRetired);
    REQUIRE(parentObserved);
    REQUIRE_FALSE(parentAlive);
}
