# Network

`NGIN::Net` is a low-level non-blocking socket library with an explicit async driver.

Use it when you want:

- direct control over TCP or UDP sockets
- non-blocking `Try*` calls in your own loop
- coroutine-based async socket operations without hidden runtime threads
- transport adapters for byte streams or framed messages

The two main styles are:

- manual non-blocking:
  - open sockets
  - call `Try*`
  - handle `WouldBlock`
  - integrate with your own readiness loop
- coroutine async:
  - create a `NetworkDriver`
  - run or poll it
  - use `ConnectAsync`, `AcceptAsync`, `SendAsync`, and `ReceiveAsync`

## When To Use It

Use `NGIN::Net` when:

- you want explicit control over socket IO
- you already use the `NGIN::Async` task model
- you need a transport layer that stays close to the underlying socket behavior

## When Not To Use It

You probably do not need it when:

- a higher-level framework already owns networking in your application
- you want a batteries-included networking stack with its own hidden runtime
- you do not need non-blocking or coroutine-based networking

## Stability

- Stable and central:
  - low-level TCP/UDP socket wrappers
  - explicit `NetworkDriver`
  - `WouldBlock`-based non-blocking flow
- Usable and maturing:
  - higher-level transport guidance and end-to-end examples
  - some builder/adaptor ergonomics

## Which API Should I Use?

- Need raw TCP control:
  - use `TcpSocket`
- Need to accept incoming TCP connections:
  - use `TcpListener`
- Need UDP datagrams:
  - use `UdpSocket`
- Need coroutine-based socket async:
  - use a `NetworkDriver` plus the async socket methods
- Need byte-stream semantics on top of TCP:
  - use `TcpByteStream`
- Need framed messages:
  - use `LengthPrefixedMessageStream`

## Most Important Rule

Sockets are non-blocking by default.

That means:

- `Try*` methods may return `NetErrorCode::WouldBlock`
- `WouldBlock` means “not ready yet”, not “fatal error”
- async socket operations require a `NetworkDriver`

If you forget to run or poll the `NetworkDriver`, async network tasks will not make progress.

## Smallest Useful Examples

### Manual non-blocking TCP client

```cpp
NGIN::Net::TcpSocket socket;

auto opened = socket.Open();
if (!opened)
{
    return;
}

auto connect = socket.TryConnect(
    {NGIN::Net::IpAddress::LoopbackV4(), 9000});

if (!connect)
{
    if (connect.Error().code == NGIN::Net::NetErrorCode::WouldBlock)
    {
        // wait for writability in your own loop, then try again
        return;
    }

    return;
}
```

Use this style when you already have your own event loop or readiness model.

### Coroutine-based TCP client

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Async::TaskContext ctx(scheduler);
auto driver = NGIN::Net::NetworkDriver::Create({});

NGIN::Net::TcpSocket socket;
auto opened = socket.Open();
if (!opened)
{
    return;
}

auto task = [&]() -> NGIN::Async::Task<void, NGIN::Net::NetError>
{
    co_await socket.ConnectAsync(
        ctx,
        *driver,
        {NGIN::Net::IpAddress::LoopbackV4(), 9000},
        ctx.GetCancellationToken());
    co_return;
}();

task.Start(ctx);
while (!task.IsCompleted())
{
    driver->PollOnce();
    scheduler.RunUntilIdle();
}
```

Use this style when the rest of your code already uses `Task<T, E>`.

## Common Workflows

### Use `Try*` when you own the loop

The manual path is:

1. call `Open`
2. call a `Try*` method
3. if it succeeds, continue
4. if it returns `WouldBlock`, wait for readiness and try again
5. if it returns another error, handle failure

This applies to:

- `TryConnect`
- `TryAccept`
- `TrySend`
- `TryReceive`

### Use async methods when you already have a driver

The coroutine path is:

1. create a `NetworkDriver`
2. make sure it is being run or polled
3. create a `TaskContext`
4. call the async socket methods from tasks

Examples:

- `TcpSocket::ConnectAsync`
- `TcpSocket::SendAsync`
- `TcpSocket::ReceiveAsync`
- `TcpListener::AcceptAsync`

### Accept incoming TCP connections

Use `TcpListener` for a listening socket.

Manual flow:

- `Open`
- `Bind`
- `Listen`
- `TryAccept`

Async flow:

- same setup
- then `AcceptAsync(ctx, driver, token)`

### Use transport adapters at the right level

Use `TcpSocket` directly when you want raw socket control.

Use `TcpByteStream` when you want async read/write stream semantics.

Use `LengthPrefixedMessageStream` when your protocol is message-oriented and every message has a 32-bit big-endian
length prefix.

## Error Handling

There are two distinct styles:

### Manual non-blocking style

Use `NetExpected<T>` and branch on `NetErrorCode`.

Most importantly:

- `WouldBlock` means try again after readiness
- it is not the same thing as connection failure or EOF

### Coroutine async style

Use `Task<T, NetError>`.

At the root of the program or in tests:

- `TaskOutcome<T, NetError>::IsDomainError()` means a networking-domain failure
- `IsCanceled()` means cancellation
- `IsFault()` means async/runtime failure

Cancellation is not reported as `NetError`.

## `NetworkDriver` In Practice

`NetworkDriver` is the explicit async runtime for socket readiness.

You need it for:

- socket async methods
- transport adapters that depend on async socket operations

You do not need it for:

- plain `Try*` socket usage in your own loop

Operationally:

- `Create(options)` constructs the driver
- `Run()` blocks and drives the runtime continuously
- `PollOnce()` performs one readiness cycle
- `Stop()` ends a running driver loop

Choose `Run()` when the driver owns a thread or dedicated loop.
Choose `PollOnce()` when you want to integrate it into an existing loop.

## Common Mistakes

- Using async socket methods without a running or polled `NetworkDriver`.
- Treating `WouldBlock` as a fatal error.
- Reaching for transport adapters when raw sockets are the right level.
- Reaching for raw sockets when a byte-stream or message-stream adapter is the right level.
- Mixing manual non-blocking flow and coroutine flow without being clear which side owns readiness.

## Platform Notes

- Windows uses IOCP-backed async operations.
- Non-Windows platforms use readiness polling.
- All platforms expose non-blocking `Try*` APIs.

Platform differences should not change the basic usage model:

- manual non-blocking flow uses `Try*`
- coroutine async flow uses `NetworkDriver`

## Reference Notes

Important types:

- `TcpSocket`
- `TcpListener`
- `UdpSocket`
- `NetworkDriver`
- `TcpByteStream`
- `LengthPrefixedMessageStream`

For design notes and follow-up work, read [NetworkPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/NetworkPlan.md).
