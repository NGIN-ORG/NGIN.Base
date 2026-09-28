#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    using namespace NGIN::Async;
    Task<int> Count(int& started, std::shared_ptr<int> lifetime)
    {
        ++started;
        co_return *lifetime;
    }
    struct ThrowValue
    {
        bool* fail {};
        explicit ThrowValue(bool* fail = nullptr) : fail(fail) {}
        ThrowValue(ThrowValue&& other) : fail(other.fail)
        {
            if (fail && *fail)
                throw std::runtime_error("result move");
        }
        ThrowValue& operator=(ThrowValue&&) = default;
    };
    struct OnRetirement
    {
        bool* flag;
        explicit OnRetirement(bool& flag) : flag(&flag) {}
        OnRetirement(OnRetirement&& other) noexcept : flag(std::exchange(other.flag, nullptr)) {}
        ~OnRetirement()
        {
            if (flag)
                *flag = true;
        }
    };
    Task<ThrowValue> Produced(OnRetirement, bool& fail)
    {
        co_return ThrowValue(&fail);
    }
    Task<ThrowValue> Waiting(bool& retired)
    {
        OnRetirement guard(retired);
        TaskContext  context(co_await CurrentEnvironment());
        co_await context.Delay(NGIN::Units::Seconds(60));
        co_return ThrowValue();
    }
}// namespace

TEST_CASE("WhenAll joins prior admissions when a later child is rejected", "[Async][Scope][WhenAll]")
{
    NGIN::Execution::CooperativeScheduler scheduler(3);
    TaskContext                           context(scheduler);
    int                                   first = 0, second = 0;
    auto                                  lifetime      = std::make_shared<int>(42);
    std::weak_ptr<int>                    weak          = lifetime;
    auto                                  operationTask = WhenAll(context, Count(first, lifetime), Count(second, lifetime));

    NGIN::Execution::CooperativeScheduler                       rootScheduler;
    TaskContext                                                 rootContext(rootScheduler);
    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(NGIN::Execution::ExecutorRef::From(scheduler), std::move(operationTask)).value();
    lifetime.reset();
    scheduler.RunUntilIdle();
    rootScheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(first == 1);
    REQUIRE(second == 0);
    REQUIRE(weak.expired());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError());
    REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
}

TEST_CASE("WhenAll errors outrank earlier stopped children", "[Async][Scope][WhenAll]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    using E            = std::unique_ptr<int>;
    auto stopped       = Task<int, E>::FromCompletion(Completion<int, E>::Stopped(StopReason::Deadline));
    auto failed        = Task<int, E>::FromCompletion(Completion<int, E>::DomainFailure(std::make_unique<int>(23)));
    auto operationTask = WhenAll(context, std::move(stopped), std::move(failed));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError());
    REQUIRE(*result.Error().DomainError() == 23);
}

TEST_CASE("WhenAll joins outstanding children when consuming a value throws", "[Async][Scope][WhenAll][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    bool                                  fail = false, retired = false;
    auto                                  operationTask = WhenAll(context, Produced(OnRetirement(fail), fail), Waiting(retired));

    TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(fail);
    REQUIRE(retired);
    REQUIRE(scheduler.PendingTimers() == 0);
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.HasError());
    REQUIRE(result.Error().Fault().code == AsyncFaultCode::UnhandledException);
}
