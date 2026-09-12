# Shared I/O runtime

`NGIN::IO::Runtime` owns an event loop for socket readiness, file completions,
timers, and queued continuations. Include `<NGIN/IO/Runtime.hpp>` and link
`NGIN::Base::IORuntime`. Filesystem operations additionally require
`NGIN::Base::IO`; sockets require `NGIN::Base::Net`. Net does not link filesystem
implementation, and IORuntime does not reference either service factory.

The broader refactor is still in progress; see
[implementation status](IORuntimeImplementation.md) for unfinished task cleanup,
resource concurrency, and platform integration work.

## Resources and execution

```cpp
NGIN::IO::Runtime io;
NGIN::IO::RuntimeRunner runner(io); // Include <NGIN/IO/RuntimeRunner.hpp>.
NGIN::Async::TaskContext ctx(io.GetExecutor());
NGIN::IO::LocalFileSystem files(io);
NGIN::Net::TcpSocket socket(io);

// Inside a coroutine started with ctx:
co_await socket.ConnectAsync(ctx, endpoint);
auto count = co_await socket.ReceiveAsync(ctx, buffer);
co_await files.CopyFileAsync(ctx, source, destination);
```

A TaskContext independently chooses its executor and cancellation token. It may
select the runtime executor or an external scheduler. I/O reserves terminal
completion capacity on that executor before backend admission. Unsupported
executors report a scheduling fault before starting the operation.
ThreadPoolScheduler and CooperativeScheduler support reserved delivery; the
cooperative scheduler's ordinary queue remains owned by its pumping thread.

Runtime executor submission queues work and never invokes it inline.
`ExecutorRef::IsCurrent()` identifies callback execution, rather than merely
being on the owner's thread. `YieldNow()` yields to queued work. `Delay()` uses
the runtime's removable monotonic timer queue; cancellation promptly removes
its queued record and terminal completion releases its frame lease.

`TcpListener(io)`, `UdpSocket(io)`, and `LocalMount(io, realRoot, mountPoint)`
use the same borrowed runtime model. Accepted TCP sockets inherit the listener
binding. Moving sockets or wrapping them in transport adapters preserves it.
The runtime must outlive resources and their operations. Default-constructed
resources remain available for synchronous and Try operations; async use
without a runtime reports `AsyncFaultCode::InvalidTaskUsage`.

Socket tasks capture stable handle state when created. Moving the public
socket afterward preserves both cold and pending tasks. Close or destruction
prevents further admission, cancels pending operations, and defers native release
until backend access ends. Buffers and task contexts must remain alive until the
operation completes. Moves and ordinary synchronous methods still require caller
synchronization with other member calls.

TCP allows one async read and one async write concurrently; UDP allows one
receive and one send. Same-direction overlap and overlapping listener accepts
report `NetErrorCode::OperationInProgress`. Connect reserves both TCP directions. Canceling an in-flight connect
closes that socket so later operations cannot inherit an unobserved connection.
Admission covers the entire operation, including immediate native completion
and readiness retries. Async operations reject blocking sockets with
`InvalidArgument`; the default socket options are nonblocking. Completion and
operation budgets, plus registration storage, are secured before native I/O.

## Driving and thread ownership

Construction starts no threads and does not bind ownership. The first `Run()`,
`PollOnce()`, `Shutdown()`, RunTask, or RuntimeRunner binds the runtime permanently to
its driving thread. Concurrent, reentrant, and later off-owner drive calls
throw `std::logic_error`.

`Run()` drives until shutdown has drained, sleeping on a platform notification
when no work is ready. `PollOnce()` performs one bounded, nonblocking batch and
returns whether immediately ready work remains. Keep pumping while it returns
true. False is a snapshot, not a promise that no producer will submit later.
The loop rotates submissions, completions, timers, readiness, and shutdown work
across batches. A long callback still occupies its thread; move CPU-heavy or
blocking application work to an external executor.

`RuntimeRunner` explicitly owns one Run thread. Its constructor waits until
ownership is established and reports conflicts synchronously. Its destructor
requests shutdown and joins. The runtime must outlive the runner. Call
`runner.Shutdown()` explicitly when an event-loop failure must be reported to
the controlling thread; destruction terminates on an unhandled driver failure.

Blocking `SyncWait()` on the only event-loop thread prevents I/O progress.
It can be used from an application thread while a runner drives the runtime.
For a standalone application, include `<NGIN/IO/RunTask.hpp>` and use:

```cpp
NGIN::IO::Runtime io;
auto result = NGIN::IO::RunTask(io,
    [](NGIN::Async::TaskContext& ctx, NGIN::Async::TaskScope<>& scope)
        -> NGIN::Async::Task<int> {
        co_await ctx.Delay(NGIN::Units::Milliseconds(1));
        co_return 42;
    });
// io is Stopped; result is Completion<int, NoError>.
```

`RunTask<E>` accepts a factory `(TaskContext&, TaskScope<E>&) -> Task<T, E>`.
It owns runtime shutdown, cancels and joins remaining children on every root
outcome, and returns root failure first, otherwise child failure or root success.
A shutdown notification is reserved before starting the supervisor, so even a
supervisor fault cannot lose the stop signal. RunTask's context and factory stay
alive until joining and shutdown finish. The optional cancellation token reaches
the root and every scoped child; the optional child capacity defaults to 256.

The root may explicitly join its scope; RunTask consumes the scope outcome after
its final join. Join children borrowing root-local variables before those variables
leave scope. Use owned factory captures or caller-owned data for children that
remain active when the root returns. See [task ownership](Async.md#owned-child-tasks)
for cancellation, borrowed results, and scope destruction rules.

## Lazy services and bounds

```cpp
NGIN::IO::Runtime io({
    .files = {.workerThreads = 2,
              .queueDepthHint = 1024,
              .backendPreference = NGIN::IO::Runtime::FileBackendPreference::Auto},
    .submissionCapacity = 4096,
    .timerCapacity = 4096,
    .completionCapacity = 8192,
    .operationCapacity = 4096,
    .registrationCapacity = 4096,
    .batchSize = 64
});
```

All capacities and batch size are positive. Invalid configuration throws
`std::invalid_argument`; platform wait initialization may throw
`std::system_error`. Queue saturation returns `ScheduleError::ResourceExhausted`
without blocking the event-loop thread. Accepted operations retain reserved
completion storage independently of ordinary submissions.
Capacities count admitted entries, not bytes. The memory footprint also depends
on coroutine frames, callback captures, application buffers, and the capacities
of external executors. The [allocation measurements](IORuntimeMeasurements.md#allocation-and-overload-checkpoint-2026-09-11)
report observed storage for fixed workloads; they are not a byte limit on arbitrary
application payloads.

File selection supports Auto, Native, and Fallback. Auto permits worker fallback
when native initialization is unavailable; Native requires a native backend.
Unsupported path/directory operations still use workers. Workers start only
when an operation needs blocking execution; native initialization alone starts
no fallback workers. `queueDepthHint` bounds three separate service-wide budgets:
worker jobs, native requests, and queued plus active `AsyncFileHandle` operations.
The handle budget is shared across all handles and continuation executors using
the runtime. Waiting for file-position ordering or a flush/close barrier therefore
cannot bypass the bound by adding external executors. The runtime's
`operationCapacity` separately bounds admitted backend completions, including
path/directory and network operations. These are separate budgets, not one combined
operation count.
`GetFileBackend()` reports None before initialization; `HasFileBackend()` and
`HasNetworkBackend()` report service initialization, not pending task counts.
A network-only application creates no file backend or filesystem workers.

`AsyncFileHandle` operation calls retain their shared backend state before
returning a cold task. Moving, replacing, or destroying the handle does not
rebind an existing call or release its native resource while that call still
owns it. Contexts, executors, and buffers remain borrowed through completion.
Local async file state releases an unclosed native handle when its last owner
retires; an opened result discarded before delivery also releases its handle.
Local file handles serialize shared-position reads and writes in admission order.
Explicit-offset operations may overlap, including with a shared-position operation;
callers coordinate overlapping byte ranges and borrowed buffers. Flush waits for
all earlier admitted operations and blocks later operations until it completes.
Close closes admission immediately when its task reaches the file gate, then waits
for earlier backend work before releasing the native handle. `IsOpen()` is false
while close is pending; further operations, including another close, report
`IOErrorCode::Busy`. Close on an already closed handle succeeds. Canceling a queued
close reopens admission; cancellation after backend submission waits for that
backend and does not promise the handle remains open.

Admission order is the order executing tasks reach the gate, not the order cold
tasks are created. A queued operation consumes a handle-budget slot and its task's
reserved continuation capacity, without occupying a filesystem worker. Saturation
reports a scheduling fault with `ScheduleError::ResourceExhausted` before entering
the gate, including for close; a rejected close leaves admission open. Canceling
queued work removes its waiter. Its slot is released when the task processes
cancellation; an active slot is released after backend task retirement. Slow or
paused external executors can consequently retain capacity until their tasks
resume and unwind.
Runtime stop lets active backend access finish and cancels queued file work before
it can submit new backend work. The runtime cannot join these tasks on an external
executor; the application must continue driving that executor to finish them.

Local async transfers reject lengths above `UINT32_MAX` and explicit offsets above
`INT64_MAX` with `InvalidArgument`. Split larger transfers and handle short results.
Explicit-offset writes on append handles report `NotSupported`; sequential writes
append normally. Access-mode errors also report `NotSupported` consistently across
native and fallback paths. File admission is tested on Linux; Windows backend
integration and platform validation remain tracked in
`IORuntimeImplementation.md`.

POSIX network readiness routes directly through stable generation registrations.
Linux uses epoll/eventfd and io_uring completion notification; there is no
private network polling thread or io_uring completion thread. A notification
causes bounded CQ draining, and any remaining CQ entries retain readiness.
The portable poll backend scans its descriptor snapshot and rotates ready
results. Windows native files use per-operation OVERLAPPED registrations in the
same IOCP loop; flush and close use lazy, bounded workers. User cancellation and
runtime stop request CancelIoEx while retaining the request until its terminal
packet is consumed. Windows TCP connect/accept and TCP/UDP transfers use the
same per-operation IOCP path and retain their directional leases until native
access ends. The affected Windows file, socket, runtime, and clock test executables
have cross-compiled and linked. Windows execution is left to native environment
validation; macOS validation also remains open.

## Shutdown and cancellation

`RequestStop()` is nonblocking and safe from a callback or another thread. It
closes admission, requests backend cancellation, and wakes the loop. The
lifecycle is Running, Stopping, then Stopped. Already reserved completions remain
deliverable while ordinary submissions and new I/O are rejected.

`Shutdown()` blocks until runtime-owned backend work and reserved delivery have
drained. On an unbound runtime it binds to the caller; on its owner it can drive
shutdown. Another thread can wait for an active Run or runner committed to
finishing shutdown. Without that commitment, off-owner shutdown throws an
ownership error. A single PollOnce call is not a committed driver. Calling
Shutdown from a callback throws; use RequestStop there. Repeated shutdown after
Stopped is harmless. A manually driven runtime must be shut down on its owner
before destruction on another thread.

Worker cancellation before execution skips the operation. A running blocking
call may return naturally; cancellation never releases its buffers while the
backend can still access them. Native file cancellation is observed after native
completion. A canceled write may already have changed the file.

Runtime shutdown transfers external completions into their reserved executor
queues. It does not imply that external application tasks have consumed them.
Keep those executors operational and join application tasks before releasing
their contexts and resources. Started runtime tasks retain a reusable continuation
reservation while awaiting external children. They can resume and join another
already-started child during Stopping; this does not admit new I/O or cold tasks.
TaskScope and RunTask make the application join explicit. Other combinator and
generator cleanup paths are still under audit.

## Host-loop integration

A POSIX host can keep runtime ownership on its own thread. Allocate a reusable
`NativeWaitSource` buffer with `GetOptions().registrationCapacity + 1` entries,
then repeat the following whenever a source or host timer notifies it:

1. Call `PollOnce()` until it returns false. Check `IsStopped()` before waiting
   again. Keep individual callbacks short so pumping cannot monopolize the host.
2. Call `CopyNativeWaitSources(buffer)` and monitor the returned prefix. A source
   has a borrowed descriptor and `Read`/`Write` interest bits. With POSIX `poll`,
   map these to `POLLIN`/`POLLOUT`; also react to error, hangup, and invalidation.
3. Read `NextDeadline()` after copying sources. Arm the host's monotonic timer
   for that deadline, or disable it when absent. If already due, pump again.
4. Wait for either descriptor notification or the host timer, then refresh the
   entire snapshot. New work, cancellation, registration changes, shutdown, and
   earlier deadlines signal the included wake source. Do not use periodic polling.

The copy does not allocate, consume readiness, drive callbacks, or initialize a
resource backend. Insufficient caller storage returns `no_buffer_space` without
partial output. The capacity above covers the largest portable snapshot; Linux
and macOS/BSD require only one entry, the aggregate epoll or kqueue descriptor.
The portable poll backend includes its wake pipe and every registered descriptor.
It therefore requires the host to monitor the whole snapshot, and retains its
documented scan cost; monitoring just the wake pipe would miss native readiness.
macOS/BSD implementation still requires native platform validation.

Sources are borrowed, changing snapshots, not operation identifiers. Never read,
close, dequeue, or dispatch them directly. Registration changes can race a host
snapshot; the wake source requests a refresh. Treat descriptor invalidation or
failure to install a stale host registration as a request to pump and refresh,
not as permission to act on a possibly reused descriptor. Keep the runtime alive
and remove its host registrations before destroying it. During asynchronous host
shutdown, request stop and keep processing notifications until `IsStopped()`;
join application tasks on their selected executors before releasing their data.

Windows IOCP is not a general Win32 wait handle, and consuming completion packets
outside the runtime would steal its results. `CopyNativeWaitSources` returns
`operation_not_supported` on Windows. Keep a `RuntimeRunner` responsible for I/O
and select the UI/host executor in `TaskContext`. That executor's queued submission
and reserved completion paths must notify the host (for example through its normal
message queue), and it must stay operational while admitted work unwinds and joins.
Do not block the UI thread in `SyncWait` when continuations target that same thread.
The runner pattern also supports POSIX hosts that prefer a separate I/O thread.
