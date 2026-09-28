#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <memory>

namespace
{
    template<typename T>
    NGIN::Async::Completion<T, std::unique_ptr<int>> Outcome(int channel)
    {
        using C = NGIN::Async::Completion<T, std::unique_ptr<int>>;
        if (channel == 1)
            return C::DomainFailure(std::make_unique<int>(7));
        if (channel == 2)
            return C::Stopped();
        if (channel == 3)
            return C::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
        if constexpr (std::is_void_v<T>)
            return C::Success();
        else
            return C::Success(42);
    }

    template<typename T>
    NGIN::Async::Task<T, std::unique_ptr<int>> Child(int channel)
    {
        auto result = Outcome<T>(channel);
        if constexpr (std::is_void_v<T>)
        {
            if ((result.HasError() && result.Error().IsDomainError()))
                co_await NGIN::Async::DomainFailure(std::move(result).Error().DomainError());
            if (result.IsStopped())
                co_await NGIN::Async::Stopped();
            if ((result.HasError() && result.Error().IsFault()))
                co_await NGIN::Async::Faulted(result.Error().Fault());
        }
        else
            co_return result;
    }
}// namespace

TEST_CASE("AsCompletion returns every terminal outcome without failing its parent", "[Async][CompletionAwait]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    const int                             channel  = GENERATE(0, 1, 2, 3);
    const bool                            ready    = GENERATE(false, true);
    const bool                            useVoid  = GENERATE(false, true);
    bool                                  observed = false;
    auto                                  parent   = [&]<typename T>() -> NGIN::Async::Task<int> {
        using Task  = NGIN::Async::Task<T, std::unique_ptr<int>>;
        auto child  = ready ? Task::FromCompletion(Outcome<T>(channel)) : Child<T>(channel);
        auto result = co_await child.AsCompletion();
        if (channel == 0)
        {
            REQUIRE(result.Succeeded());
            if constexpr (!std::is_void_v<T>)
                REQUIRE(result.Value() == 42);
        }
        else if (channel == 1)
            REQUIRE(*result.Error().DomainError() == 7);
        else if (channel == 2)
            REQUIRE(result.IsStopped());
        else
            REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure);
        auto repeated = co_await child.AsCompletion();
        REQUIRE(repeated.Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
        observed = true;
        co_return 99;
    };
    auto operationTask = useVoid ? parent.template operator()<void>() : parent.template operator()<int>();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 99);
    REQUIRE(observed);
}

namespace
{
    struct FrameProbe
    {
        bool* retired;
        explicit FrameProbe(bool& value) : retired(&value) {}
        FrameProbe(FrameProbe&& other) noexcept : retired(std::exchange(other.retired, nullptr)) {}
        ~FrameProbe()
        {
            if (retired)
                *retired = true;
        }
    };
    struct HoldFrame
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
    NGIN::Async::Task<int, std::unique_ptr<int>> LeasedChild(int channel, NGIN::Execution::WorkItem& release, FrameProbe)
    {
        co_await HoldFrame {release};
        co_return Outcome<int>(channel);
    }
}// namespace

TEST_CASE("AsCompletion waits for frame retirement before returning any outcome", "[Async][CompletionAwait][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler executor(1);
    NGIN::Async::TaskContext              context(executor);
    NGIN::Execution::CooperativeScheduler rootExecutor;
    NGIN::Async::TaskContext              rootContext(rootExecutor);
    NGIN::Execution::WorkItem             release;
    const int                             channel            = GENERATE(0, 1, 2, 3);
    bool                                  retired            = false;
    bool                                  observedRetirement = false;
    auto                                  parent             = [&]() -> NGIN::Async::Task<int> {
        auto result        = co_await LeasedChild(channel, release, FrameProbe(retired)).AsCompletion();
        observedRetirement = retired;
        if (channel == 0)
            REQUIRE(result.Value() == 42);
        else if (channel == 1)
            REQUIRE(*result.Error().DomainError() == 7);
        else if (channel == 2)
            REQUIRE(result.IsStopped());
        else
            REQUIRE((result.HasError() && result.Error().IsFault()));
        co_return 99;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(executor), std::move(operationTask)).value();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    const bool completedBeforeRelease = operation.IsCompleted();
    release.Invoke();
    release                             = {};
    const bool completedOutsideExecutor = operation.IsCompleted();
    executor.RunUntilIdle();
    rootExecutor.RunUntilIdle();
    REQUIRE_FALSE(completedBeforeRelease);
    REQUIRE_FALSE(completedOutsideExecutor);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 99);
    REQUIRE(retired);
    REQUIRE(observedRetirement);
}
