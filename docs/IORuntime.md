# Shared I/O runtime

`NGIN::IO::Runtime` owns the services that make asynchronous local-file and
socket operations progress. Include `<NGIN/IO/Runtime.hpp>` and link
`NGIN::Base::IO`; socket operations additionally require `NGIN::Base::Net`.
An IO-only executable does not need to link networking.

## Bind resources once

```cpp
NGIN::Execution::ThreadPoolScheduler tasks(1);
NGIN::IO::Runtime io;
NGIN::IO::LocalFileSystem files(io);
NGIN::Net::TcpSocket socket(io);
NGIN::Async::CancellationSource cancellation;
NGIN::Async::TaskContext ctx(tasks, cancellation.GetToken());

// Inside a coroutine:
co_await socket.ConnectAsync(ctx, endpoint);
auto count = co_await socket.ReceiveAsync(ctx, buffer);
co_await files.CopyFileAsync(ctx, source, destination);
```

The runtime handles I/O progress. The context selects the continuation executor
and cancellation. Socket methods optionally accept an additional token, linked
with context cancellation. A thread-safe executor is required when background
I/O posts completions; `ThreadPoolScheduler` supports this arrangement.

`TcpListener(io)` and `UdpSocket(io)` follow the same binding model. Accepted
TCP sockets inherit the listener's runtime, including through `TryAccept`.
Moving a socket or wrapping it in `TcpByteStream`, `UdpDatagramChannel`, or a
transport builder preserves the binding. Adapters no longer take a driver.
For asynchronous virtual mounts, use `LocalMount(io, realRoot, mountPoint)`.

Default-constructed sockets and filesystems remain available for synchronous
and `Try*` operations. Async use without a runtime reports
`AsyncFaultCode::InvalidTaskUsage`. Resource binding borrows the runtime; it
must outlive its resources and operations. This is not shared ownership of the
runtime object.

## Lazy services and options

Constructing a runtime, binding resources, and using synchronous operations
start no backend workers. The first file operation initializes one shared file
service. The first socket async operation initializes one shared network service.
A networking-only application starts no filesystem workers. Independent
runtimes have independent services and shutdown states.

```cpp
NGIN::IO::Runtime io({
    .files = {.workerThreads = 2,
              .backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback},
    .network = {.mode = NGIN::IO::Runtime::NetworkMode::Background}
});
```

File backend selection supports `Auto`, `Native`, and `Fallback`. `Auto` tries
native io_uring on Linux or IOCP on Windows, then uses workers if unavailable.
`Native` does not silently fall back when native initialization fails. Unsupported
path/directory operations still use file workers when the service is available.
`GetFileBackend()` reports the selection; before initialization it returns
`FileBackend::None`. `HasFileBackend()` and `HasNetworkBackend()` report whether
the corresponding service has been initialized, not whether tasks are pending.

`FileOptions` requires nonzero `workerThreads` and `queueDepthHint`.
`NetworkOptions::pollInterval` must be finite and at least one millisecond.
Invalid configuration throws `std::invalid_argument`.

## Background and manual networking

`Background` is the default: the runtime starts and owns one polling thread on
first async socket use. Applications do not need to call `Run()` or `PollOnce()`.
The filesystem service independently owns its native completion machinery and
fallback workers; sharing a runtime does not mean forcing both services onto
one thread.

For an externally driven network loop, select `NetworkMode::Manual` and call
`io.PollOnce()` regularly, or run `io.Run()` on one application-owned thread.
`Run()` waits for lazy network initialization if called before the first async
socket operation. `Stop()` wakes it even if no network service was initialized.
`PollOnce()` is nonblocking and does nothing before network initialization.
Do not run multiple polling calls concurrently. Both methods reject Background
mode with `std::logic_error`.

## Shutdown

1. Stop submitting application work.
2. Request cancellation where needed and await every operation's terminal result.
3. Release the operation's sockets, handles, buffers, and borrowed contexts.
4. Call `io.Stop()`, then join any application-owned `Run()` thread.
5. Destroy the runtime before the continuation scheduler.

`Stop()` is permanent, idempotent, and safe to call from another thread. It
rejects new async work and stops/joins the owned network thread. It does not
cancel or drain pending application tasks: they must complete first, while
polling and their continuation executors can still progress. Synchronous
operations remain usable after Stop. Backends are released with their owners;
Stop is not an immediate release of all filesystem workers.
