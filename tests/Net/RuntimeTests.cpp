#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Task.hpp>
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
    auto                       operation = NGIN::Async::Spawn(context, socket.ReceiveFromAsync(context, bytes));
    REQUIRE_FALSE(runtime.PollOnce());
    REQUIRE(runtime.HasNetworkBackend());
    REQUIRE_FALSE(operation.IsCompleted());
    runtime.Shutdown();
    REQUIRE(runtime.IsStopped());
    REQUIRE(operation.IsCanceled());
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
    REQUIRE(result.IsCanceled());
    REQUIRE(runtime.HasNetworkBackend());
    REQUIRE_FALSE(runtime.HasFileBackend());
    runtime.Shutdown();
    NGIN::Async::TaskContext freshContext(scheduler);
    auto                     rejected = NGIN::Async::SyncWait(freshContext, listener.AcceptAsync(freshContext));
    REQUIRE(rejected.IsFault());
    REQUIRE(rejected.Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
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
    REQUIRE(result.IsCanceled());
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
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
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
    REQUIRE(result.IsCanceled());
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
    REQUIRE(result.IsCanceled());
}

TEST_CASE("Net.Runtime close cancels a pending UDP receive without an application token", "[Net][Runtime][Close]")
{
    NGIN::IO::Runtime        runtime;
    NGIN::Async::TaskContext context(runtime.GetExecutor());
    NGIN::Net::UdpSocket     socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    std::array<NGIN::Byte, 8> bytes {};
    auto                      operation = NGIN::Async::Spawn(context, socket.ReceiveFromAsync(context, bytes));
    while (runtime.PollOnce()) {}
    const bool wasPending = !operation.IsCompleted();
    socket.Close();
    while (runtime.PollOnce()) {}
    const bool closedWithoutShutdown = operation.IsCanceled();
    runtime.Shutdown();
    REQUIRE(wasPending);
    REQUIRE(closedWithoutShutdown);
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
    auto operation = NGIN::Async::Spawn(context, listener.AcceptAsync(context));
    while (runtime.PollOnce()) {}
    const bool wasPending = !operation.IsCompleted();
    listener.Close();
    while (runtime.PollOnce()) {}
    const bool closedWithoutShutdown = operation.IsCanceled();
    runtime.Shutdown();
    REQUIRE(wasPending);
    REQUIRE(closedWithoutShutdown);
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
    auto                      first = NGIN::Async::Spawn(context, socket.ReceiveFromAsync(context, firstBytes));
    while (runtime.PollOnce()) {}
    auto second = NGIN::Async::Spawn(context, socket.ReceiveFromAsync(context, secondBytes));
    while (runtime.PollOnce()) {}
    const bool rejected = second.IsCompleted();
    socket.Close();
    runtime.Shutdown();
    REQUIRE(rejected);
    auto outcome = second.TakeResult();
    REQUIRE(outcome.IsDomainError());
    REQUIRE(outcome.DomainError().code == NGIN::Net::NetErrorCode::OperationInProgress);
    REQUIRE(first.IsCanceled());
}
