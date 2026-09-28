#include <NGIN/Async/TaskSupervisor.hpp>
/// @file Resolve.cpp
/// @brief Tests for synchronous and explicitly driven asynchronous name resolution.

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/Net/Resolve.hpp>
#include <latch>
#include <thread>

#include <chrono>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Net.Resolve resolves numeric loopback without external network access", "[Net][Resolve]")
{
    NGIN::Net::ResolveOptions options;
    options.family         = NGIN::Net::AddressFamily::V4;
    options.socketType     = NGIN::Net::ResolveSocketType::Stream;
    options.numericHost    = true;
    options.numericService = true;

    const auto result = NGIN::Net::Resolve("127.0.0.1", "8080", options);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result.value().empty());
    for (const auto& address: result.value())
    {
        CHECK(address.endpoint.address == NGIN::Net::IpAddress::LoopbackV4());
        CHECK(address.endpoint.port == 8080);
        CHECK(address.socketType == NGIN::Net::ResolveSocketType::Stream);
    }
}

TEST_CASE("Net.Resolve applies address-family filters and preserves resolver diagnostics", "[Net][Resolve]")
{
    NGIN::Net::ResolveOptions v6;
    v6.family           = NGIN::Net::AddressFamily::V6;
    v6.socketType       = NGIN::Net::ResolveSocketType::Datagram;
    v6.numericHost      = true;
    v6.numericService   = true;
    const auto v6Result = NGIN::Net::Resolve("::1", "53", v6);
    REQUIRE(v6Result.has_value());
    REQUIRE_FALSE(v6Result.value().empty());
    CHECK(v6Result.value().front().endpoint.address == NGIN::Net::IpAddress::LoopbackV6());
    CHECK(v6Result.value().front().socketType == NGIN::Net::ResolveSocketType::Datagram);

    NGIN::Net::ResolveOptions numeric;
    numeric.numericHost = true;
    const auto missing  = NGIN::Net::Resolve("not-a-numeric-address", "80", numeric);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().network.code == NGIN::Net::NetErrorCode::NameNotFound);
    CHECK(missing.error().resolverCode != 0);
    CHECK_FALSE(missing.error().diagnostic.empty());

    const auto invalid = NGIN::Net::Resolve("", "");
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().network.code == NGIN::Net::NetErrorCode::InvalidArgument);
}

TEST_CASE("Net.Resolve async work uses its explicit driver and caller executor", "[Net][Resolve]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::Async::TaskContext             context {scheduler};
    NGIN::Net::ResolverDriver            driver;
    NGIN::Net::ResolveOptions            options;
    options.family         = NGIN::Net::AddressFamily::V4;
    options.socketType     = NGIN::Net::ResolveSocketType::Stream;
    options.numericHost    = true;
    options.numericService = true;

    auto completion = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "443", options));
    REQUIRE(completion.HasValue());
    REQUIRE_FALSE(completion.Value().empty());
    CHECK(completion.Value().front().endpoint.port == 443);
}

TEST_CASE("Net.Resolve async cancellation and timeout complete without waiting for lookup", "[Net][Resolve]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::Async::TaskContext             context {scheduler};
    NGIN::Net::ResolverDriver            driver;

    NGIN::Async::CancellationSource source;
    source.Cancel(NGIN::Async::StopReason::Deadline);
    auto canceled = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "localhost", "80", {}, source.GetToken()));
    CHECK(canceled.IsStopped());
    CHECK(canceled.StopReason() == NGIN::Async::StopReason::Deadline);

    NGIN::Net::ResolveOptions timedOptions;
    timedOptions.timeout = std::chrono::milliseconds {0};
    auto timedOut        = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "localhost", "80", timedOptions));
    REQUIRE((timedOut.HasError() && timedOut.Error().IsDomainError()));
    CHECK(timedOut.Error().DomainError().network.code == NGIN::Net::NetErrorCode::TimedOut);
}

TEST_CASE("Net.Resolve retires timeout capacity before delivering successful results", "[Net][Resolve][Lifetime]")
{
    NGIN::IO::Runtime         runtime({.timerCapacity = 1});
    NGIN::Net::ResolverDriver driver;
    NGIN::Async::TaskContext  context(runtime.GetExecutor());
    NGIN::Net::ResolveOptions options;
    options.numericHost    = true;
    options.numericService = true;
    options.timeout        = std::chrono::hours(1);
    for (int iteration = 0; iteration != 20; ++iteration)
    {
        auto operationTask = NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "80", options);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto       operation = operationRoot.Spawn(std::move(operationTask)).value();
        const auto deadline  = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
        {
            (void) runtime.PollOnce();
            std::this_thread::yield();
        }
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().HasValue());
    }
    runtime.Shutdown();
}

TEST_CASE("Net.Resolve uses reserved delivery after ordinary executor admission closes", "[Net][Resolve][Admission]")
{
    struct Gate final
    {
        NGIN::Execution::ExecutorRef executor;
        bool                         closed {false};
        std::atomic<int>             rejected {0};
        auto                         Execute(NGIN::Execution::WorkItem work) noexcept -> NGIN::Execution::ScheduleResult
        {
            if (closed)
            {
                ++rejected;
                return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
            }
            return executor.Execute(std::move(work));
        }
        auto ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint at) noexcept
        {
            return executor.ExecuteAt(std::move(work), at);
        }
        auto ReserveCompletion(NGIN::Execution::WorkItem work) noexcept
        {
            return executor.ReserveCompletion(std::move(work));
        }
        bool IsCurrent() const noexcept { return executor.IsCurrent(); }
    };
    std::latch                started {1}, release {1};
    NGIN::IO::Runtime         runtime;
    NGIN::Net::ResolverDriver driver;
    Gate                      executor {runtime.GetExecutor()};
    NGIN::Async::TaskContext  context(executor);
    REQUIRE(driver.GetExecutor().Execute([&] { started.count_down(); release.wait(); }));
    started.wait();
    struct Release final
    {
        std::latch& latch;
        bool        done {false};
        ~Release()
        {
            if (!done)
                latch.count_down();
        }
    } guard {release};
    NGIN::Net::ResolveOptions options;
    options.numericHost    = true;
    options.numericService = true;
    auto operationTask     = NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "80", options);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    (void) runtime.PollOnce();
    REQUIRE_FALSE(operation.IsCompleted());
    executor.closed = true;
    release.count_down();
    guard.done          = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
    {
        (void) runtime.PollOnce();
        std::this_thread::yield();
    }
    runtime.Shutdown();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().HasValue());
    REQUIRE(executor.rejected.load() == 0);
}

TEST_CASE("Net.Resolve cancellation racing setup publishes once on the selected executor", "[Net][Resolve][Race]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    NGIN::Async::TaskContext             context(scheduler);
    NGIN::Net::ResolverDriver            driver;
    for (int iteration = 0; iteration != 100; ++iteration)
    {
        NGIN::Async::CancellationSource cancellation;
        std::latch                      entered {1};
        std::jthread                    canceller([&] { entered.wait(); cancellation.Cancel(); });
        auto                            parent = [&]() -> NGIN::Async::Task<bool> {
            NGIN::Net::ResolveOptions options;
            options.numericHost    = true;
            options.numericService = true;
            options.timeout        = std::chrono::seconds(30);
            entered.count_down();
            auto result = co_await NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "80", options, cancellation.GetToken()).AsCompletion();
            co_return scheduler.IsCurrent() && (result.HasValue() || result.IsStopped());
        };
        auto result = NGIN::Async::SyncWait(context, parent());
        REQUIRE(result.HasValue());
        REQUIRE(result.Value());
    }
}

TEST_CASE("Net.Resolve supports cooperative timed delivery and retires its timer", "[Net][Resolve][Timer]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Net::ResolverDriver             driver;
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::Net::ResolveOptions             options;
    options.numericHost    = true;
    options.numericService = true;
    options.timeout        = std::chrono::hours(1);
    for (int iteration = 0; iteration != 20; ++iteration)
    {
        auto operationTask = NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "80", options);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto       operation = operationRoot.Spawn(std::move(operationTask)).value();
        const auto deadline  = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
        {
            scheduler.RunUntilIdle();
            std::this_thread::yield();
        }
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        REQUIRE(operation.TakeResult().HasValue());
        REQUIRE(scheduler.PendingTimers() == 0);
    }
}

TEST_CASE("Net.Resolve driver shutdown completes queued lookup through reserved delivery", "[Net][Resolve][Shutdown]")
{
    std::latch                            started {1}, release {1};
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    auto                                  driver         = std::make_unique<NGIN::Net::ResolverDriver>();
    auto                                  driverExecutor = driver->GetExecutor();
    REQUIRE(driverExecutor.Execute([&] { started.count_down(); release.wait(); }));
    started.wait();
    NGIN::Net::ResolveOptions options;
    options.numericHost    = true;
    options.numericService = true;
    NGIN::Async::CancellationSource cancellation;
    bool                            cancelFirst = false;
    SECTION("discard wins") {}
    SECTION("already delivered cancellation is not delivered twice")
    {
        cancelFirst = true;
    }
    auto operationTask = NGIN::Net::ResolveAsync(context, *driver, "127.0.0.1", "80", options, cancellation.GetToken());

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    const bool pending = !operation.IsCompleted();
    if (cancelFirst)
    {
        cancellation.Cancel(NGIN::Async::StopReason::OwnerClosed);
        scheduler.RunUntilIdle();
    }
    // The parked worker keeps the driver's executor alive while shutdown closes admission.
    std::jthread shutdown([owner = std::move(driver)]() mutable { owner.reset(); });
    while (driverExecutor.Execute(NGIN::Execution::WorkItem([] {})))
        std::this_thread::yield();
    release.count_down();
    shutdown.join();
    scheduler.RunUntilIdle();
    REQUIRE(pending);
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    if (cancelFirst)
    {
        REQUIRE(result.IsStopped());
        REQUIRE(result.StopReason() == NGIN::Async::StopReason::OwnerClosed);
        return;
    }
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed);
    REQUIRE(result.Error().Fault().native == static_cast<int>(NGIN::Execution::ScheduleError::Stopped));
}
