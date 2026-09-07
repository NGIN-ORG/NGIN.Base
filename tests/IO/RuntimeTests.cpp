#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>

#include <limits>
#include <stdexcept>

TEST_CASE("IO.Runtime creates no backend for construction or synchronous use", "[IO][Runtime]")
{
    NGIN::IO::Runtime         runtime;
    NGIN::IO::LocalFileSystem files(runtime);
    NGIN::IO::LocalFileSystem synchronous;
    REQUIRE(files.GetRuntime() == &runtime);
    REQUIRE(synchronous.GetRuntime() == nullptr);
    REQUIRE(files.TempDirectory());
    REQUIRE_FALSE(runtime.HasFileBackend());
    REQUIRE_FALSE(runtime.HasNetworkBackend());
    REQUIRE(runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None);
    runtime.Stop();
    runtime.Stop();
    REQUIRE(runtime.IsStopped());
    REQUIRE(files.TempDirectory());
}

TEST_CASE("IO.Runtime shares lazy filesystem services across resources and contexts", "[IO][Runtime][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    NGIN::IO::Runtime                    runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::LocalFileSystem            first(runtime);
    NGIN::IO::LocalFileSystem            second(runtime);
    NGIN::Async::TaskContext             firstContext(scheduler);
    NGIN::Async::TaskContext             secondContext(scheduler);
    auto                                 directory = first.TempDirectory();
    REQUIRE(directory);
    auto result = NGIN::Async::SyncWait(firstContext, NGIN::Async::WhenAll(
                                                              firstContext, first.GetInfoAsync(firstContext, *directory), second.GetInfoAsync(secondContext, *directory)));
    REQUIRE(result.Succeeded());
    REQUIRE(std::get<0>(result.Value()).exists);
    REQUIRE(std::get<1>(result.Value()).exists);
    REQUIRE(runtime.HasFileBackend());
    REQUIRE(runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::WorkerFallback);
    REQUIRE_FALSE(runtime.HasNetworkBackend());
    runtime.Stop();
    auto rejected = NGIN::Async::SyncWait(firstContext, first.GetInfoAsync(firstContext, *directory));
    REQUIRE(rejected.IsFault());
    REQUIRE(rejected.Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("IO.Runtime requires an explicit binding for asynchronous filesystem operations", "[IO][Runtime][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::Async::TaskContext             ctx(scheduler);
    NGIN::IO::LocalFileSystem            files;
    auto                                 result = NGIN::Async::SyncWait(ctx, files.GetInfoAsync(ctx, NGIN::IO::Path(".")));
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("IO.Runtime validates configuration and polling mode", "[IO][Runtime]")
{
    using Runtime = NGIN::IO::Runtime;
    REQUIRE_THROWS_AS(Runtime(Runtime::Options {.network = {.mode = static_cast<Runtime::NetworkMode>(255)}}), std::invalid_argument);
    REQUIRE_THROWS_AS(Runtime(Runtime::Options {.files = {.workerThreads = 0}}), std::invalid_argument);
    REQUIRE_THROWS_AS(Runtime(Runtime::Options {.files = {.queueDepthHint = 0}}), std::invalid_argument);
    REQUIRE_THROWS_AS(Runtime(Runtime::Options {.network = {.pollInterval = NGIN::Units::Milliseconds(0)}}), std::invalid_argument);
    REQUIRE_THROWS_AS(Runtime(Runtime::Options {.network = {.pollInterval = NGIN::Units::Milliseconds(std::numeric_limits<double>::infinity())}}), std::invalid_argument);
    Runtime runtime;
    REQUIRE_THROWS_AS(runtime.PollOnce(), std::logic_error);
    REQUIRE_THROWS_AS(runtime.Run(), std::logic_error);
    Runtime manual({.network = {.mode = Runtime::NetworkMode::Manual}});
    manual.PollOnce();
    REQUIRE_FALSE(manual.HasNetworkBackend());
    manual.Stop();
    manual.Run();
}
