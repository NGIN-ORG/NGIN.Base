#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/NGIN/Net/SocketPlatform.hpp"
#include <NGIN/Async/CurrentEnvironment.hpp>
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
    auto operationTask = std::move(task);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
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
    REQUIRE(operationRoot.TryJoin());
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
    Async::TaskSupervisor<NetError>                   operationRoot(ctx.GetEnvironment(), 1);
    {
        UdpSocket socket(runtime);
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
        operation = operationRoot.Spawn(socket.ReceiveFromAsync(ctx, buffer)).value();
        scheduler.RunUntilIdle();
        REQUIRE_FALSE(operation.IsCompleted());
    }
    const bool completed = Pump(runtime, scheduler, [&] { return operation.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(completed);
    REQUIRE(operation.IsStopped());
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().StopReason() == Async::StopReason::OwnerClosed);
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
    auto                firstTask = receiver.ReceiveFromAsync(ctx, firstBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(ctx.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {73}};
    REQUIRE(sender.TrySendTo(BoundEndpoint(receiver.Handle()), payload));
    auto secondTask = receiver.ReceiveFromAsync(ctx, secondBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(ctx.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    // Start the second receive before the runtime consumes readiness for first.
    scheduler.RunUntilIdle();
    const bool rejected = second.IsCompleted();
    const bool received = Pump(runtime, scheduler, [&] { return first.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    REQUIRE(secondRoot.TryJoin());
    auto result = second.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(received);
    REQUIRE(firstRoot.TryJoin());
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
    auto                firstTask = pending.ReceiveFromAsync(ctx, buffer);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(ctx.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {19}};
    auto                      rejectedTask = sender.SendToAsync(ctx, endpoint, payload);

    NGIN::Async::TaskSupervisor<typename decltype(rejectedTask)::ErrorType> rejectedRoot(ctx.GetEnvironment(), 1);

    auto rejected = rejectedRoot.Spawn(std::move(rejectedTask)).value();
    scheduler.RunUntilIdle();
    const bool rejectedBeforeProgress = rejected.IsCompleted();
    auto       unseen                 = receiver.TryReceiveFrom(buffer);
    pending.Close();
    const bool canceled      = Pump(runtime, scheduler, [&] { return first.IsCompleted(); });
    auto       recoveredTask = sender.SendToAsync(ctx, endpoint, payload);

    NGIN::Async::TaskSupervisor<typename decltype(recoveredTask)::ErrorType> recoveredRoot(ctx.GetEnvironment(), 1);

    auto       recovered = recoveredRoot.Spawn(std::move(recoveredTask)).value();
    const bool sent      = Pump(runtime, scheduler, [&] { return recovered.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejectedBeforeProgress);
    REQUIRE(rejectedRoot.TryJoin());
    auto failure = rejected.TakeResult();
    if (registrationLimit)
    {
        REQUIRE((failure.HasError() && failure.Error().IsDomainError()));
        REQUIRE(failure.Error().DomainError().code == NetErrorCode::ResourceExhausted);
    }
    else
    {
        REQUIRE((failure.HasError() && failure.Error().IsFault()));
        REQUIRE(failure.Error().Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    }
    REQUIRE_FALSE(unseen);
    REQUIRE(unseen.error().code == NetErrorCode::WouldBlock);
    REQUIRE(canceled);
    REQUIRE(first.IsStopped());
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(sent);
    REQUIRE(recoveredRoot.TryJoin());
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
    auto                operationTask = sender.SendToAsync(ctx, BoundEndpoint(receiver.Handle()), bytes);

    Execution::CooperativeScheduler ownerScheduler;
    Async::TaskContext              ownerContext(ownerScheduler);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ownerContext.GetEnvironment(), 1);

    auto operation = operationRoot.SpawnOn(ctx.GetExecutor(), std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    ownerScheduler.RunUntilIdle();
    runtime.Shutdown();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsFault()));
    REQUIRE(result.Error().Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
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
    std::array<Byte, 4>             buffer {};
    Async::TaskSupervisor<NetError> operationRoot(ctx.GetEnvironment(), 1);
    auto                            operation = operationRoot.Spawn(socket.ReceiveFromAsync(ctx, buffer)).value();
    scheduler.RunUntilIdle();
    const bool rejected = operation.IsCompleted();
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().code == NetErrorCode::InvalidArgument);
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
    const Endpoint endpoint   = BoundEndpoint(listener.Handle());
    auto           acceptTask = listener.AcceptAsync(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(acceptTask)::ErrorType> acceptRoot(ctx.GetEnvironment(), 1);

    auto accept = acceptRoot.Spawn(std::move(acceptTask)).value();
    scheduler.RunUntilIdle();
    TcpListener movedListener(std::move(listener));
    TcpSocket   client(runtime);
    REQUIRE(client.Open(AddressFamily::V4));
    auto      connectTask = client.ConnectAsync(ctx, endpoint);
    TcpSocket movedClient(std::move(client));

    NGIN::Async::TaskSupervisor<typename decltype(connectTask)::ErrorType> connectRoot(ctx.GetEnvironment(), 1);

    auto connect = connectRoot.Spawn(std::move(connectTask)).value();
    scheduler.RunUntilIdle();
    auto overlappingAcceptTask = movedListener.AcceptAsync(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(overlappingAcceptTask)::ErrorType> overlappingAcceptRoot(ctx.GetEnvironment(), 1);

    auto overlappingAccept = overlappingAcceptRoot.Spawn(std::move(overlappingAcceptTask)).value();
    scheduler.RunUntilIdle();
    const bool connected = Pump(runtime, scheduler, [&] { return connect.IsCompleted() && accept.IsCompleted(); });
    if (!connected)
    {
        runtime.Shutdown();
        scheduler.RunUntilIdle();
    }
    REQUIRE(connected);
    REQUIRE(overlappingAccept.IsCompleted());
    REQUIRE(overlappingAcceptRoot.TryJoin());
    auto overlap = overlappingAccept.TakeResult();
    REQUIRE((overlap.HasError() && overlap.Error().IsDomainError()));
    REQUIRE(overlap.Error().DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE(connectRoot.TryJoin());
    REQUIRE(connect.TakeResult().Succeeded());
    REQUIRE(acceptRoot.TryJoin());
    auto accepted = accept.TakeResult();
    REQUIRE(accepted.Succeeded());
    TcpSocket server = std::move(accepted.Value());
    REQUIRE(server.GetRuntime() == &runtime);
    std::array<Byte, 8> incoming {};
    auto                readTask = movedClient.ReceiveAsync(ctx, incoming);

    NGIN::Async::TaskSupervisor<typename decltype(readTask)::ErrorType> readRoot(ctx.GetEnvironment(), 1);

    auto read = readRoot.Spawn(std::move(readTask)).value();
    scheduler.RunUntilIdle();
    // A pending read must not prevent the independent write from progressing.
    const std::array<Byte, 1> payload {Byte {91}};
    auto                      writeTask = movedClient.SendAsync(ctx, payload);

    NGIN::Async::TaskSupervisor<typename decltype(writeTask)::ErrorType> writeRoot(ctx.GetEnvironment(), 1);

    auto                write = writeRoot.Spawn(std::move(writeTask)).value();
    const bool          sent  = Pump(runtime, scheduler, [&] { return write.IsCompleted(); });
    std::array<Byte, 8> outgoing {};
    auto                serverRead    = server.TryReceive(outgoing);
    bool                onExternal    = false;
    bool                readSucceeded = false;
    auto                observe       = [&](Async::TaskContext& child) -> Async::Task<void, NetError> {
        (void) child;
        const auto result = co_await read;
        readSucceeded     = result.Succeeded();
        onExternal        = ctx.GetExecutor().IsCurrent() && !runtime.GetExecutor().IsCurrent();
    };
    auto observedTask = observe(ctx);

    NGIN::Async::TaskSupervisor<typename decltype(observedTask)::ErrorType> observedRoot(ctx.GetEnvironment(), 1);

    auto observed = observedRoot.Spawn(std::move(observedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(server.TrySend(payload));
    const bool received = Pump(runtime, scheduler, [&] { return observed.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(sent);
    REQUIRE(writeRoot.TryJoin());
    REQUIRE(write.TakeResult().Succeeded());
    REQUIRE(serverRead);
    REQUIRE(*serverRead == 1);
    REQUIRE(outgoing[0] == payload[0]);
    REQUIRE(received);
    REQUIRE(observedRoot.TryJoin());
    REQUIRE(observed.TakeResult().Succeeded());
    REQUIRE(readSucceeded);
    REQUIRE(readRoot.TryJoin());
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
    auto connectingTask = client.ConnectAsync(ctx, endpoint, cancellation.GetToken());

    NGIN::Async::TaskSupervisor<typename decltype(connectingTask)::ErrorType> connectingRoot(ctx.GetEnvironment(), 1);

    auto connecting = connectingRoot.Spawn(std::move(connectingTask)).value();
    scheduler.RunUntilIdle();
    const bool pending       = !connecting.IsCompleted();
    auto       duplicateTask = client.ConnectAsync(ctx, endpoint);

    NGIN::Async::TaskSupervisor<typename decltype(duplicateTask)::ErrorType> duplicateRoot(ctx.GetEnvironment(), 1);

    auto                duplicate = duplicateRoot.Spawn(std::move(duplicateTask)).value();
    std::array<Byte, 1> bytes {};
    auto                readingTask = client.ReceiveAsync(ctx, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(readingTask)::ErrorType> readingRoot(ctx.GetEnvironment(), 1);

    auto reading     = readingRoot.Spawn(std::move(readingTask)).value();
    auto writingTask = client.SendAsync(ctx, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(writingTask)::ErrorType> writingRoot(ctx.GetEnvironment(), 1);

    auto writing = writingRoot.Spawn(std::move(writingTask)).value();
    scheduler.RunUntilIdle();
    cancellation.Cancel(Async::StopReason::Deadline);
    const bool canceled = Pump(runtime, scheduler, [&] { return connecting.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(pending);
    REQUIRE(canceled);
    REQUIRE(connecting.IsStopped());
    REQUIRE(connectingRoot.TryJoin());
    REQUIRE(connecting.TakeResult().StopReason() == Async::StopReason::Deadline);
    REQUIRE_FALSE(client.Handle().IsOpen());
    REQUIRE(duplicateRoot.TryJoin());
    auto duplicateResult = duplicate.TakeResult();
    REQUIRE(readingRoot.TryJoin());
    auto readingResult = reading.TakeResult();
    REQUIRE(writingRoot.TryJoin());
    auto writingResult = writing.TakeResult();
    REQUIRE((duplicateResult.HasError() && duplicateResult.Error().IsDomainError()));
    REQUIRE(duplicateResult.Error().DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE((readingResult.HasError() && readingResult.Error().IsDomainError()));
    REQUIRE(readingResult.Error().DomainError().code == NetErrorCode::OperationInProgress);
    REQUIRE((writingResult.HasError() && writingResult.Error().IsDomainError()));
    REQUIRE(writingResult.Error().DomainError().code == NetErrorCode::OperationInProgress);
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
        auto                      operationTask = socket.ReceiveFromAsync(ctx, buffer, cancellation.GetToken());

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ctx.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
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
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE((result.Succeeded() || result.IsStopped()));
        if (result.Succeeded())
            REQUIRE(buffer[0] == payload[0]);

        // Reopening the same wrapper must create independent state. Readiness
        // left in a previous OS batch cannot address the new receive's buffer.
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::LoopbackV4(), 0}));
        auto nextTask = socket.ReceiveFromAsync(ctx, buffer);

        NGIN::Async::TaskSupervisor<typename decltype(nextTask)::ErrorType> nextRoot(ctx.GetEnvironment(), 1);

        auto next = nextRoot.Spawn(std::move(nextTask)).value();
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
        REQUIRE(next.IsStopped());
        REQUIRE(nextRoot.TryJoin());
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
    auto                currentTask = socket.ReceiveFromAsync(ctx, newBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(currentTask)::ErrorType> currentRoot(ctx.GetEnvironment(), 1);

    auto current      = currentRoot.Spawn(std::move(currentTask)).value();
    auto previousTask = std::move(oldTask);

    NGIN::Async::TaskSupervisor<typename decltype(previousTask)::ErrorType> previousRoot(ctx.GetEnvironment(), 1);

    auto previous = previousRoot.Spawn(std::move(previousTask)).value();
    scheduler.RunUntilIdle();
    const bool rejected     = previous.IsCompleted();
    const bool stillPending = !current.IsCompleted();
    socket.Close();
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(rejected);
    REQUIRE(previousRoot.TryJoin());
    auto outcome = previous.TakeResult();
    REQUIRE((outcome.HasError() && outcome.Error().IsDomainError()));
    REQUIRE(outcome.Error().DomainError().code == NetErrorCode::Disconnected);
    REQUIRE(stillPending);
    REQUIRE(current.IsStopped());
    REQUIRE(currentRoot.TryJoin());
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
    auto                      readingTask = client.ReceiveAsync(context, inbound);

    NGIN::Async::TaskSupervisor<typename decltype(readingTask)::ErrorType> readingRoot(context.GetEnvironment(), 1);

    auto reading = readingRoot.Spawn(std::move(readingTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(reading.IsCompleted());
    auto writingTask = client.SendAsync(context, payload);

    NGIN::Async::TaskSupervisor<typename decltype(writingTask)::ErrorType> writingRoot(context.GetEnvironment(), 1);

    auto       writing          = writingRoot.Spawn(std::move(writingTask)).value();
    const bool sent             = Pump(runtime, scheduler, [&] { return writing.IsCompleted(); });
    const bool readStillPending = !reading.IsCompleted();
    auto       received         = server.TryReceive(outbound);
    auto       reply            = server.TrySend(payload);
    const bool read             = Pump(runtime, scheduler, [&] { return reading.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(sent);
    REQUIRE(readStillPending);
    REQUIRE(writingRoot.TryJoin());
    REQUIRE(writing.TakeResult().Value() == payload.size());
    REQUIRE(received);
    REQUIRE(received.value() == payload.size());
    REQUIRE(outbound[0] == payload[0]);
    REQUIRE(reply);
    REQUIRE(read);
    REQUIRE(readingRoot.TryJoin());
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
    auto                      readingTask = receiver.ReceiveFromAsync(context, buffer);

    NGIN::Async::TaskSupervisor<typename decltype(readingTask)::ErrorType> readingRoot(context.GetEnvironment(), 1);

    auto reading = readingRoot.Spawn(std::move(readingTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(sender.TrySendTo(endpoint, oversized));
    const bool truncated = Pump(runtime, scheduler, [&] { return reading.IsCompleted(); });
    auto       nextTask  = receiver.ReceiveFromAsync(context, buffer);

    NGIN::Async::TaskSupervisor<typename decltype(nextTask)::ErrorType> nextRoot(context.GetEnvironment(), 1);

    auto next = nextRoot.Spawn(std::move(nextTask)).value();
    scheduler.RunUntilIdle();
    const std::array<Byte, 1> payload {Byte {23}};
    REQUIRE(sender.TrySendTo(endpoint, payload));
    const bool recovered = Pump(runtime, scheduler, [&] { return next.IsCompleted(); });
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(truncated);
    REQUIRE(readingRoot.TryJoin());
    const auto result = reading.TakeResult();
    REQUIRE((result.HasError() && result.Error().IsDomainError()));
    REQUIRE(result.Error().DomainError().code == NetErrorCode::MessageTooLarge);
    REQUIRE(recovered);
    REQUIRE(nextRoot.TryJoin());
    REQUIRE(next.TakeResult().Value().bytesReceived == 1);
    REQUIRE(buffer[0] == payload[0]);
}
#endif

TEST_CASE("Net pending socket completion transfers the task chain to its leaf executor", "[Net][Runtime][Placement]")
{
    IO::Runtime                     runtime;
    Execution::CooperativeScheduler source, target;
    Async::TaskContext              parentContext(source), leafContext(target);
    const auto                      targetRef = Execution::ExecutorRef::From(target);
    UdpSocket                       receiver(runtime), sender;
    REQUIRE(receiver.Open(AddressFamily::V4));
    REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
    REQUIRE(sender.Open(AddressFamily::V4));
    const auto                endpoint = BoundEndpoint(receiver.Handle());
    std::array<Byte, 4>       buffer {};
    const std::array<Byte, 2> payload {Byte {11}, Byte {42}};
    bool                      delivered = false;
    auto                      parent    = [&]() -> Async::Task<void> {
        auto result = co_await receiver.ReceiveFromAsync(leafContext, buffer).AsCompletion();
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().bytesReceived == payload.size());
        REQUIRE(targetRef.IsCurrent());
        auto environment = co_await Async::CurrentEnvironment();
        REQUIRE(environment.executor == targetRef);
        delivered = true;
    };
    auto operationTask = parent();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(parentContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    source.RunUntilIdle();
    target.RunUntilIdle();
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE(sender.TrySendTo(endpoint, payload));
    const bool completed = Pump(runtime, target, [&] { source.RunUntilIdle(); return operation.IsCompleted(); });
    runtime.Shutdown();
    target.RunUntilIdle();
    source.RunUntilIdle();
    REQUIRE(completed);
    REQUIRE(operationRoot.TryJoin());
    REQUIRE(operation.TakeResult().Succeeded());
    REQUIRE(delivered);
    REQUIRE(buffer[0] == payload[0]);
    REQUIRE(buffer[1] == payload[1]);
}

namespace
{
    struct SocketDeliveryCounter final
    {
        Execution::ExecutorRef executor;
        int                    deliveries {0};
        auto                   Execute(Execution::WorkItem work) noexcept { return executor.Execute(std::move(work)); }
        auto                   ExecuteAt(Execution::WorkItem work, Time::TimePoint time) noexcept
        {
            return executor.ExecuteAt(std::move(work), time);
        }
        auto ReserveCompletion(Execution::WorkItem work) noexcept
        {
            return executor.ReserveCompletion(Execution::WorkItem([this, work = std::move(work)]() mutable {
                ++deliveries;
                work.Invoke();
            }));
        }
        bool IsCurrent() const noexcept { return executor.IsCurrent(); }
    };
}// namespace

TEST_CASE("Net pending socket delivery inlines when eligible and retains reserved fallback", "[Net][Runtime][Dispatch]")
{
    for (std::size_t batch: {std::size_t(1), std::size_t(16)})
    {
        IO::Runtime           runtime({.batchSize = batch});
        SocketDeliveryCounter executor {runtime.GetExecutor()};
        Async::TaskContext    context(executor);
        UdpSocket             receiver(runtime), sender;
        REQUIRE(receiver.Open(AddressFamily::V4));
        REQUIRE(receiver.Bind({IpAddress::LoopbackV4(), 0}));
        REQUIRE(sender.Open(AddressFamily::V4));
        const auto                endpoint = BoundEndpoint(receiver.Handle());
        std::array<Byte, 4>       buffer {};
        const std::array<Byte, 2> payload {Byte {11}, Byte {42}};
        auto                      operationTask = receiver.ReceiveFromAsync(context, buffer);

        Execution::CooperativeScheduler ownerScheduler;
        Async::TaskContext              ownerContext(ownerScheduler);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(ownerContext.GetEnvironment(), 1);

        auto operation = operationRoot.SpawnOn(context.GetExecutor(), std::move(operationTask)).value();
        struct Drain final
        {
            IO::Runtime& runtime;
            ~Drain() { runtime.Shutdown(); }
        } drain {runtime};
        while (runtime.PollOnce()) {}
        REQUIRE_FALSE(operation.IsCompleted());
        const int before = executor.deliveries;
        REQUIRE(sender.TrySendTo(endpoint, payload));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
        {
            (void) runtime.PollOnce();
            ownerScheduler.RunUntilIdle();
            std::this_thread::yield();
        }
        runtime.Shutdown();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().bytesReceived == payload.size());
        REQUIRE(executor.deliveries - before == (batch == 1 ? 1 : 0));
        REQUIRE(buffer[0] == payload[0]);
        REQUIRE(buffer[1] == payload[1]);
    }
}
