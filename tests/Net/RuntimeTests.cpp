#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <array>

TEST_CASE("Net.Runtime background polling uses context cancellation and no file workers", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    runtime;
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
    runtime.Stop();
    NGIN::Async::TaskContext freshContext(scheduler);
    auto                     rejected = NGIN::Async::SyncWait(freshContext, listener.AcceptAsync(freshContext));
    REQUIRE(rejected.IsFault());
    REQUIRE(rejected.Fault().code == NGIN::Async::AsyncFaultCode::InvalidTaskUsage);
}

TEST_CASE("Net.Runtime manual Run waits for lazy initialization and stops cleanly", "[Net][Runtime]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::IO::Runtime                    runtime({.network = {.mode = NGIN::IO::Runtime::NetworkMode::Manual}});
    NGIN::Net::UdpSocket                 socket(runtime);
    REQUIRE(socket.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(socket.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    NGIN::Async::CancellationSource cancellation;
    NGIN::Async::TaskContext        ctx(scheduler, cancellation.GetToken());
    REQUIRE(cancellation.CancelAfter(ctx.GetExecutor(), NGIN::Units::Milliseconds(50)));
    NGIN::Execution::Thread    poller([&runtime] { runtime.Run(); });
    std::array<NGIN::Byte, 16> bytes {};
    auto                       result = NGIN::Async::SyncWait(ctx, socket.ReceiveFromAsync(ctx, bytes));
    runtime.Stop();
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
    NGIN::Net::UdpSocket                 original(active);
    REQUIRE(original.Open(NGIN::Net::AddressFamily::V4));
    REQUIRE(original.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}));
    NGIN::Net::UdpSocket socket(std::move(original));
    REQUIRE(socket.GetRuntime() == &active);
    stopped.Stop();
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
