# I/O Runtime Foundation Plan

## Status and objective

This document describes planned work, not implemented behavior.

Build one event loop for I/O, timers, and continuations, with explicit thread
ownership and a defined shutdown protocol. Preserve the public resource-binding
model: `NGIN::IO::Runtime`, `socket(io)`, and `files(io)`.

Prioritize end-user performance and usability: predictable latency and memory
under load, low idle overhead, and a straightforward application entry and exit
pattern. Capture performance baselines before replacing execution machinery.

API and ABI breaks are allowed wherever they improve the foundation. Replace
superseded APIs directly and update all consumers. Do not add compatibility
paths, deprecation shims, or a migration guide.

Implement the phases in order, introducing lifetime and shutdown tests early.
Broader improvements are in scope when needed to establish these contracts;
unrelated cleanup remains outside this work.

## 1. Establish execution and ownership contracts

Define these invariants before changing backends:

- Every accepted operation reaches exactly one terminal outcome.
- Cancellation requests cancellation; buffers and operation state remain alive
  until the backend has finished accessing them.
- Completions respect the chosen executor.
- Shutdown rejects new operations while allowing existing operations to finish
  cancellation and cleanup.
- Runtime shutdown covers runtime-owned work. It cannot implicitly join
  unrelated application tasks on external executors.
- Admission is bounded and reserves the storage and executor capacity required
  for terminal completion. Overload cannot strand already accepted operations.
- Stopping rejects new I/O and unrelated work, but preserves continuations
  needed by admitted work to observe cancellation, unwind, and join children.

Define the shutdown admission boundary and `Stopped` semantics in this phase,
before backend implementation. Distinguish backend completion, delivery to an
external executor, and application task completion. Also settle `TaskScope`
joining and destruction semantics early: destruction must not silently detach
children or block the only thread capable of completing them.

Audit `Task`, `Operation`, cancellation registrations, and coroutine frame
retention against these rules. Address concrete lifetime gaps before adding
more concurrency.

Document ownership, thread-safety, submission failure, cancellation races, and
the point at which an operation no longer borrows its buffers and resources.

Define concurrency per resource separately from thread-safe runtime submission.
Document supported overlaps for socket reads, writes, accepts, file operations,
and close, including ordering and failures for unsupported combinations. Support
one TCP read and one TCP write concurrently. Decide before backend changes
whether overlapping operations in the same direction are supported, explicitly
serialized, or rejected; do not silently serialize every operation on a resource.
For files, distinguish operations using independent offsets from operations
sharing a file position. Specify how close races with submission and admitted
operations without invalidating state still in use by the backend.

Specify configurable limits and sensible defaults for pending submissions,
blocking worker jobs, and in-flight operations. Define actionable capacity
errors before starting OS work. Submission must not block the event-loop thread
waiting for capacity. Include timers and reserved completion storage in the
memory budget; a processing batch limit alone does not bound queued memory.

## 2. Separate the runtime core from filesystem implementation

Introduce an `IORuntime` build component, keeping the public namespace
`NGIN::IO`.

| Component | Responsibility |
| --- | --- |
| `Execution` | Executor contracts and general schedulers |
| `IORuntime` | Event loop, timers, wakeups, and operation tracking |
| `IO` | Filesystem and other existing I/O facilities |
| `Net` | Sockets and networking services |

Both `IO` and `Net` depend on `IORuntime`. The runtime must not directly
reference filesystem factories.

Backends attach through a small private service boundary. Remove the current
filesystem linkage dependency from networking without introducing a public
plugin framework.

Update component definitions, source and header ownership, exports, package
integration, and consumer tests together. Preserve the separation between
public namespaces and build-component ownership.

## 3. Provide a runtime executor and explicit thread ownership

Target API shape:

```cpp
NGIN::IO::Runtime io(options);
NGIN::Async::TaskContext ctx(io.GetExecutor());

NGIN::Net::TcpSocket socket(io);
NGIN::IO::LocalFileSystem files(io);
```

`GetExecutor()` uses the existing `ExecutorRef` abstraction. Support immediate
and timed work so `YieldNow()` and `Delay()` work directly on the runtime.

Define immediate completion separately from executor submission. An I/O await
may complete without suspending when its result is ready and the caller is
already executing on the selected executor, subject to the same lifetime and
exactly-once rules as suspended operations. Provide a reliable way to identify
that execution context. Runtime executor submission queues work rather than
invoking it inline; `YieldNow()` must yield to queued work. Specify any permitted
inline continuation dispatch explicitly, preserve executor affinity, and bound
recursive dispatch so it cannot bypass batch fairness. Avoid mandatory queue
hops for eligible immediate completion and measure both completion paths.

Make the runtime externally driven by default. Provide an optional RAII runner:

```cpp
NGIN::IO::RuntimeRunner runner(io); // Owns one event-loop thread.
```

Remove network-specific background/manual modes. `Run()` drives the whole
runtime until shutdown has completed. `PollOnce()` processes a bounded batch
without blocking and reports whether immediately runnable work remains. This
report is a snapshot, not a substitute for notification of subsequent work.

Define a simple root-task entry helper, provisionally `RunTask(...)`, before
rewriting consumers. For a standalone application it starts a root task, drives
the runtime, joins owned children, completes shutdown, and returns the root
result or reports its failure. Specify cancellation and failure handling,
including canceling and joining unfinished children. Callers should not need to
wire root completion to `RequestStop()` themselves. Keep `Run()` available for
long-lived services and make the helper's ownership of runtime shutdown explicit.

Initially support one event-loop consumer and thread-safe submission from
other threads. Applications can use multiple runtimes or external worker
pools for parallelism. Reject concurrent or reentrant loop-driving calls
according to the documented contract.

Initially bind loop ownership to the thread that first drives the runtime,
including through `PollOnce()`, the root-task helper, or `RuntimeRunner`.
Ownership remains fixed for that runtime's lifetime; a runner cannot take over
a runtime already driven by another thread. Reject attempts to drive it from
another thread even when no loop-driving call is currently active. Construction
alone does not bind ownership. One consumer at a time is insufficient to
guarantee thread affinity across separate calls.

Keep `TaskContext` independent: the runtime executor is an option, and callers
can still select an external executor for continuations.

Define and document the supported way to embed the runtime in an existing UI
or application loop, including notification of new work and timer deadlines.
Do not require hosts to discover pending work through periodic `PollOnce()`
calls. Keep platform integration narrow and separate from a public service
plugin framework.

## 4. Implement event-driven waiting and shared timers

Replace the 1 ms polling interval with a wakeable platform event loop:

- Linux: `epoll` and `eventfd`.
- Windows: IOCP with posted control packets.
- macOS/BSD: `kqueue` with an explicit wakeup mechanism.
- Portable fallback: a supported wait mechanism with a wakeup descriptor.

Wake for new work, cancellation, shutdown, or an earlier timer deadline. Keep
timers in a monotonic deadline queue and wait until the next deadline when one
exists. Do not introduce periodic idle polling.

Canceled timers must promptly release their queue storage and retained
coroutine-frame leases once cancellation and any concurrent dispatch are safe.
Do not retain canceled work until its original deadline. Choose a timer
representation that supports efficient removal under cancellation-heavy loads.

Linux `eventfd` and Windows completion ports support these wakeup patterns.
See the [Linux eventfd documentation](https://man7.org/linux/man-pages/man2/eventfd.2.html)
and [Microsoft completion-port documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports).

Use bounded processing batches so busy sockets, queued tasks, and timers cannot
indefinitely starve one another. Test wakeup coalescing and the race between
submitting work and entering the platform wait.

If a batch leaves ready work unprocessed, retain that runnable state and do not
enter a blocking wait. Rotate between event sources while draining backlogs.
Batch limits bound dispatch work; they cannot preempt a long user callback.
Document that callbacks must yield or move CPU-heavy work to an external executor.

## 5. Integrate network and file completions

Replace the private network driver's independent loop with registrations in
the runtime event loop.

Remove the current copying and scanning of all waiters from native event
dispatch. Route ready events directly to stable registrations, with lifetime
and generation protection against stale events and descriptor reuse. Dispatch
cost should scale with ready work rather than all idle connections. Document
any scanning required by the portable fallback separately.

For files:

- Feed Linux `io_uring` completions into the same loop through completion
  notification.
- Share the runtime's IOCP on Windows where supported.
- Use lazy blocking workers for operations without suitable native asynchronous
  support.
- Return worker results through the common completion queue.

`io_uring` supports eventfd completion notification. Notifications must trigger
draining the completion queue rather than be treated as one notification per
result. See the [Linux io_uring registration documentation](https://www.man7.org/linux/man-pages/man2/io_uring_register.2.html).

Drain completions in bounded batches under the shared fairness policy. Consuming
a coalesced notification must not lose the fact that completion entries remain;
continue servicing them without requiring another notification or sleeping.

Fix the current eager creation of fallback workers when a native file backend
succeeds. Workers should start only when an operation actually needs them.

Sharing completion processing does not require executing blocking filesystem
calls on the event-loop thread. Keep blocking work off that thread.

## 6. Make cancellation, executor rejection, and shutdown coherent

Introduce a lifecycle of `Running -> Stopping -> Stopped`.

`RequestStop()` is nonblocking and safe from a runtime callback. It rejects new
I/O and unrelated work, requests cancellation, and wakes the loop. Continuations
for admitted work remain admissible so tasks can observe cancellation, unwind,
and join children. This does not permit starting new I/O during cleanup. The
loop continues processing terminal completions and cleanup before exiting.

`Stopped` means backend access has ended and runtime-owned completion and
cleanup work has drained. Completions targeting external executors may have
been transferred to their reserved delivery paths without being consumed yet;
that transfer must leave no runtime-internal state requiring further loop
progress. It does not mean external application tasks have finished or their
borrowed resources can be destroyed. Application task joining remains explicit.

Provide blocking shutdown outside runtime callbacks. On the bound owner thread,
it may drive a manually owned runtime; an unbound runtime may bind to the caller
for this purpose. A different thread may request stop and wait for an active
`Run()` or runner committed to driving through shutdown, but must never take
over completion dispatch. An individual `PollOnce()` call is not such a
commitment. Reject blocking shutdown from a runtime callback, or from a non-owner
without a driver committed to shutdown, with an actionable ownership error
rather than deadlocking or moving callbacks. Shutdown of an already stopped
runtime is an idempotent no-op. `RequestStop()` remains usable from any thread
without transferring ownership.

Define runner and runtime destruction behavior consistently with this protocol,
including external executor lifetime requirements. Resource owners must keep
external executors operational until their dependent work has completed.
Destruction must respect the same fixed thread ownership; document how a
manually driven runtime is shut down on its owner before destruction elsewhere.

Replace the current fallback that resumes a coroutine inline when its executor
rejects work. This can violate thread affinity. Extend the executor contract
with tracked completion work so an admitted operation retains a valid
completion path during shutdown. Handle admission failure before starting OS
work. Account for completion storage and allocation failure in this contract.

Implement the phase 1 admission limits together with this reservation contract.
Completion delivery for accepted operations must not compete with new
submissions for unreserved capacity or require a fresh allocation to succeed.

Define how queued tasks, timers, native operations, and blocking worker jobs
reach terminal outcomes during shutdown. Do not discard pending continuations
or free operation state while the backend can still access it.

Blocking filesystem calls may need to return naturally before shutdown
completes. Cancellation must never imply that the OS has stopped using a
buffer when it has not.

## 7. Make application task ownership straightforward

Add an `Async::TaskScope` for owned child operations, shared cancellation, and
explicit joining. Build it on the existing task and operation machinery using
the destruction and joining contracts established in phase 1. Implement the
root-task entry helper defined in phase 3 on top of this ownership model.

This addresses a separate responsibility from runtime shutdown: ensuring
application coroutines have finished before their resources disappear.
Specify child failure reporting, cancellation propagation, joining, and scope
destruction without silently detaching unfinished work.

Rewrite Hello.IO around three examples:

- A single-thread application using the runtime executor and root-task helper.
- Background I/O using `RuntimeRunner`.
- I/O continuations explicitly assigned to an external executor.

Keep the existing networking and file demonstrations. Add shutdown with
operations in flight. Explain why blocking `SyncWait()` on the only event-loop
thread would deadlock and demonstrate the correct application entry pattern.
Demonstrate moving CPU-heavy processing to an external executor without
blocking I/O progress. Document the supported host-loop integration pattern.

Update current API documentation and all affected consumers. Remove obsolete
driver and lifecycle APIs rather than retaining legacy entry points.

## 8. Verify correctness, component isolation, and overhead

Add focused tests for:

- Completion, cancellation, and close races.
- Shutdown during submission and from callbacks.
- Executor rejection and allocation failure.
- Admission saturation, capacity recovery, and completion delivery under overload.
- Timer cancellation, prompt storage/frame release, and earlier-deadline wakeups.
- Resource moves, descriptor reuse, and backend initialization failure.
- Task-scope joining, cancellation, and failure reporting.
- Root-task entry success, failure, and cancellation with children in flight.
- Partial completion-queue draining after a coalesced notification.
- Continued cancellation and join progress after new submissions are rejected.
- Immediate and suspended completion affinity, queued submission, yielding, and
  recursive dispatch limits.
- Concurrent TCP read/write, the selected same-direction overlap policies,
  file-position concurrency, and close racing with submission.
- Sequential loop-driving calls from different threads, runner ownership
  conflicts, and manual shutdown from owner and non-owner threads.

Verify these observable outcomes:

- Idle runtimes have no periodic polling wakeups.
- Network-only applications create no filesystem workers or backend.
- Runtime continuations execute on the expected thread.
- Manual shutdown never transfers callbacks to a different thread.
- `PollOnce()` advances networking, file completions, and timers.
- Remaining ready work is reported and never stranded behind a blocking wait.
- Host-loop integration wakes for new work and earlier timers without polling.
- Queue memory remains within configured budgets under sustained overload.
- Shutdown leaves no outstanding backend access to application buffers.
- Runtime shutdown and application task joining obey their distinct contracts.

Run the affected Async, Execution, IO, and Net suites using repository-defined
build and test workflows. Run sanitizers and race stress tests, Hello.IO, and
component-only consumer builds. Validate Linux, Windows, and macOS backends
before claiming cross-platform completion.

Measure idle CPU, thread count, allocations, wakeup latency, throughput, and
network-only binary size against the current implementation. Also measure
p50/p95/p99 completion latency, timer lateness under mixed load, peak queued
memory, and cancellation and shutdown latency. Separate cold initialization
from steady-state costs and cover native and fallback file backends. Compare
eligible immediate completion with suspended completion, including allocations
and queue hops, and verify concurrent TCP read/write makes independent progress.

Include many idle connections with a small active subset, mixed socket/file/task
loads, sustained submission overload, and cancellation-heavy long-duration
timers. Record workload sizes, platform/backend, and measurement conditions.

Capture a baseline before replacing the relevant execution machinery. Set
performance regression thresholds from that baseline before implementation,
and evaluate the final results against them. Lower idle CPU or fewer threads
must not conceal regressions in latency under load, memory use, or throughput.

## Deferred work

Defer multiple threads driving one runtime, custom public services, and
additional networking features until this core is correct and measured.

Do not add a third-party runtime dependency as part of this plan. The existing
first-party execution abstractions and platform facilities provide the basis
for the implementation.

## Completion criteria

The refactor is complete when the contracts above are implemented coherently,
superseded APIs are removed, consumers and examples use the new model, relevant
verification passes, and measured results and platform limitations are recorded.
Record results against the agreed performance thresholds and resolve regressions
or explicitly document and justify accepted tradeoffs before declaring completion.
