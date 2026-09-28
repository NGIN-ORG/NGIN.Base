#include <NGIN/Async/CurrentEnvironment.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Net/Resolve.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>

TEST_CASE("Network leaves transfer early and pending outcomes to their context executor", "[Net][Placement]")
{
    NGIN::Execution::CooperativeScheduler source, target;
    auto                                  targetRef = NGIN::Execution::ExecutorRef::From(target);
    NGIN::Async::TaskContext              parentContext(source);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              leafContext(target, cancellation.GetToken());
    NGIN::Net::ResolverDriver             driver;
    NGIN::Net::TcpSocket                  socket;
    const int                             kind = GENERATE(0, 1, 2);
    if (kind == 1)
        cancellation.Cancel();
    bool delivered = false;
    auto parent    = [&]() -> NGIN::Async::Task<void> {
        if (kind == 2)
        {
            auto result = co_await socket.ReceiveAsync(leafContext, {}).AsCompletion();
            REQUIRE((result.HasError() && result.Error().IsFault()));
        }
        else
        {
            NGIN::Net::ResolveOptions options;
            options.numericHost    = true;
            options.numericService = true;
            auto result            = co_await NGIN::Net::ResolveAsync(leafContext, driver, "127.0.0.1", "80", options).AsCompletion();
            if (kind == 1)
                REQUIRE(result.IsStopped());
            else
                REQUIRE(result.HasValue());
        }
        REQUIRE(targetRef.IsCurrent());
        auto environment = co_await NGIN::Async::CurrentEnvironment();
        REQUIRE(environment.executor == targetRef);
        delivered = true;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(parentContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    source.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE_FALSE(delivered);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
    {
        target.RunUntilIdle();
        source.RunUntilIdle();
        std::this_thread::yield();
    }
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(delivered);
}
