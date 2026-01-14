# Network

NGIN::Net is the low-level networking layer for NGIN. It provides socket wrappers, a small set of value types, and an
explicit async runtime. The public headers define the API; platform-specific work lives in src.

The key idea is to keep IO explicit and deterministic: non-blocking Try* calls for fast paths, and coroutine-friendly
async calls that are driven by an explicit NetworkDriver. There are no hidden global threads or background runtimes.

## Design Goals

- Explicit runtime control: no hidden threads or globals; you drive or run NetworkDriver yourself.
- Non-blocking by default: sockets open in non-blocking mode and Try* calls surface WouldBlock.
- Clear error boundaries: NetExpected for fast-path results, std::system_error for async failures.
- Minimal allocations: buffer pooling and stack-first I/O vectoring for hot paths.
- Portable async: IOCP on Windows and readiness polling on other platforms.

## How It Fits Together

- Use TcpSocket or UdpSocket directly for low-level, non-blocking operations.
- Use NetworkDriver to wait for readability/writability in async workflows.
- Wrap sockets in transport adapters (TcpByteStream, UdpDatagramChannel) for higher-level protocols.
- Optionally layer filters such as LengthPrefixedMessageStream for framing.

## Core Types and Errors

### AddressFamily

Selects the address family when opening sockets: V4, V6, or DualStack. DualStack chooses IPv6 sockets that can also
accept IPv4 where the platform supports it.

### IpAddress

Value type that stores IPv4 or IPv6 bytes in a fixed 16-byte array. This keeps the type trivially copyable and avoids
variant allocations. Bytes() returns a span sized to the active family (4 or 16). Helpers like AnyV4/AnyV6 and
LoopbackV4/LoopbackV6 provide common addresses.

### Endpoint

Simple pair of IpAddress and port. It is passed by value through the API to keep call sites explicit and avoid hidden
DNS or resolver behavior.

### NetErrorCode, NetError, NetExpected, ToErrorCode

- NetErrorCode is a compact, library-level error enum used for fast-path decisions.
- NetError pairs NetErrorCode with an optional native error code for diagnostic detail.
- NetExpected<T> is std::expected<T, NetError>, used by Try* APIs.
- ToErrorCode prefers native error codes when present, otherwise maps NetErrorCode to std::errc for std::system_error.

This split keeps Try* fast and allocation-free while preserving OS error details when needed.

### SocketOptions

Common socket option flags used during Open/Bind. Defaults favor non-blocking sockets and address reuse. The v6Only
flag is honored for IPv6 sockets; when AddressFamily::V6 is requested it is forced on.

### ShutdownMode

Used for TcpSocket::Shutdown to select receive, send, or both directions.

### BufferSegment, MutableBufferSegment, and spans

Small POD descriptors for scatter/gather IO. They map directly to WSABUF or iovec/recvmsg usage, avoiding per-call
allocations. Spans (BufferSegmentSpan, MutableBufferSegmentSpan) capture arrays of segments.

### Buffer

Move-only owning buffer with an optional release callback. This design lets buffers be returned to a pool without
allocations or virtual dispatch. Buffer::Release is idempotent and resets the handle; BufferPool sets the release
function so releasing returns the memory to the pool.

### BufferPool<Allocator>

A simple, non-thread-safe pool that rents and reuses buffers. It uses an allocator (SystemAllocator by default) and
returns Buffer objects that know how to return themselves to the pool. The pool is not thread-safe to keep hot paths
lean; use one pool per thread or add external synchronization.

## Socket Layer

### SocketHandle

RAII wrapper over the native socket handle. It owns the lifetime, closes on destruction, and supports move-only
transfer. It intentionally exposes Native() for platform calls and keeps InvalidHandle() internal.

### TcpSocket

Non-blocking TCP wrapper with Try* operations and async helpers.

- Open() creates a socket with SocketOptions applied.
- TryConnect() returns WouldBlock when a non-blocking connect is in progress.
- ConnectAsync() uses ConnectEx on Windows and readiness polling elsewhere.
- Send/Receive have both byte-span and segment-based overloads using WSASend/WSARecv or writev/readv.
- Connect() temporarily switches to blocking mode for a synchronous call, then restores non-blocking state.

### TcpListener

Non-blocking TCP listener.

- Open()/Bind()/Listen() configure the listen socket.
- TryAccept() returns WouldBlock when no connection is ready.
- AcceptAsync() uses AcceptEx on Windows and readiness polling elsewhere.
- Accepted sockets are set to non-blocking mode.

### UdpSocket

Non-blocking UDP wrapper with both connected and unconnected workflows.

- Open()/Bind()/Connect() mirror the OS primitives.
- TrySendTo/TryReceiveFrom work on address+payload pairs.
- Segment variants use WSASendTo/WSARecvFrom or sendmsg/recvmsg.
- Async methods submit IOCP work on Windows and use readiness polling elsewhere.

### DatagramReceiveResult

Result type for UdpSocket receive operations. It carries the remote endpoint and bytes received.

## Runtime

### NetworkDriverOptions

Options for the explicit runtime:

- workerThreads: 0 runs the driver on the current thread; non-zero spawns worker threads that call PollOnce.
- busyPoll: when true, polling uses a zero timeout for low latency.
- pollInterval: idle wait duration when busyPoll is false.

### NetworkDriver

The explicit async runtime used by socket awaitables.

- Create() constructs a driver with options; the constructor is private to enforce explicit creation.
- Run() loops until Stop() is called. PollOnce() performs a single readiness cycle.
- WaitUntilReadable/Writable provide awaitables for non-Windows async loops.
- On Windows, IOCP-backed submit methods are used by TcpSocket/UdpSocket for overlapped send/receive/connect/accept.
- On non-Windows platforms, PollOnce uses epoll (Linux), kqueue (BSD/macOS), or select as a fallback.

Cancellation integrates with the async system: waiters unregister themselves, IOCP operations are canceled with
CancelIoEx, and awaiting tasks observe AsyncErrorCode::Canceled.

## Transport Layer

### IByteStream

Abstract async byte-stream interface used by higher-level protocols. Implementations must provide ReadAsync,
WriteAsync, and Close. Errors are surfaced via AsyncExpected from async methods and via NetExpected from Close.

### TcpByteStream

Adapter that implements IByteStream over a TcpSocket. It forwards ReadAsync/WriteAsync to the socket and requires a
NetworkDriver. A missing driver is treated as a logic error because the adapter depends on it for async scheduling.

### IDatagramChannel

Abstract async datagram interface for message-based transports. SendAsync sends a payload to a remote endpoint; the
ReceiveAsync result is a ReceivedDatagram containing the payload view.

### ReceivedDatagram

Result type for IDatagramChannel receives. The payload is a view into the provided Buffer, so the Buffer must outlive
the returned span.

### UdpDatagramChannel

Adapter that implements IDatagramChannel over a UdpSocket. It uses a user-provided Buffer for receives and returns a
payload span that points into that buffer to avoid allocations.

### ByteStreamBuilder

Builder for stream adapters. It binds a TcpSocket and NetworkDriver, then produces either a TcpByteStream or a
LengthPrefixedMessageStream. Build() returns NetExpected<std::unique_ptr<...>> and clears the stored socket to avoid
accidental reuse.

### DatagramBuilder

Builder for datagram adapters. It binds a UdpSocket and NetworkDriver, then produces a UdpDatagramChannel. Build()
returns NetExpected<std::unique_ptr<...>> and resets internal state after use.

### Filters::LengthPrefixedMessageStream

A framing filter over IByteStream that prepends a 32-bit big-endian length prefix to each message.

- WriteMessageAsync() writes the length header followed by the payload.
- ReadMessageAsync() reads exactly 4 bytes for the header and then reads the declared payload length.
- The caller supplies the Buffer used for message storage to avoid allocations.
- Zero-length messages are supported and return an empty span.

The length prefix is big-endian to match common network conventions. Read/Write are strict: if the stream returns
zero bytes mid-transfer, the filter returns AsyncErrorCode::Fault to avoid silent truncation.

## Platform Notes

- Windows: IOCP is used for async IO. AcceptEx and ConnectEx are loaded lazily and used for accept/connect.
- Non-Windows: sockets stay non-blocking; async methods loop Try* calls and wait for readiness via NetworkDriver.
- All platforms: Try* APIs return NetExpected with WouldBlock for in-progress operations.

## Usage Sketch

TCP client with length-prefixed framing:

```cpp
auto driver = NGIN::Net::NetworkDriver::Create({});
NGIN::Net::TcpSocket socket;
NGIN::Net::Endpoint endpoint {NGIN::Net::IpAddress::LoopbackV4(), 9000};

socket.Open();
// ConnectAsync uses TaskContext and NetworkDriver.
```

The expected flow is:

1. Create a NetworkDriver and run it (Run() on a thread, or PollOnce() manually).
2. Open/bind/connect sockets and use Try* calls for non-blocking integration.
3. Use async methods from coroutines with a TaskContext bound to the driver.
4. Wrap sockets in transport adapters when you want byte-stream or message semantics.

