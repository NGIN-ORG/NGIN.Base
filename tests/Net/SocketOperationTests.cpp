#include <catch2/catch_test_macros.hpp>

#include "../../src/NGIN/Net/SocketPlatform.hpp"
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <array>
#include <barrier>
#include <chrono>
#include <optional>
#include <thread>

namespace
{
    using namespace NGIN;
    using namespace NGIN::Net;

    struct ShutdownAndDrain final
    {
        IO::Runtime&                     runtime;
        Execution::CooperativeScheduler& scheduler;
        ~ShutdownAndDrain()
        {
            runtime.Shutdown();
            scheduler.RunUntilIdle();
        }
    };

    Endpoint BoundEndpoint(const SocketHandle& handle)
    {
        sockaddr_storage address {};
        socklen_t        length = sizeof(address);
        REQUIRE(::getsockname(NGIN::Net::detail::ToNative(handle), reinterpret_cast<sockaddr*>(&address), &length) == 0);
        return NGIN::Net::detail::FromSockAddr(address, length);
    }

    template<typename Predicate>
    bool Pump(IO::Runtime& runtime, Execution::CooperativeScheduler& scheduler, Predicate complete)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do
        {
            (void) runtime.PollOnce();
            scheduler.RunUntilIdle();
            if (complete())
                return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}// namespace

TEST_CASE("Net socket tasks retain their resource across cold and pending moves", "[Net][Runtime][Move]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    std::optional<UdpSocket>        original(std::in_place, runtime);
    REQUIRE(original->Open(AddressFamily::V4));
    REQUIRE(original->Bind({IpAddress::LoopbackV4(), 0}));
    const Endpoint           endpoint = BoundEndpoint(original->Handle());
    std::array<Byte, 4>      buffer {};
    auto                     task = original->ReceiveFromAsync(ctx, buffer);
    std::optional<UdpSocket> moved;
    SECTION("before starting the cold task")
    {
        moved.emplace(std::move(*original));
        original.reset();
    }
    SECTION("after native admission") {}
    auto operation = Async::Spawn(ctx, std::move(task));
    scheduler.RunUntilIdle();
    if (original)
    {
        moved.emplace(std::move(*original));
        original.reset();
    }
    UdpSocket sender;
    REQUIRE(sender.Open(AddressFamily::V4));
    const std::array<Byte, 2> payload {Byte {11}, Byte {42}};
    REQUIRE(sender.TrySendTo(endpoint, payload));
    const bool completed = Pump(runtime, scheduler, [&] { return operation.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(completed);
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().bytesReceived == payload.size());
    REQUIRE(buffer[0] == payload[0]);
    REQUIRE(buffer[1] == payload[1]);
}

TEST_CASE("Net destroying a pending socket cancels without retaining its wrapper", "[Net][Runtime][Close]")
{
    IO::Runtime                                       runtime;
    Execution::CooperativeScheduler                   scheduler;
    Async::TaskContext                                ctx(scheduler);
    std::array<Byte, 8>                               buffer {};
    Async::Operation<DatagramReceiveResult, NetError> operation;
    {
        UdpSocket socket(runtime);
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
        operation = Async::Spawn(ctx, socket.ReceiveFromAsync(ctx, buffer));
        scheduler.RunUntilIdle();
        REQUIRE_FALSE(operation.IsCompleted());
    }
    const bool completed = Pump(runtime, scheduler, [&] { return operation.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(completed);
    REQUIRE(operation.IsCanceled());
}

TEST_CASE("Net a ready datagram cannot bypass same-direction admission", "[Net][Runtime][Admission]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       receiver(runtime);
    UdpSocket                       sender;
    REQUIRE(receiver.Open(AddressFamily::V4));
    REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(sender.Open(AddressFamily::V4));
    std::array<Byte, 4> firstBuffer {};
    std::array<Byte, 4> secondBuffer {};
    auto                first = Async::Spawn(ctx, receiver.ReceiveFromAsync(ctx, firstBuffer));
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {73}};
    REQUIRE(sender.TrySendTo(BoundEndpoint(receiver.Handle()), payload));
    auto second = Async::Spawn(ctx, receiver.ReceiveFromAsync(ctx, secondBuffer));
    // Start the second receive before the runtime consumes readiness for first.
    scheduler.RunUntilIdle();
    const bool rejected = second.IsCompleted();
    const bool received = Pump(runtime, scheduler, [&] { return first.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    auto result = second.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(received);
    REQUIRE(first.TakeResult().Succeeded());
    REQUIRE(firstBuffer[0] == payload[0]);
    REQUIRE(secondBuffer[0] == Byte {});
}

TEST_CASE("Net operation and registration saturation reject before sending and recover", "[Net][Runtime][Admission]")
{
    IO::Runtime::Options options;
    bool                 registrationLimit = false;
    SECTION("operation budget")
    {
        options.operationCapacity = 1;
    }
    SECTION("registration budget")
    {
        options.registrationCapacity = 1;
        registrationLimit            = true;
    }
    IO::Runtime                     runtime(options);
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       pending(runtime);
    UdpSocket                       sender(runtime);
    UdpSocket                       receiver;
    REQUIRE(pending.Open(AddressFamily::V4));
    REQUIRE(pending.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(sender.Open(AddressFamily::V4));
    REQUIRE(receiver.Open(AddressFamily::V4));
    REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
    const Endpoint      endpoint = BoundEndpoint(receiver.Handle());
    std::array<Byte, 8> buffer {};
    auto                first = Async::Spawn(ctx, pending.ReceiveFromAsync(ctx, buffer));
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {19}};
    auto                      rejected = Async::Spawn(ctx, sender.SendToAsync(ctx, endpoint, payload));
    scheduler.RunUntilIdle();
    const bool rejectedBeforeProgress = rejected.IsCompleted();
    auto       unseen                 = receiver.TryReceiveFrom(buffer);
    pending.Close();
    const bool canceled  = Pump(runtime, scheduler, [&] { return first.IsCompleted(); });
    auto       recovered = Async::Spawn(ctx, sender.SendToAsync(ctx, endpoint, payload));
    const bool sent      = Pump(runtime, scheduler, [&] { return recovered.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejectedBeforeProgress);
    auto failure = rejected.TakeResult();
    if (registrationLimit)
    {
        REQUIRE(failure.IsDomainError());
        REQUIRE(failure.DomainError().code == NetErrorCode::ResourceExhausted);
    }
    else
    {
        REQUIRE(failure.IsFault());
        REQUIRE(failure.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    }
    REQUIRE_FALSE(unseen);
    REQUIRE(unseen.error().code == NetErrorCode::WouldBlock);
    REQUIRE(canceled);
    REQUIRE(first.IsCanceled());
    REQUIRE(sent);
    REQUIRE(recovered.TakeResult().Succeeded());
    auto received = receiver.TryReceiveFrom(buffer);
    REQUIRE(received);
    REQUIRE(received->bytesReceived == 1);
    REQUIRE(buffer[0] == payload[0]);
}

TEST_CASE("Net fast sends require reserved completion storage", "[Net][Runtime][Admission]")
{
    IO::Runtime runtime;
    // The started task owns the only slot; its I/O cannot reserve delivery.
    Execution::CooperativeScheduler scheduler(1);
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       sender(runtime);
    UdpSocket                       receiver;
    REQUIRE(sender.Open(AddressFamily::V4));
    REQUIRE(receiver.Open(AddressFamily::V4));
    REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
    std::array<Byte, 1> bytes {Byte {51}};
    auto                operation = Async::Spawn(ctx, sender.SendToAsync(ctx, BoundEndpoint(receiver.Handle()), bytes));
    scheduler.RunUntilIdle();
    runtime.Shutdown();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    auto received = receiver.TryReceiveFrom(bytes);
    REQUIRE_FALSE(received);
    REQUIRE(received.error().code == NetErrorCode::WouldBlock);
}

TEST_CASE("Net async admission rejects blocking sockets", "[Net][Runtime][Admission]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       socket(runtime);
    SocketOptions                   options;
    options.nonBlocking = false;
    REQUIRE(socket.Open(AddressFamily::V4, options));
    REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
    std::array<Byte, 4> buffer {};
    auto                operation = Async::Spawn(ctx, socket.ReceiveFromAsync(ctx, buffer));
    scheduler.RunUntilIdle();
    const bool rejected = operation.IsCompleted();
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    auto result = operation.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().code == NetErrorCode::InvalidArgument);
}

TEST_CASE("Net moved listeners and clients complete on the selected external executor", "[Net][Runtime][Move][Affinity]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    TcpListener                     listener(runtime);
    REQUIRE(listener.Open(AddressFamily::V4));
    REQUIRE(listener.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(listener.Listen());
    const Endpoint endpoint = BoundEndpoint(listener.Handle());
    auto           accept   = Async::Spawn(ctx, listener.AcceptAsync(ctx));
    scheduler.RunUntilIdle();
    TcpListener movedListener(std::move(listener));
    TcpSocket   client(runtime);
    REQUIRE(client.Open(AddressFamily::V4));
    auto      connectTask = client.ConnectAsync(ctx, endpoint);
    TcpSocket movedClient(std::move(client));
    auto      connect = Async::Spawn(ctx, std::move(connectTask));
    scheduler.RunUntilIdle();
    auto overlappingAccept = Async::Spawn(ctx, movedListener.AcceptAsync(ctx));
    scheduler.RunUntilIdle();
    const bool connected = Pump(runtime, scheduler, [&] { return connect.IsCompleted() && accept.IsCompleted(); });
    if (!connected)
    {
        runtime.Shutdown();
        scheduler.RunUntilIdle();
    }
    REQUIRE(connected);
    REQUIRE(overlappingAccept.IsCompleted());
    auto overlap = overlappingAccept.TakeResult();
    REQUIRE(overlap.IsDomainError());
    REQUIRE(overlap.DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(connect.TakeResult().Succeeded());
    auto accepted = accept.TakeResult();
    REQUIRE(accepted.Succeeded());
    TcpSocket server = std::move(accepted.Value());
    REQUIRE(server.GetRuntime() == &runtime);
    std::array<Byte, 8> incoming {};
    auto                read = Async::Spawn(ctx, movedClient.ReceiveAsync(ctx, incoming));
    scheduler.RunUntilIdle();
    // A pending read must not prevent the independent write from progressing.
    const std::array<Byte, 1> payload {Byte {91}};
    auto                      write = Async::Spawn(ctx, movedClient.SendAsync(ctx, payload));
    const bool                sent  = Pump(runtime, scheduler, [&] { return write.IsCompleted(); });
    std::array<Byte, 8>       outgoing {};
    auto                      serverRead    = server.TryReceive(outgoing);
    bool                      onExternal    = false;
    bool                      readSucceeded = false;
    auto                      observe       = [&](Async::TaskContext& child) -> Async::Task<void, NetError> {
        (void) child;
        const auto result = co_await read;
        readSucceeded     = result.Succeeded();
        onExternal        = ctx.GetExecutor().IsCurrent() && !runtime.GetExecutor().IsCurrent();
    };
    auto observed = Async::Spawn(ctx, observe(ctx));
    scheduler.RunUntilIdle();
    REQUIRE(server.TrySend(payload));
    const bool received = Pump(runtime, scheduler, [&] { return observed.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(sent);
    REQUIRE(write.TakeResult().Succeeded());
    REQUIRE(serverRead);
    REQUIRE(*serverRead == 1);
    REQUIRE(outgoing[0] == payload[0]);
    REQUIRE(received);
    REQUIRE(observed.TakeResult().Succeeded());
    REQUIRE(readSucceeded);
    REQUIRE(onExternal);
    REQUIRE(incoming[0] == payload[0]);
}

TEST_CASE("Net pending connects reserve both directions and cancellation closes the connection", "[Net][Runtime][Admission][Cancel]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::CancellationSource       cancellation;
    Async::TaskContext              ctx(scheduler);
    TcpListener                     listener;
    REQUIRE(listener.Open(AddressFamily::V4));
    REQUIRE(listener.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(listener.Listen());
    const Endpoint endpoint = BoundEndpoint(listener.Handle());
    TcpSocket      client(runtime);
    REQUIRE(client.Open(AddressFamily::V4));
    auto connecting = Async::Spawn(ctx, client.ConnectAsync(ctx, endpoint, cancellation.GetToken()));
    scheduler.RunUntilIdle();
    const bool          pending   = !connecting.IsCompleted();
    auto                duplicate = Async::Spawn(ctx, client.ConnectAsync(ctx, endpoint));
    std::array<Byte, 1> bytes {};
    auto                reading = Async::Spawn(ctx, client.ReceiveAsync(ctx, bytes));
    auto                writing = Async::Spawn(ctx, client.SendAsync(ctx, bytes));
    scheduler.RunUntilIdle();
    cancellation.Cancel();
    const bool canceled = Pump(runtime, scheduler, [&] { return connecting.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(pending);
    REQUIRE(canceled);
    REQUIRE(connecting.IsCanceled());
    REQUIRE_FALSE(client.Handle().IsOpen());
    auto duplicateResult = duplicate.TakeResult();
    auto readingResult   = reading.TakeResult();
    auto writingResult   = writing.TakeResult();
    REQUIRE(duplicateResult.IsDomainError());
    REQUIRE(duplicateResult.DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(readingResult.IsDomainError());
    REQUIRE(readingResult.DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(writingResult.IsDomainError());
    REQUIRE(writingResult.DomainError().code == NetErrorCode::OperationInProgress);
}

TEST_CASE("Net close and ready cancellation races retire before descriptor reuse", "[Net][Runtime][Race][Close]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       sender;
    REQUIRE(sender.Open(AddressFamily::V4));
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        UdpSocket socket(runtime);
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
        const Endpoint            endpoint = BoundEndpoint(socket.Handle());
        Async::CancellationSource cancellation;
        std::array<Byte, 8>       buffer {};
        const std::array<Byte, 1> payload {Byte {37}};
        auto                      operation = Async::Spawn(ctx, socket.ReceiveFromAsync(ctx, buffer, cancellation.GetToken()));
        scheduler.RunUntilIdle();
        std::barrier start(3);
        std::thread  close([&] { start.arrive_and_wait(); socket.Close(); });
        std::thread  cancel([&] { start.arrive_and_wait(); cancellation.Cancel(); });
        start.arrive_and_wait();
        (void) sender.TrySendTo(endpoint, payload);
        (void) runtime.PollOnce();
        close.join();
        cancel.join();
        const bool completed = Pump(runtime, scheduler, [&] { return operation.IsCompleted(); });
        if (!completed)
        {
            runtime.Shutdown();
            scheduler.RunUntilIdle();
        }
        REQUIRE(completed);
        auto result = operation.TakeResult();
        REQUIRE((result.Succeeded() || result.IsCanceled()));
        if (result.Succeeded())
            REQUIRE(buffer[0] == payload[0]);

        // Reopening the same wrapper must create independent state. Readiness
        // left in a previous OS batch cannot address the new receive's buffer.
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
        auto next = Async::Spawn(ctx, socket.ReceiveFromAsync(ctx, buffer));
        scheduler.RunUntilIdle();
        (void) runtime.PollOnce();
        scheduler.RunUntilIdle();
        const bool stillPending = !next.IsCompleted();
        socket.Close();
        const bool retired = Pump(runtime, scheduler, [&] { return next.IsCompleted(); });
        if (!retired)
        {
            runtime.Shutdown();
            scheduler.RunUntilIdle();
        }
        REQUIRE(stillPending);
        REQUIRE(retired);
        REQUIRE(next.IsCanceled());
    }
    runtime.Shutdown();
}

TEST_CASE("Net a cold task cannot switch to a reopened socket", "[Net][Runtime][Move][Close]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              ctx(scheduler);
    UdpSocket                       socket(runtime);
    REQUIRE(socket.Open(AddressFamily::V4));
    REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
    std::array<Byte, 1> oldBuffer {};
    auto                oldTask = socket.ReceiveFromAsync(ctx, oldBuffer);
    socket.Close();
    REQUIRE(socket.Open(AddressFamily::V4));
    REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
    std::array<Byte, 1> newBuffer {};
    auto                current  = Async::Spawn(ctx, socket.ReceiveFromAsync(ctx, newBuffer));
    auto                previous = Async::Spawn(ctx, std::move(oldTask));
    scheduler.RunUntilIdle();
    const bool rejected     = previous.IsCompleted();
    const bool stillPending = !current.IsCompleted();
    socket.Close();
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    auto outcome = previous.TakeResult();
    REQUIRE(outcome.IsDomainError());
    REQUIRE(outcome.DomainError().code == NetErrorCode::Disconnected);
    REQUIRE(stillPending);
    REQUIRE(current.IsCanceled());
}

TEST_CASE("Net a pending TCP read permits a write on the same socket", "[Net][Runtime][Admission][Duplex]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              context(scheduler);
    TcpListener                     listener(runtime);
    REQUIRE(listener.Open(AddressFamily::V4));
    REQUIRE(listener.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(listener.Listen(4));
    TcpSocket client(runtime);
    REQUIRE(client.Open(AddressFamily::V4));
    REQUIRE(client.Connect(BoundEndpoint(listener.Handle())));
    auto accepted = listener.TryAccept();
    REQUIRE(accepted);
    auto                      server = std::move(accepted).value();
    std::array<Byte, 4>       inbound {}, outbound {};
    const std::array<Byte, 2> payload {Byte {17}, Byte {29}};
    ShutdownAndDrain          cleanup {runtime, scheduler};
    auto                      reading = Async::Spawn(context, client.ReceiveAsync(context, inbound));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(reading.IsCompleted());
    auto       writing          = Async::Spawn(context, client.SendAsync(context, payload));
    const bool sent             = Pump(runtime, scheduler, [&] { return writing.IsCompleted(); });
    const bool readStillPending = !reading.IsCompleted();
    auto       received         = server.TryReceive(outbound);
    auto       reply            = server.TrySend(payload);
    const bool read             = Pump(runtime, scheduler, [&] { return reading.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(sent);
    REQUIRE(readStillPending);
    REQUIRE(writing.TakeResult().Value() == payload.size());
    REQUIRE(received);
    REQUIRE(received.value() == payload.size());
    REQUIRE(outbound[0] == payload[0]);
    REQUIRE(reply);
    REQUIRE(read);
    REQUIRE(reading.TakeResult().Value() == payload.size());
    REQUIRE(inbound[1] == payload[1]);
}

#if defined(_WIN32)
TEST_CASE("Net IOCP datagram truncation retires the receive lease before reuse", "[Net][Runtime][IOCP]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler scheduler;
    Async::TaskContext              context(scheduler);
    UdpSocket                       receiver(runtime), sender;
    REQUIRE(receiver.Open(AddressFamily::V4));
    REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(sender.Open(AddressFamily::V4));
    const auto                endpoint = BoundEndpoint(receiver.Handle());
    std::array<Byte, 2>       buffer {};
    const std::array<Byte, 8> oversized {};
    ShutdownAndDrain          cleanup {runtime, scheduler};
    auto                      reading = Async::Spawn(context, receiver.ReceiveFromAsync(context, buffer));
    scheduler.RunUntilIdle();
    REQUIRE(sender.TrySendTo(endpoint, oversized));
    const bool truncated = Pump(runtime, scheduler, [&] { return reading.IsCompleted(); });
    auto       next      = Async::Spawn(context, receiver.ReceiveFromAsync(context, buffer));
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {23}};
    REQUIRE(sender.TrySendTo(endpoint, payload));
    const bool recovered = Pump(runtime, scheduler, [&] { return next.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(truncated);
    const auto result = reading.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError().code == NetErrorCode::MessageTooLarge);
    REQUIRE(recovered);
    REQUIRE(next.TakeResult().Value().bytesReceived == 1);
    REQUIRE(buffer[0] == payload[0]);
}
#endif
