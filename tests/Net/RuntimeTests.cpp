#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <array>

TEST_CASE("Net.Runtime shutdown cancels an admitted receive on its own executor", "[Net][Runtime][Shutdown]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    NGIN::Net::UdpSocket     socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    std::array<NGIN::Byte, 16> bytes {};
    auto                       operationTask = socket.ReceiveFromAsync(context, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    REQUIRE_FALSE(runtime.PollOnce());
    REQUIRE(runtime.HasNetworkBackend());
    REQUIRE_FALSE(operation.IsCompleted());
    runtime.Shutdown();
    REQUIRE(runtime.IsStopped());
    REQUIRE(operation.IsStopped());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().StopReason() == NGIN::Async::StopReason::RuntimeShutdown);
}

TEST_CASE("Net.Runtime runner uses context cancellation and no file workers", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::RuntimeRunner              runner(runtime);
    NGIN::Net::TcpListener               listener(runtime);
    REQUIRE_FALSE(runtime.HasNetworkBackend());
    REQUIRE(listener.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(listener.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    REQUIRE(listener.Listen());
    REQUIRE_FALSE(runtime.HasNetworkBackend());
    NGIN::Async::CancellationSource cancellation;
    NGIN::Async::TaskContext        ctx(scheduler, cancellation.GetToken());
    REQUIRE(cancellation.CancelAfter(ctx.GetExecutor(), NGIN::Units::Milliseconds(50)));
    auto result = NGIN::Async::SyncWait(ctx, listener.AcceptAsync(ctx));
    REQUIRE(result.IsStopped());
    REQUIRE(runtime.HasNetworkBackend());
    REQUIRE_FALSE(runtime.HasFileBackend());
    runtime.Shutdown();
    NGIN::Async::TaskContext freshContext(scheduler);
    auto                     rejected = NGIN::Async::SyncWait(freshContext, listener.AcceptAsync(freshContext));
    REQUIRE((rejected.HasError() && rejected.Error().IsFault()));
    REQUIRE(rejected.Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("Net.Runtime manual Run waits for lazy initialization and stops cleanly", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    runtime;
    NGIN::Net::UdpSocket                 socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    NGIN::Async::CancellationSource cancellation;
    NGIN::Async::TaskContext        ctx(scheduler, cancellation.GetToken());
    REQUIRE(cancellation.CancelAfter(ctx.GetExecutor(), NGIN::Units::Milliseconds(50)));
    NGIN::Execution::Thread    poller([&runtime] { runtime.Run(); });
    std::array<NGIN::Byte, 16> bytes {};
    auto                       result = NGIN::Async::SyncWait(ctx, socket.ReceiveFromAsync(ctx, bytes));
    runtime.Shutdown();
    poller.Join();
    REQUIRE(result.IsStopped());
    REQUIRE(runtime.HasNetworkBackend());
    REQUIRE_FALSE(runtime.HasFileBackend());
}

TEST_CASE("Net.Runtime unbound sockets reject async use without starting a backend", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::Async::TaskContext             ctx(scheduler);
    NGIN::Net::UdpSocket                 socket;
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    std::array<NGIN::Byte, 16> bytes {};
    auto                       result = NGIN::Async::SyncWait(ctx, socket.ReceiveFromAsync(ctx, bytes));
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("Net.Runtime socket moves preserve bindings and runtimes stop independently", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    stopped;
    NGIN::IO::Runtime                    active;
    NGIN::IO::RuntimeRunner              runner(active);
    NGIN::Net::UdpSocket                 original(active);
    REQUIRE(original.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(original.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    NGIN::Net::UdpSocket socket(std::move(original));
    REQUIRE(socket.GetRuntime() == &active);
    stopped.Shutdown();
    NGIN::Async::CancellationSource cancellation;
    NGIN::Async::TaskContext        ctx(scheduler, cancellation.GetToken());
    REQUIRE(cancellation.CancelAfter(ctx.GetExecutor(), NGIN::Units::Milliseconds(50)));
    std::array<NGIN::Byte, 16> bytes {};
    auto                       result = NGIN::Async::SyncWait(ctx, socket.ReceiveFromAsync(ctx, bytes));
    REQUIRE(result.IsStopped());
    REQUIRE_FALSE(active.IsStopped());
    REQUIRE(active.HasNetworkBackend());
    REQUIRE_FALSE(stopped.HasNetworkBackend());
}

TEST_CASE("Net.Runtime honors a canceled operation context before a socket fast path", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    runtime;
    NGIN::Net::UdpSocket                 socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    NGIN::Async::CancellationSource cancellation;
    cancellation.Cancel();
    NGIN::Async::TaskContext  caller(scheduler);
    NGIN::Async::TaskContext  canceledContext(scheduler, cancellation.GetToken());
    std::array<NGIN::Byte, 1> bytes {};
    auto                      result = NGIN::Async::SyncWait(caller, socket.SendToAsync(
                                                        canceledContext, {NGIN::Net::IpAddress::LoopbackV4(), 1}, bytes));
    REQUIRE(result.IsStopped());
}

TEST_CASE("Net.Runtime close cancels a pending UDP receive without an application token", "[Net][Runtime][Close]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    NGIN::Net::UdpSocket     socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    std::array<NGIN::Byte, 8> bytes {};
    auto                      operationTask = socket.ReceiveFromAsync(context, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    while (runtime.PollOnce()) {}
    const bool wasPending = !operation.IsCompleted();
    socket.Close();
    while (runtime.PollOnce()) {}
    const bool closedWithoutShutdown = operation.IsStopped();
    runtime.Shutdown();
    REQUIRE(wasPending);
    REQUIRE(closedWithoutShutdown);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE_FALSE(socket.Handle().IsOpen());
}

TEST_CASE("Net.Runtime close cancels a pending listener without an application token", "[Net][Runtime][Close]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    NGIN::Net::TcpListener   listener(runtime);
    REQUIRE(listener.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(listener.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    REQUIRE(listener.Listen());
    auto operationTask = listener.AcceptAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    while (runtime.PollOnce()) {}
    const bool wasPending = !operation.IsCompleted();
    listener.Close();
    while (runtime.PollOnce()) {}
    const bool closedWithoutShutdown = operation.IsStopped();
    runtime.Shutdown();
    REQUIRE(wasPending);
    REQUIRE(closedWithoutShutdown);
    REQUIRE(operationRoot.TryJoin());
}

TEST_CASE("Net.Runtime pending receives reject another wait in the same direction", "[Net][Runtime][Admission]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    NGIN::Net::UdpSocket     socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    std::array<NGIN::Byte, 8> firstBytes {};
    std::array<NGIN::Byte, 8> secondBytes {};
    auto                      firstTask = socket.ReceiveFromAsync(context, firstBytes);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    while (runtime.PollOnce()) {}
    auto secondTask = socket.ReceiveFromAsync(context, secondBytes);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    while (runtime.PollOnce()) {}
    const bool rejected = second.IsCompleted();
    socket.Close();
    runtime.Shutdown();
    REQUIRE(rejected);
    REQUIRE(secondRoot.TryJoin());
    auto outcome = second.TakeResult();
    REQUIRE((outcome.HasError() && outcome.Error().IsDomainError()));
    REQUIRE(outcome.Error().DomainError().code == NGIN::Net::NetErrorCode::OperationInProgress);
    REQUIRE(first.IsStopped());
    REQUIRE(firstRoot.TryJoin());
}


TEST_CASE("Runtime drainage precedes external receive delivery and application joining", "[Net][Runtime][Shutdown][Ownership]")
{
    NGIN::IO::Runtime runtime;
    NGIN::Execution::CooperativeScheduler delivery, publication;
    NGIN::Async::TaskContext leafContext(delivery); // No application cancellation source.
    NGIN::Async::TaskContext ownerContext(publication);
    NGIN::Net::UdpSocket socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    bool destroyed = false;
    bool receivedStop = false;
    auto reason = NGIN::Async::StopReason::Requested;
    struct Lifetime final
    {
        bool& destroyed;
        ~Lifetime() { destroyed = true; }
    };
    auto receive = [&]() -> NGIN::Async::Task<void> {
        Lifetime lifetime {destroyed};
        std::array<NGIN::Byte, 16> buffer {};
        auto result = co_await socket.ReceiveFromAsync(leafContext, buffer).AsCompletion();
        receivedStop = result.IsStopped();
        if (receivedStop)
            reason = result.StopReason();
    };
    NGIN::Async::TaskSupervisor<> owner(ownerContext.GetEnvironment(), 1);
    auto operation = owner.SpawnOn(NGIN::Execution::ExecutorRef::From(delivery), receive()).value();
    delivery.RunUntilIdle();
    while (runtime.PollOnce()) {}
    const bool pendingBeforeStop = !operation.IsCompleted() && !destroyed;
    const auto deadline = NGIN::Time::TimePoint::FromNanoseconds(
            NGIN::Time::MonotonicClock::Now().ToNanoseconds() + 1000000000);
    const auto status = runtime.ShutdownUntil(deadline);
    const bool pendingAfterDrain = owner.Pending() == 1 && !operation.IsCompleted() && !destroyed;
    delivery.RunUntilIdle();
    const bool awaitingOwnerPublication = !operation.IsCompleted() && owner.Pending() == 1;
    publication.RunUntilIdle();
    // Complete cleanup before assertions so failed observations retain no live borrowers.
    runtime.Shutdown();
    const auto report = owner.TryJoin();
    REQUIRE(pendingBeforeStop);
    REQUIRE(status == NGIN::IO::Runtime::ShutdownStatus::Stopped);
    REQUIRE(pendingAfterDrain);
    REQUIRE(awaitingOwnerPublication);
    REQUIRE(receivedStop);
    REQUIRE(reason == NGIN::Async::StopReason::RuntimeShutdown);
    REQUIRE(destroyed);
    REQUIRE(report);
    REQUIRE(report->Succeeded());
    REQUIRE(operation.TakeResult().Succeeded());
}
