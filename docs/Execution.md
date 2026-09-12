# Execution, threads, and fibers

`NGIN/Execution.hpp` is the focused umbrella for schedulers, native threads,
and fibers. The coroutine task model has its parallel `NGIN/Async.hpp`
umbrella; include both when an implementation directly uses both areas.

Use:

- `InlineScheduler` when queued work should run immediately
- `CooperativeScheduler` for caller-driven task progress
- `ThreadPoolScheduler` for explicitly owned worker threads
- `Thread` and `ThisThread` for native thread control
- `FiberScheduler`, `Fiber`, and `ThisFiber` for cooperative stackful work
- `Task<T, E>` and `TaskContext` for coroutine composition

Schedulers and drivers are explicit owners. Creating a task does not start it,
and no global scheduler or worker pool is created behind the caller's back.
Cancellation is a terminal async state, distinct from a domain error and from
an unexpected exception/fault.

Immediate and timed submission return `ScheduleResult`, an
`std::expected<void, ScheduleError>`. Callers must handle invalid executor
references, stopped or rejected schedulers, and queue/resource exhaustion.
The success result is trivial and allocation-free. Queue admission and
constructing a heap-backed `WorkItem` can allocate.

See [Async](Async.md) for task composition and [Synchronization](Sync.md) for
cross-thread coordination.


## Tracked terminal delivery

`ExecutorRef::ReserveCompletion(WorkItem)` reserves one terminal continuation
before starting backend work. Its move-only `CompletionReservation` queues work
with `Dispatch()`; delivery does not allocate, invoke inline, or compete for an
ordinary submission slot. Destroying an unused ticket releases capacity without
running it. Repeated dispatch on a consumed ticket is a no-op.

ThreadPoolScheduler and CooperativeScheduler support these tickets with a
separate default capacity of 4,096. The thread pool accepts an optional second
constructor argument for this limit; the cooperative scheduler accepts the limit
as its optional constructor argument. A saturated queue reports
`ScheduleError::ResourceExhausted`. Unsupported executors, including
InlineScheduler, report `Rejected`; an invalid ExecutorRef reports
`InvalidExecutor`. Fallback file operations require this contract and reject
unsupported executors before starting worker operations.

Pool workers rotate between reserved completions, externally injected jobs, and
older/newer local jobs. A continuously rescheduling task therefore cannot keep
those ready sources from progressing on its worker. Callbacks must yield or
finish before the worker can service another source. `RunOne()` also services
reserved/injected work and can help any worker's local queue, including a pool
with only one worker.

Thread-pool destruction rejects new reservations and drains existing tickets on
its workers. Outstanding tickets must be dispatched or released by their owners;
otherwise shutdown cannot finish. Never destroy the pool from one of its
callbacks. A cooperative scheduler must be pumped until its reservations and
queued completions have drained before destruction; violating this requirement
terminates rather than detaching unfinished delivery. Ordinary queue APIs retain
their existing threading requirements. Distinct reservation tickets may be
published from other threads.

`ExecutorRef::IsCurrent()` identifies dispatch on a supporting executor. It is
false outside dispatch and when the executor cannot identify its execution
context. Awaiting an external child task or operation returns a successful
continuation to the parent's executor.


Cancellation-aware `TaskContext::YieldNow` and `Delay` also require reserved
terminal delivery. They reject unsupported executors before registering work.
Cancellation publishes a terminal result on the selected executor and releases
its coroutine lease there. If queued timed work is discarded by a thread-pool
shutdown or `CancelAll`, its reserved path delivers a scheduling fault instead
of stranding the task. Canceled timer records still require dispatch or discard
for queue-storage reclamation in the general schedulers.

Started tasks and generator advances use their existing completion reservation
to retire a discarded initial submission. Token-free `YieldNow` and ordinary
`ExecuteAt` delays reuse that same slot when their context selects the task's
executor. Submission and dispatch/discard meet at a two-event handshake: an
executor that executes or discards the work before returning cannot finish the
coroutine while submission still accesses its frame. Immediate rejection retains
its specific scheduling error; discarded accepted work reports `Stopped` through
the reserved executor path. Normal queued execution can resume directly on the
selected executor after submission has finished.

Executor references compare their borrowed state and dispatch bindings with
`operator==`. A context selecting a different binding reserves separate delivery
on that executor. Token-free suspension on an executor without completion
reservations reports `Rejected` before submitting the await; unsupported
executors may still execute standalone tasks that do not suspend this way.

## Removable timers

`ExecutorRef::SupportsTimerRemoval()` reports whether the executor implements
`ScheduleTimer(WorkItem, TimePoint)`. That operation returns a move-only
`TimerRegistration`. Canceling or destroying it removes undispatched work and
releases its queue storage immediately. Cancellation does not invoke the work
and cannot interrupt a callback already claimed for dispatch. `Detach()` leaves
the timer queued without retaining cancellation ownership. The executor must
outlive every registration, including registrations for timers that have fired.

`TaskContext::Delay` uses this capability when present and removes the timer
when cancellation wins, including cancellation racing timer installation.
It also reserves terminal delivery without an explicit cancellation token, so
discarding an accepted timer during shutdown reports a scheduling fault.
Unsupported executors report `Rejected` from `ScheduleTimer`; their existing
`ExecuteAt` behavior remains available. Runtime implements removable timers through its executor.

`CompletionReservation::Schedule()` retains its reserved callback for reuse.
There may be at most one pending invocation. A callback may schedule its next
invocation while running; it queues after the current invocation returns. Reset
releases ownership, while an already queued invocation still completes before
storage is freed. The reservation consumes one capacity slot until released,
including while idle. This supports admitted task cleanup after Close without
allocating or bypassing the executor queue. `Dispatch()` remains the one-shot
ownership transfer. `ExecutorRef::SupportsCompletionReservations()` reports
whether an executor implements this contract.
