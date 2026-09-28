#include <NGIN/Async/CurrentEnvironment.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/AsyncDirectoryHandle.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#include <NGIN/IO/VirtualFileSystem.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>

TEST_CASE("File leaves transfer both pending and early outcomes to their context executor", "[IO][Placement]")
{
    NGIN::Execution::CooperativeScheduler source, target;
    auto                                  targetRef = NGIN::Execution::ExecutorRef::From(target);
    int                                   metadata  = 42;
    NGIN::Async::TaskContext              parentContext(NGIN::Async::AsyncEnvironment {
                         .executor = NGIN::Execution::ExecutorRef::From(source),
                         .metadata = &metadata});
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              leafContext(target, cancellation.GetToken());
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::RuntimeRunner               runner(runtime);
    NGIN::IO::LocalFileSystem             bound(runtime), unbound;
    NGIN::IO::AsyncFileHandle             emptyFile;
    NGIN::IO::AsyncDirectoryHandle        emptyDirectory;
    const int                             kind = GENERATE(0, 1, 2, 3, 4, 5);
    if (kind == 4)
        cancellation.Cancel();
    bool delivered = false;
    auto parent    = [&]() -> NGIN::Async::Task<void> {
        if (kind == 0 || kind == 4)
        {
            auto result = co_await bound.GetInfoAsync(leafContext, NGIN::IO::Path(".")).AsCompletion();
            if (kind == 4)
                REQUIRE(result.IsStopped());
            else
                REQUIRE(result.HasValue());
        }
        else if (kind == 1)
        {
            auto result = co_await unbound.GetInfoAsync(leafContext, NGIN::IO::Path(".")).AsCompletion();
            REQUIRE((result.HasError() && result.Error().IsFault()));
        }
        else if (kind == 2)
        {
            auto result = co_await emptyFile.ReadAsync(leafContext, {}).AsCompletion();
            REQUIRE((result.HasError() && result.Error().IsDomainError()));
        }
        else if (kind == 3)
        {
            auto result = co_await emptyDirectory.ExistsAsync(leafContext, NGIN::IO::Path(".")).AsCompletion();
            REQUIRE((result.HasError() && result.Error().IsDomainError()));
        }
        else
        {
            NGIN::IO::VirtualFileSystem filesystem;
            auto                        result = co_await filesystem.GetInfoAsync(leafContext, NGIN::IO::Path("/unmounted")).AsCompletion();
            REQUIRE((result.HasError() && result.Error().IsDomainError()));
        }
        REQUIRE(targetRef.IsCurrent());
        auto environment = co_await NGIN::Async::CurrentEnvironment();
        REQUIRE(environment.executor == targetRef);
        REQUIRE(environment.metadata == &metadata);
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
