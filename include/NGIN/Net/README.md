# NGIN::Net

Purpose & Scope:
- Low-level socket APIs with explicit async runtime.
- Optional transport layer for higher-level protocols.

Key Types:
- Addressing: AddressFamily, IpAddress, Endpoint.
- Errors: NetErrorCode, NetError, NetExpected.
- Buffers: Buffer, BufferPool<Allocator>, BufferSegment.
- Runtime: NetworkDriver.
- Sockets: TcpSocket, TcpListener, UdpSocket.
- Transport: IByteStream, IDatagramChannel, TcpByteStream, UdpDatagramChannel, ByteStreamBuilder, DatagramBuilder.
- Filters: LengthPrefixedMessageStream (message framing over IByteStream).

Usage Notes:
- Try* APIs are non-blocking and report errors via NetExpected.
- Async APIs return AsyncExpected from `co_await`/`Get()`; cancellation uses AsyncErrorCode::Canceled.
- Async methods require TaskContext and NetworkDriver.
- Filters wrap transports to add semantics like framing, TLS, compression, or metrics.
- Length-prefixed framing uses a 32-bit big-endian size prefix per message.
- ByteStreamBuilder::BuildLengthPrefixed() returns NetExpected<std::unique_ptr<LengthPrefixedMessageStream>>.

Performance Notes:
- No hidden threads; worker threads are explicit.
- No allocations on hot paths when BufferPool is used.

Testing Guidance:
- Loopback-only tests for TCP/UDP.
- Validate WouldBlock, EOF, and cancellation behavior.

