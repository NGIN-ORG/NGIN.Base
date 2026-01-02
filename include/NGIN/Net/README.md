# NGIN::Net

Purpose & Scope:
- Low-level socket APIs with explicit async runtime.
- Optional transport layer for higher-level protocols.

Key Types:
- Addressing: AddressFamily, IpAddress, Endpoint.
- Errors: NetErrc, NetError, NetExpected.
- Buffers: Buffer, BufferPool<Allocator>, IOVec.
- Runtime: NetworkDriver.
- Sockets: TcpSocket, TcpListener, UdpSocket.

Usage Notes:
- Try* APIs are non-blocking and report errors via NetExpected.
- Async methods require TaskContext and NetworkDriver.

Performance Notes:
- No hidden threads; worker threads are explicit.
- No allocations on hot paths when BufferPool is used.

Testing Guidance:
- Loopback-only tests for TCP/UDP.
- Validate WouldBlock, EOF, and cancellation behavior.
