# I/O runtime implementation contracts

These are the implementation decisions for `IORuntimePlan.md`. Until the plan's
verification is complete, `IORuntime.md` describes the released implementation;
this document does not claim that the new runtime is already implemented.

## Admission and completion

Admission linearizes under the runtime admission lock while the state is
`Running`. It reserves an operation slot, backend request storage, and a tracked
completion on the selected executor before any OS operation starts. Failure is
reported as an async fault with the underlying `ScheduleError` (`Stopped` or
`ResourceExhausted`); it never waits for capacity. Allocation failure during
reservation has the same behavior and releases all partial reservations.

Defaults are 4,096 ordinary queued submissions, 4,096 timers, 4,096 in-flight
operations, 1,024 blocking jobs, 8,192 continuation reservations, and a total dispatch batch of 64. All limits
are positive and configurable. A reservation counts against its budget until
released, including while awaiting backend completion. Completion queues use
reserved nodes, so delivery cannot allocate or compete with ordinary work.

Executor submission always queues on the runtime. A ready I/O result may avoid
suspension only while already executing on the selected executor. Continuations
of suspended operations queue through their reservation. No rejected submission
causes inline execution. No recursive inline dispatch is needed or permitted.
External executors must explicitly support tracked completion reservations;
otherwise I/O admission fails before touching OS state. They must remain alive
and operational until dependent application operations have been joined.

## Cancellation and lifetime

Cancellation is a request, not a terminal result. The backend or worker publishes
exactly one terminal result after its final access to borrowed state. Cancellation
before a blocking job starts skips the job. Cancellation during a blocking call
is observed after the call returns. Cancellation racing with success may produce
either success or cancellation, but cannot publish both or race writes to the
completion payload. A canceled write may have changed the file.

Operation state and coroutine leases remain owned through backend completion and
executor delivery. Dropping an `Operation` alone does not join it. Resources,
buffers, and borrowed contexts must outlive application task completion, not
merely an early cancellation request. Cancellation callbacks must not publish a
parent task's completion while a child still uses its borrowed state.

## Resource concurrency

Runtime submission is thread-safe; it does not make arbitrary synchronous
resource methods safe to race. TCP permits one read and one write concurrently.
Overlapping reads, overlapping writes, simultaneous connects, and overlapping
accepts on one listener fail with an actionable operation-in-progress error.
UDP similarly permits one receive and one send. Ordering between independent
directions is unspecified. No resource-wide lock spans a suspended read and
prevents a write from progressing.

Explicit-offset file operations may overlap; operations using the shared file
position are serialized in admission order (when executing tasks reach the gate).
Callers coordinate conflicting byte ranges and buffers. Flush is an admission
barrier: earlier work drains before it starts, and later work waits for it.
Close closes admission before draining earlier work. New operations and overlapping
close calls report `Busy`; close on an already closed handle succeeds. Canceling a
queued close reopens admission. A submitted close may release the native handle
even when cancellation wins result delivery. `IsOpen()` is false during close.
Queued and active handle operations share a service-wide `files.queueDepthHint`
budget, independently of backend request and worker-job budgets. Multiple handles
or external executors cannot bypass this bound. Capacity rejection reports
`ScheduleError::ResourceExhausted` before queueing, including for close; a rejected
close leaves admission open. A canceled waiter retains its slot until its executor
processes cleanup, and an active operation releases its slot after backend-task
retirement. Already-closed and closing-handle outcomes take precedence over capacity.
Resource close requests backend cancellation where supported and defers OS-handle
release until backend access ends. Registration identifiers include a generation,
preventing stale readiness
events from addressing a newly reused descriptor. Synchronous close and resource
destruction must obey the same lifetime boundary.

## Shutdown and thread ownership

`RequestStop()` linearizes the `Running -> Stopping` transition under the same
admission lock, requests cancellation, and wakes the loop without waiting. New
I/O and unrelated tasks are rejected. Existing reservations admit continuations
needed to observe cancellation, unwind, and join, including during `Stopping`.
They do not authorize new I/O. Queued ordinary callbacks already accepted are
drained; timers receive cancellation rather than waiting for their deadlines.

`Stopped` is published only after backend access and runtime completion/cleanup
work have drained. Delivery to an external executor may still be queued there,
but retains no internal state requiring runtime progress. It is not application
task completion and does not release that application's borrowed resources.

The first `Run`, `PollOnce`, `RunTask`, or runner drive binds ownership permanently
to that thread. Concurrent, recursive, or different-thread driving throws
`std::logic_error`. Construction does not bind ownership. `PollOnce` is a bounded,
nonblocking batch and reports a snapshot of remaining immediately runnable work.
Host integration provides a wakeup source and next monotonic timer deadline;
hosts arm their wait before rechecking readiness, without periodic polling.

Blocking `Shutdown()` requests stop and drives the loop if called on its owner
outside dispatch (or binds an unbound runtime). Another thread may wait only
while `Run` or a runner is committed to driving through shutdown. `PollOnce` is
not that commitment. Other calls throw an ownership error instead of deadlocking.
Shutdown after `Stopped` is an idempotent no-op. Destruction shuts down under the
same rules; a contract violation terminates because a destructor cannot safely
detach live backends. A manual runtime destroyed elsewhere must first be shut
down on its owner. Runner construction synchronously reports ownership failure;
runner destruction requests stop and joins its thread.

## Application ownership

`TaskScope` owns children and their cancellation contexts. `RequestCancel` is
nonblocking; `Join` waits for every child and then reports the first observed
failure (domain failure, fault, or cancellation). A failing child requests sibling
cancellation. Joining is explicit and remains possible after runtime stop.
The scope destructor never blocks or detaches: destroying a scope with unfinished
children is a contract violation and terminates. Completed children may be
released safely, but callers must join to observe failures.

`RunTask` owns shutdown of a standalone runtime. It starts a root with a scope,
cancels and joins remaining children on every root terminal outcome, drains
shutdown, and returns a `Completion<T, E>`. Root failure takes precedence over
child failure; otherwise the first child failure is returned. A root task must
not block in `SyncWait` on the sole loop thread. CPU-heavy work belongs on an
explicit external executor; callbacks cannot be preempted by batch fairness.
