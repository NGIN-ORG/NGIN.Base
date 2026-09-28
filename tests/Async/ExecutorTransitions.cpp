#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <memory_resource>
#include <stdexcept>

namespace
{
    NGIN::Async::Task<int, int> Inner(int channel, bool& entered, NGIN::Execution::ExecutorRef expected,
                                      const void* metadata, NGIN::Execution::ExecutorRef intermediate)
    {
        entered = true;
        REQUIRE(expected.IsCurrent());
        auto environment = co_await NGIN::Async::CurrentEnvironment();
        REQUIRE(environment.metadata == metadata);
        REQUIRE(environment.priority == 3);
        if (intermediate.IsValid())
            co_await NGIN::Async::SwitchTo(intermediate);
        co_await NGIN::Async::YieldNow();
        using C = NGIN::Async::Completion<int, int>;
        if (channel == 1)
            co_return C::DomainFailure(7);
        if (channel == 2)
            co_return C::Stopped();
        if (channel == 3)
            co_return C::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
        co_return 42;
    }
}// namespace

TEST_CASE("Executor transitions deliver every outcome without restoring the original executor", "[Async][Transitions]")
{
    NGIN::Execution::CooperativeScheduler first(4), target(4), middle(4);
    auto                                  firstRef     = NGIN::Execution::ExecutorRef::From(first);
    auto                                  targetRef    = NGIN::Execution::ExecutorRef::From(target);
    auto                                  middleRef    = NGIN::Execution::ExecutorRef::From(middle);
    const int                             channel      = GENERATE(0, 1, 2, 3);
    const bool                            terminal     = GENERATE(false, true);
    const bool                            switchInside = GENERATE(false, true);
    int                                   metadata     = 42;
    std::pmr::monotonic_buffer_resource   resource;
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::CancellationToken        observedToken;
    const auto                            deadline = NGIN::Time::TimePoint::FromNanoseconds(123456789);
    NGIN::Async::TaskContext              context(NGIN::Async::AsyncEnvironment {
                         .executor     = firstRef,
                         .cancellation = cancellation.GetToken(),
                         .deadline     = deadline,
                         .resource     = &resource,
                         .metadata     = &metadata,
                         .priority     = 3});
    bool                                  entered   = false;
    bool                                  delivered = false;
    auto                                  parent    = [&]() -> NGIN::Async::Task<void> {
        auto child        = Inner(channel, entered, terminal ? firstRef : targetRef, &metadata,
                           switchInside ? middleRef : NGIN::Execution::ExecutorRef {});
        auto transitioned = terminal ? NGIN::Async::ContinuesOn(targetRef, std::move(child))
                                                                         : NGIN::Async::StartsOn(targetRef, std::move(child));
        auto result       = co_await transitioned.AsCompletion();
        auto expected     = terminal || !switchInside ? targetRef : middleRef;
        REQUIRE(expected.IsCurrent());
        const auto environment = co_await NGIN::Async::CurrentEnvironment();
        REQUIRE(environment.executor == expected);
        REQUIRE(environment.metadata == &metadata);
        REQUIRE(environment.priority == 3);
        REQUIRE(environment.deadline == deadline);
        REQUIRE(environment.resource == &resource);
        observedToken = environment.cancellation;
        if (channel == 0)
            REQUIRE(result.Value() == 42);
        else if (channel == 1)
            REQUIRE(result.Error().DomainError() == 7);
        else if (channel == 2)
            REQUIRE(result.IsStopped());
        else
            REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure);
        delivered = true;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    for (int turn = 0; turn != 10 && !operation.IsCompleted(); ++turn)
    {
        first.RunUntilIdle();
        middle.RunUntilIdle();
        target.RunUntilIdle();
    }
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(entered);
    REQUIRE(delivered);
    cancellation.Cancel();
    REQUIRE(observedToken.IsCancellationRequested());
}

TEST_CASE("Rejected transition admission never enters the child", "[Async][Transitions]")
{
    NGIN::Execution::CooperativeScheduler first, target(1);
    auto                                  firstRef  = NGIN::Execution::ExecutorRef::From(first);
    auto                                  targetRef = NGIN::Execution::ExecutorRef::From(target);
    auto                                  occupied  = targetRef.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE(occupied);
    NGIN::Async::TaskContext context(firstRef);
    const bool               terminal      = GENERATE(false, true);
    bool                     entered       = false;
    auto                     child         = Inner(0, entered, targetRef, nullptr, {});
    auto                     transitioned  = terminal ? NGIN::Async::ContinuesOn(targetRef, std::move(child))
                                                      : NGIN::Async::StartsOn(targetRef, std::move(child));
    auto                     operationTask = std::move(transitioned);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    first.RunUntilIdle();
    target.RunUntilIdle();
    REQUIRE_FALSE(entered);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
}

TEST_CASE("Ready void transitions preserve all terminal outcomes", "[Async][Transitions]")
{
    NGIN::Execution::CooperativeScheduler first(2), target(2);
    NGIN::Async::TaskContext              context(first);
    auto                                  targetRef = NGIN::Execution::ExecutorRef::From(target);
    const int                             channel   = GENERATE(0, 1, 2, 3);
    const bool                            terminal  = GENERATE(false, true);
    auto                                  parent    = [&]() -> NGIN::Async::Task<int> {
        using C         = NGIN::Async::Completion<void, int>;
        auto completion = channel == 0 ? C::Success() : channel == 1 ? C::DomainFailure(7)
                                                                                    : channel == 2       ? C::Stopped()
                                                                                                         : C::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
        auto child      = NGIN::Async::Task<void, int>::FromCompletion(std::move(completion));
        auto task       = terminal ? NGIN::Async::ContinuesOn(targetRef, std::move(child))
                                                                       : NGIN::Async::StartsOn(targetRef, std::move(child));
        auto result     = co_await task.AsCompletion();
        REQUIRE(targetRef.IsCurrent());
        if (channel == 0)
            REQUIRE(result.Succeeded());
        else if (channel == 1)
            REQUIRE(result.Error().DomainError() == 7);
        else if (channel == 2)
            REQUIRE(result.IsStopped());
        else
            REQUIRE((result.HasError() && result.Error().IsFault()));
        co_return 42;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    first.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    target.RunUntilIdle();
    first.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

namespace
{
    struct AdmissionGate
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        bool                                  open = true;
        NGIN::Execution::ScheduleResult       Execute(NGIN::Execution::WorkItem work) noexcept
        {
            if (!open)
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            return scheduler.Execute(std::move(work));
        }
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint time) noexcept
        {
            if (!open)
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            return scheduler.ExecuteAt(std::move(work), time);
        }
        std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
        ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
        {
            if (!open)
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            return scheduler.ReserveCompletion(std::move(work));
        }
        bool IsCurrent() const noexcept { return scheduler.IsCurrent(); }
    };
}// namespace

TEST_CASE("ContinuesOn uses its admitted terminal slot after admission closes", "[Async][Transitions][Shutdown]")
{
    NGIN::Execution::CooperativeScheduler first;
    AdmissionGate                         target;
    auto                                  targetRef = NGIN::Execution::ExecutorRef::From(target);
    NGIN::Async::TaskContext              context(first);
    auto                                  child = [&]() -> NGIN::Async::Task<int> {
        target.open = false;
        co_await NGIN::Async::YieldNow();
        co_return 42;
    };
    auto parent = [&]() -> NGIN::Async::Task<int> {
        int value = co_await NGIN::Async::ContinuesOn(targetRef, child());
        REQUIRE(targetRef.IsCurrent());
        co_return value;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    first.RunUntilIdle();
    REQUIRE_FALSE(target.open);
    REQUIRE_FALSE(operation.IsCompleted());
    target.scheduler.RunUntilIdle();
    first.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Value() == 42);
}

TEST_CASE("Executor transitions retain placement across concurrent worker deliveries", "[Async][Transitions]")
{
    const int                            workers = GENERATE(1, 4);
    NGIN::Execution::ThreadPoolScheduler first(1), target(workers);
    const auto                           firstRef  = NGIN::Execution::ExecutorRef::From(first);
    const auto                           targetRef = NGIN::Execution::ExecutorRef::From(target);
    int                                  metadata  = 42;
    NGIN::Async::TaskContext             context(NGIN::Async::AsyncEnvironment {.executor = firstRef, .metadata = &metadata});
    for (int iteration = 0; iteration != 100; ++iteration)
    {
        const int channel = iteration % 4;
        bool      valid   = true;
        auto      child   = [&]() -> NGIN::Async::Task<int, int> {
            valid &= firstRef.IsCurrent();
            for (int hop = 0; hop != 32; ++hop)
            {
                const auto expected = hop % 2 ? firstRef : targetRef;
                co_await NGIN::Async::SwitchTo(expected);
                co_await NGIN::Async::YieldNow();
                auto environment = co_await NGIN::Async::CurrentEnvironment();
                valid &= expected.IsCurrent() && environment.executor == expected && environment.metadata == &metadata;
            }
            using C = NGIN::Async::Completion<int, int>;
            if (channel == 1)
                co_return C::DomainFailure(7);
            if (channel == 2)
                co_return C::Stopped();
            if (channel == 3)
                co_return C::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
            co_return 42;
        };
        auto parent = [&]() -> NGIN::Async::Task<bool> {
            auto result      = co_await NGIN::Async::ContinuesOn(targetRef, child()).AsCompletion();
            auto environment = co_await NGIN::Async::CurrentEnvironment();
            valid &= targetRef.IsCurrent() && environment.executor == targetRef && environment.metadata == &metadata;
            valid &= channel == 0   ? result.HasValue() && result.Value() == 42
                     : channel == 1 ? (result.HasError() && result.Error().IsDomainError()) && result.Error().DomainError() == 7
                     : channel == 2 ? result.IsStopped()
                                    : (result.HasError() && result.Error().IsFault());
            co_return valid;
        };
        auto result = NGIN::Async::SyncWait(context, parent());
        REQUIRE(result.HasValue());
        REQUIRE(result.Value());
    }
}

namespace
{
    struct TerminalMove final
    {
        bool* returned;
        explicit TerminalMove(bool& flag) : returned(&flag) {}
        TerminalMove(TerminalMove&& other) : returned(other.returned)
        {
            if (*returned)
                throw std::runtime_error("terminal result move");
        }
        TerminalMove(const TerminalMove&) = delete;
    };

    NGIN::Async::Task<TerminalMove> ThrowOnTerminalMove(bool& returned)
    {
        struct MarkReturn final
        {
            bool& flag;
            ~MarkReturn() { flag = true; }
        } mark {returned};
        co_return TerminalMove(returned);
    }
}// namespace

TEST_CASE("ContinuesOn delivers a throwing terminal result move as a fault on its target", "[Async][Transitions]")
{
    NGIN::Execution::CooperativeScheduler source, target;
    NGIN::Async::TaskContext              context(source);
    const auto                            targetRef = NGIN::Execution::ExecutorRef::From(target);
    bool                                  returned = false, delivered = false;
    auto                                  parent = [&]() -> NGIN::Async::Task<void> {
        auto result = co_await NGIN::Async::ContinuesOn(targetRef, ThrowOnTerminalMove(returned)).AsCompletion();
        REQUIRE((result.HasError() && result.Error().IsFault()));
        REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::UnhandledException);
        REQUIRE(targetRef.IsCurrent());
        delivered = true;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    source.RunUntilIdle();
    REQUIRE(returned);
    REQUIRE_FALSE(delivered);
    target.RunUntilIdle();
    source.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(delivered);
}
