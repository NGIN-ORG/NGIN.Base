#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Async/Race.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <memory>

TEST_CASE("Completion exposes three channels with distinct error payloads", "[Async][Completion]")
{
    using namespace NGIN::Async;
    using C      = Completion<int, int>;
    auto value   = C::Success(7);
    auto domain  = C::DomainFailure(11);
    auto fault   = C::Faulted(MakeAsyncFault(AsyncFaultCode::UnknownRuntimeFailure));
    auto stopped = C::Stopped(StopReason::Deadline);
    REQUIRE(value.Kind() == CompletionKind::Value);
    REQUIRE(domain.Kind() == CompletionKind::Error);
    REQUIRE(fault.Kind() == CompletionKind::Error);
    REQUIRE(stopped.Kind() == CompletionKind::Stopped);
    REQUIRE(domain.Error().IsDomainError());
    REQUIRE(domain.Error().DomainError() == 11);
    REQUIRE(fault.Error().IsFault());
    REQUIRE(domain.Status() == TaskStatus::Error);
    REQUIRE(fault.Status() == TaskStatus::Error);
    REQUIRE(stopped.StopReason() == StopReason::Deadline);
    REQUIRE(Completion<void, int>::Success().Kind() == CompletionKind::Value);
}

TEST_CASE("Task and combinator propagation preserves stop reasons", "[Async][Completion]")
{
    using namespace NGIN::Async;
    const auto                            reason = GENERATE(StopReason::Requested, StopReason::Deadline, StopReason::OwnerClosed, StopReason::RuntimeShutdown);
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    auto                                  leaf         = [reason](TaskContext&) -> Task<int> { co_return Completion<int, NoError>::Stopped(reason); };
    auto                                  parent       = [&](TaskContext& ctx) -> Task<int> { co_return co_await leaf(ctx); };
    auto                                  ordinaryTask = parent(context);

    TaskSupervisor<typename decltype(ordinaryTask)::ErrorType> ordinaryRoot(context.GetEnvironment(), 1);

    auto ordinary = ordinaryRoot.Spawn(std::move(ordinaryTask)).value();
    auto allTask  = WhenAll(context, leaf(context));

    TaskSupervisor<typename decltype(allTask)::ErrorType> allRoot(context.GetEnvironment(), 1);

    auto all     = allRoot.Spawn(std::move(allTask)).value();
    auto anyTask = Race(context, leaf);

    TaskSupervisor<typename decltype(anyTask)::ErrorType> anyRoot(context.GetEnvironment(), 1);

    auto any       = anyRoot.Spawn(std::move(anyTask)).value();
    auto produce   = [reason](TaskContext&) -> AsyncGenerator<int> { co_await Stopped(reason); co_return; };
    auto generator = produce(context);
    auto nextTask  = generator.Next(context);

    TaskSupervisor<typename decltype(nextTask)::ErrorType> nextRoot(context.GetEnvironment(), 1);

    auto next = nextRoot.Spawn(std::move(nextTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(ordinaryRoot.TryJoin());
    REQUIRE(ordinary.TakeResult().StopReason() == reason);
    REQUIRE(allRoot.TryJoin());
    REQUIRE(all.TakeResult().StopReason() == reason);
    REQUIRE(anyRoot.TryJoin());
    REQUIRE(any.TakeResult().StopReason() == reason);
    REQUIRE(nextRoot.TryJoin());
    REQUIRE(next.TakeResult().StopReason() == reason);
}

TEST_CASE("Retained move-only errors propagate without consuming immutable payloads", "[Async][Completion][Lifetime]")
{
    using namespace NGIN::Async;
    using E                                     = std::unique_ptr<int>;
    auto                                  owner = std::make_shared<const AsyncError<E>>(AsyncError<E>::FromDomain(std::make_unique<int>(42)));
    std::weak_ptr<const AsyncError<E>>    weak  = owner;
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    auto                                  leaf   = Task<int, E>::FromCompletion(Completion<int, E>::Failure(AsyncError<E>::Retain(owner)));
    auto                                  parent = [](Task<int, E> child) -> Task<int, E> { co_return co_await child; };
    {
        auto operationTask = parent(std::move(leaf));

        TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        owner.reset();
        scheduler.RunUntilIdle();
        {
            REQUIRE(operationRoot.TryJoin());
            const auto result = operation.TakeResult();
            REQUIRE(result.Kind() == CompletionKind::Error);
            REQUIRE(result.Error().IsRetained());
            REQUIRE(*result.Error().DomainError() == 42);
            REQUIRE_FALSE(weak.expired());
        }
        operation = {};
    }
    REQUIRE(weak.expired());
}

namespace
{
    template<typename T>
    NGIN::Async::Task<T, int> PublishThenCancel(NGIN::Async::CancellationSource source, int outcome)
    {
        struct CancelOnExit
        {
            NGIN::Async::CancellationSource source;
            ~CancelOnExit() { source.Cancel(); }
        } cancel {std::move(source)};
        using namespace NGIN::Async;
        if constexpr (std::is_void_v<T>)
        {
            if (outcome == 1)
                co_await DomainFailure(17);
            if (outcome == 2)
                co_await Faulted(MakeAsyncFault(AsyncFaultCode::UnknownRuntimeFailure));
            if (outcome == 3)
                co_await Stopped(StopReason::Deadline);
            co_return;
        }
        else
        {
            if (outcome == 1)
                co_return Completion<T, int>::DomainFailure(17);
            if (outcome == 2)
                co_return Completion<T, int>::Faulted(MakeAsyncFault(AsyncFaultCode::UnknownRuntimeFailure));
            if (outcome == 3)
                co_return Completion<T, int>::Stopped(StopReason::Deadline);
            co_return 42;
        }
    }
}// namespace

TEST_CASE("Cancellation during terminal frame cleanup cannot replace a published outcome", "[Async][Completion][Race]")
{
    using namespace NGIN::Async;
    const int                             outcome = GENERATE(0, 1, 2, 3);
    const bool                            useVoid = GENERATE(false, true);
    NGIN::Execution::CooperativeScheduler scheduler;
    CancellationSource                    source;
    TaskContext                           context(scheduler, source.GetToken());
    int                                   callbacks = 0;
    CancellationRegistration              registration;
    REQUIRE(source.GetToken().Register(registration, {}, {}, +[](void* raw) noexcept {
        ++*static_cast<int*>(raw);
        return false; }, &callbacks));
    auto verify = [&](const auto& result) {
        REQUIRE(source.IsCancellationRequested());
        REQUIRE(callbacks == 1);
        REQUIRE(result.Kind() == (outcome == 0 ? CompletionKind::Value : outcome == 3 ? CompletionKind::Stopped
                                                                                      : CompletionKind::Error));
        if (outcome == 1)
            REQUIRE(result.Error().DomainError() == 17);
        if (outcome == 2)
            REQUIRE(result.Error().Fault().code == AsyncFaultCode::UnknownRuntimeFailure);
        if (outcome == 3)
            REQUIRE(result.StopReason() == StopReason::Deadline);
        source.Cancel();
        REQUIRE(callbacks == 1);
    };
    if (useVoid)
    {
        auto operationTask = PublishThenCancel<void>(source, outcome);

        TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        verify(operation.TakeResult());
    }
    else
    {
        auto operationTask = PublishThenCancel<int>(source, outcome);

        TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        verify(result);
        if (outcome == 0)
            REQUIRE(result.Value() == 42);
    }
}
