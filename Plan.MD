# NGIN.Base Networking - Plan

## Design goals

- Low overhead: no threads, allocations, or global init unless you explicitly create objects.
- Fast: non-blocking Try* APIs, pooled buffers, batch-friendly design.
- Modular: raw sockets first, optional transport/pipeline layer on top.
- Async-native: integrates with Task<T> via explicit TaskContext and NetworkDriver.
- Clear names: TryReceive, ReceiveAsync, SendAsync, AcceptAsync, ConnectAsync.

## Bleeding-edge targets

- Platform backends: IOCP (Windows), epoll (Linux), kqueue (macOS/BSD), optional io_uring for Linux when available.
- Batching: vectored I/O (TrySendV/TryReceiveV) and batch receive/send APIs to reduce syscalls.
- Zero-copy paths: explicit APIs for OS features like sendfile/TransmitFile/splice when supported.
- Buffering: pooled buffers with offset/length views; no allocations in hot paths.
- Measurable goals: latency/throughput targets plus benchmarks for Try* and async round trips.
- Usability without surprises: explicit blocking vs non-blocking, explicit driver threads, explicit cancellation/timeouts.
  
## Decisions to lock early

- Public API in headers; platform backends implemented in `src/NGIN/Net` and wired via small `detail` helpers.
- Async operations must take `NGIN::Async::TaskContext&` (or use it internally) so Tasks have an executor before awaiting.
- Use C++23 `std::expected` for error-return paths with `NetError` (alias in `NGIN::Net`).
- Non-blocking by default; blocking behavior is explicit via `Connect` or options.
- Use NGIN primitives and naming: `NGIN::UInt16`, `NGIN::UInt32`, `NGIN::Byte`.
- Define Try* contract now (partial progress, EOF rules, WouldBlock mapping) to prevent churn.
- Socket moves are allowed only when no driver registration is active (or use stable internal state); this must be explicit.

---

## Package layout

### Namespaces

- `NGIN::Net` public API
- `NGIN::Net::detail` internal helpers
- `NGIN::Net::Platform` OS-specific headers (gated by `#if`)
- `NGIN::Net::Transport` optional composition layer (filters/pipelines)

### Files (conceptual)

```
include/NGIN/Net/
  README.md
  Types/
    AddressFamily.hpp
    IpAddress.hpp
    Endpoint.hpp
    NetError.hpp
    Buffer.hpp
    BufferPool.hpp
    SocketOptions.hpp
  Runtime/
    NetworkDriver.hpp
    Awaitables.hpp
  Sockets/
    SocketHandle.hpp
    UdpSocket.hpp
    TcpSocket.hpp
    TcpListener.hpp
    DnsResolver.hpp (optional)
  Transport/
    IByteStream.hpp
    IDatagramChannel.hpp
    ByteStreamBuilder.hpp
    DatagramBuilder.hpp
    Filters/
      LengthPrefixedFraming.hpp
      TlsStream.hpp (optional module)
      CompressionStream.hpp (optional module)
  Platform/
    SocketPlatform.hpp
    PollerEpoll.hpp
    PollerKqueue.hpp
    PollerIocp.hpp
src/NGIN/Net/
  NetworkDriver.cpp
  SocketPlatform.cpp
  PollerEpoll.cpp
  PollerKqueue.cpp
  PollerIocp.cpp
```

---

## Core types

### Addressing

```cpp
namespace NGIN::Net {
    enum class AddressFamily : NGIN::UInt8 { V4, V6, DualStack };

    struct IpAddress {
        // v4/v6 storage, parsing, and to-string helpers
    };

    struct Endpoint {
        IpAddress address;
        NGIN::UInt16 port {0};
    };
}
```

### Errors (cheap + explicit)

```cpp
namespace NGIN::Net {
    enum class NetErrc : NGIN::UInt8 {
        Ok,
        WouldBlock,
        TimedOut,
        Disconnected,
        ConnectionReset,
        HostUnreachable,
        MessageTooLarge,
        PermissionDenied,
        Unknown,
    };

    struct NetError {
        NetErrc code {NetErrc::Ok};
        int native {0};
    };

    template<typename T>
    using NetExpected = std::expected<T, NetError>;
}
```

### Buffers (span-first, pool-backed ownership)

```cpp
namespace NGIN::Net {
    using ByteSpan = std::span<NGIN::Byte>;
    using ConstByteSpan = std::span<const NGIN::Byte>;
    struct BufferSegment { const NGIN::Byte* data; NGIN::UInt32 size; };
    using BufferSegmentSpan = std::span<const BufferSegment>;

    struct Buffer {
        NGIN::Byte* data {nullptr};
        NGIN::UInt32 size {0};
        NGIN::UInt32 capacity {0};
        // move-only, returns to pool on destruction
    };

    template<NGIN::Memory::AllocatorConcept Allocator = NGIN::Memory::SystemAllocator>
    class BufferPool {
    public:
        explicit BufferPool(Allocator allocator = {});
        Buffer Rent(NGIN::UInt32 minimumCapacity);
    };
}
```

---

## Runtime: NetworkDriver

`NetworkDriver` is the explicit async runtime for sockets. It does not own or imply a scheduler. It only
waits for OS I/O readiness and uses the `ExecutorRef` from the caller's `TaskContext` to resume work.

```cpp
namespace NGIN::Net {
    struct NetworkDriverOptions {
        NGIN::UInt32 workerThreads {0}; // 0 = manual pumping only
        bool busyPoll {false};
        NGIN::Units::Milliseconds pollInterval {1.0};
    };

    class NetworkDriver {
    public:
        static std::unique_ptr<NetworkDriver> Create(NetworkDriverOptions);

        void Run();      // blocks if workerThreads > 0
        void PollOnce(); // manual pump
        void Stop();

        // Awaitables for async socket ops
        NGIN::Async::Task<void> WaitUntilReadable(NGIN::Async::TaskContext& ctx,
                                                  class SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token);
        NGIN::Async::Task<void> WaitUntilWritable(NGIN::Async::TaskContext& ctx,
                                                  class SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token);
    };
}
```

Notes:

- `WaitUntilReadable/Writable` are the building blocks; async socket methods wrap these plus Try* calls.
- Worker threads, if configured, should use `NGIN::Execution::Thread` and named `NGIN.NetW.<i>`.
- OS-specific code lives in `src/NGIN/Net` with small `detail` headers to select the backend per platform.

---

## Socket layer (thin, direct, fast)

### Shared socket handle

```cpp
namespace NGIN::Net {
    class SocketHandle {
    public:
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;
        // minimal native handle access if needed (careful, platform-gated)
    };
}
```

### UDP: UdpSocket

```cpp
namespace NGIN::Net {
    struct DatagramReceiveResult {
        Endpoint remoteEndpoint;
        NGIN::UInt32 bytesReceived {0};
    };

    class UdpSocket {
    public:
        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack) noexcept;
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        NetExpected<void> Connect(Endpoint remoteEndpoint) noexcept; // optional connected-UDP
        void Close() noexcept;

        NetExpected<NGIN::UInt32> TrySendTo(Endpoint remoteEndpoint, ConstByteSpan payload) noexcept;
        NetExpected<DatagramReceiveResult> TryReceiveFrom(ByteSpan destination) noexcept;

        NGIN::Async::Task<NGIN::UInt32> SendToAsync(NGIN::Async::TaskContext& ctx,
                                                   NetworkDriver& driver,
                                                   Endpoint remoteEndpoint,
                                                   ConstByteSpan payload,
                                                   NGIN::Async::CancellationToken token);
        NGIN::Async::Task<DatagramReceiveResult> ReceiveFromAsync(NGIN::Async::TaskContext& ctx,
                                                                  NetworkDriver& driver,
                                                                  ByteSpan destination,
                                                                  NGIN::Async::CancellationToken token);

        SocketHandle& Handle() noexcept;
    };
}
```

### TCP: TcpSocket + TcpListener

```cpp
namespace NGIN::Net {
    class TcpListener {
    public:
        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack) noexcept;
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        NetExpected<void> Listen(NGIN::Int32 backlog = 128) noexcept;
        NetExpected<class TcpSocket> TryAccept() noexcept; // non-blocking

        NGIN::Async::Task<class TcpSocket> AcceptAsync(NGIN::Async::TaskContext& ctx,
                                                       NetworkDriver& driver,
                                                       NGIN::Async::CancellationToken token);

        void Close() noexcept;
        SocketHandle& Handle() noexcept;
    };

    class TcpSocket {
    public:
        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack) noexcept;

        // Non-blocking connect
        NetExpected<bool> TryConnect(Endpoint remoteEndpoint) noexcept; // true when connected
        NGIN::Async::Task<void> ConnectAsync(NGIN::Async::TaskContext& ctx,
                                             NetworkDriver& driver,
                                             Endpoint remoteEndpoint,
                                             NGIN::Async::CancellationToken token);

        // Explicit blocking helper (optional, separate from core API)
        NetExpected<void> Connect(Endpoint remoteEndpoint);

        NetExpected<NGIN::UInt32> TrySend(ConstByteSpan data) noexcept;
        NetExpected<NGIN::UInt32> TryReceive(ByteSpan destination) noexcept;

        NGIN::Async::Task<NGIN::UInt32> SendAsync(NGIN::Async::TaskContext& ctx,
                                                 NetworkDriver& driver,
                                                 ConstByteSpan data,
                                                 NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32> ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                    NetworkDriver& driver,
                                                    ByteSpan destination,
                                                    NGIN::Async::CancellationToken token);

        NetExpected<void> Shutdown(ShutdownMode mode) noexcept;
        void Close() noexcept;
        SocketHandle& Handle() noexcept;
    };
}
```

Naming notes:

- TryReceive/TrySend are explicit non-blocking fast paths.
- Async variants are ReceiveAsync/SendAsync.
- Listener uses TryAccept/AcceptAsync.

---

## API contract (lock semantics now)

- Try* never throws; it returns `NetExpected` with `NetErrc::WouldBlock` for non-ready sockets.
- TrySend/TryReceive may return partial progress; `Ok` with `bytes > 0` means progress.
- TCP EOF: `TryReceive` returns `Ok` with `bytes == 0` (not `Disconnected`). `Disconnected` is for hard errors.
- UDP: zero-length payload is allowed; `TryReceiveFrom` returns `bytesReceived == 0` only if the datagram is empty.
- `WouldBlock` is the only signal that it is safe to wait and retry without treating it as an error.
- Cancellation: async APIs propagate `TaskCanceled` from `TaskContext` (do not map to `NetErrc`).
- Socket moves: not allowed while registered with a `NetworkDriver` unless registration is tied to stable internal state.

## Optional transport layer (composition, testability, features)

Sockets are for performance. Transports are for app ergonomics, filters, and tests.

### Stream abstraction: IByteStream

```cpp
namespace NGIN::Net::Transport {
    class IByteStream {
    public:
        virtual ~IByteStream() = default;
        virtual NGIN::Async::Task<NGIN::UInt32> ReadAsync(NGIN::Async::TaskContext& ctx,
                                                         NGIN::Net::ByteSpan destination,
                                                         NGIN::Async::CancellationToken token) = 0;
        virtual NGIN::Async::Task<NGIN::UInt32> WriteAsync(NGIN::Async::TaskContext& ctx,
                                                          NGIN::Net::ConstByteSpan source,
                                                          NGIN::Async::CancellationToken token) = 0;
        virtual NetExpected<void> Close() = 0;
    };
}
```

### Datagram abstraction: IDatagramChannel

```cpp
namespace NGIN::Net::Transport {
    struct ReceivedDatagram {
        NGIN::Net::Endpoint remoteEndpoint;
        NGIN::Net::ConstByteSpan payload;
        NGIN::UInt32 bytesReceived {0};
    };

    class IDatagramChannel {
    public:
        virtual ~IDatagramChannel() = default;

        virtual NGIN::Async::Task<void> SendAsync(NGIN::Async::TaskContext& ctx,
                                                  NGIN::Net::Endpoint remoteEndpoint,
                                                  NGIN::Net::ConstByteSpan payload,
                                                  NGIN::Async::CancellationToken token) = 0;

        virtual NGIN::Async::Task<ReceivedDatagram> ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                                 NGIN::Net::Buffer& receiveBuffer,
                                                                 NGIN::Async::CancellationToken token) = 0;
    };
}
```

### Adapters

- TcpByteStream wraps TcpSocket
- UdpDatagramChannel wraps UdpSocket + BufferPool

---

## Filters and pipelines (opt-in)

Filters wrap IByteStream or IDatagramChannel and add semantics. They may allocate; this is intentional
and only paid by users who opt in.

Example: length-prefixed framing

```cpp
class LengthPrefixedMessageStream {
public:
    explicit LengthPrefixedMessageStream(std::unique_ptr<IByteStream> inner);

    NGIN::Async::Task<void> WriteMessageAsync(NGIN::Async::TaskContext& ctx,
                                              NGIN::Net::ConstByteSpan message,
                                              NGIN::Async::CancellationToken token);
    NGIN::Async::Task<NGIN::Net::ConstByteSpan> ReadMessageAsync(NGIN::Async::TaskContext& ctx,
                                                                 NGIN::Net::Buffer& messageBuffer,
                                                                 NGIN::Async::CancellationToken token);
};
```

Builder (discoverable)

```cpp
auto stream =
    Net::Transport::ByteStreamBuilder()
        .FromTcpSocket(std::move(tcpSocket), driver)
        .WithLengthPrefixedMessages()
        .WithTls(tlsOptions)
        .WithCompression(compression)
        .Build();
```

---

## Coroutine integration model (simple + fast)

Async socket ops are implemented as:

1. attempt TryReceive/TrySend
2. if WouldBlock, co_await driver.WaitUntilReadable/Writable(ctx, handle, token)
3. try again

This yields:

- minimal branching
- no hidden threads
- consistent cancellation handling

---

## Usage patterns

### 1) High-performance polling (no driver threads)

```cpp
Execution::CooperativeScheduler scheduler;
Async::TaskContext ctx(scheduler);

auto driver = NetworkDriver::Create({.workerThreads = 0});

UdpSocket socket;
socket.Open();
socket.Bind({IpAddress::AnyV6(), 9000});

auto buffer = pool.Rent(2048);

while (running) {
    for (;;) {
        auto result = socket.TryReceiveFrom({buffer.data, buffer.capacity});
        if (!result || result.error().code == NetErrc::WouldBlock) {
            break;
        }
        HandleDatagram(result->remoteEndpoint, {buffer.data, result->bytesReceived});
    }

    driver->PollOnce();
    scheduler.RunUntilIdle();
}
```

### 2) Coroutine receive loop

```cpp
Async::Task<void> ReceiveLoop(Async::TaskContext& ctx,
                              NetworkDriver& driver,
                              UdpSocket& socket,
                              BufferPool& pool,
                              Async::CancellationToken token)
{
    auto buffer = pool.Rent(2048);

    while (!token.IsCancellationRequested()) {
        auto r = co_await socket.ReceiveFromAsync(ctx, driver, {buffer.data, buffer.capacity}, token);
        Process(r.remoteEndpoint, {buffer.data, r.bytesReceived});
    }
}
```

### 3) Clean app-level stream with framing

```cpp
auto byteStream =
    Net::Transport::ByteStreamBuilder()
        .FromTcpSocket(std::move(sock), driver)
        .WithLengthPrefixedMessages()
        .Build();

Buffer msg = pool.Rent(64 * 1024);
auto payload = co_await byteStream.ReadMessageAsync(ctx, msg, token);
```

---

## Zero overhead if not used

- No global driver instance.
- No static initialization.
- Async only exists if NetworkDriver + async methods are used.
- Transport/filter overhead only exists if the transport layer is used.

---

## Tests and verification

- Positive and negative tests for each socket type: Open/Bind/Connect/Try* and error mapping.
- Cancellation tests for WaitUntilReadable/WaitUntilWritable and async send/receive.
- Loopback-only integration tests (TCP and UDP) to avoid external network dependencies.
- Benchmarks if performance is a focus: TrySend/TryReceive and async latency.
