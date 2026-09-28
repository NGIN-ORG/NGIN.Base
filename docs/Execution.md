# Execution, threads, and fibers

`NGIN/Execution.hpp` is the focused umbrella for schedulers, native threads,
and fibers. The coroutine task model has its parallel `NGIN/Async.hpp`
umbrella; include both when an implementation directly uses both areas.

Use:

- `InlineScheduler::Invoke` for an explicit synchronous call without scheduling
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


## Eligible inline dispatch

`ExecutorRef::TryDispatchInline(WorkItem&)` is separate from ordinary submission.
It requires the executor's current context and available shared work/depth budget.
On refusal, it leaves the work untouched. Mandatory delivery must fall back to an
already-reserved continuation rather than depend on new queue admission. The caller
must publish results, finish retirement required before user continuation, and release
internal locks before attempting inline dispatch. Explicit yields always defer.
TaskContext timer completions use eligible inline delivery only after setup commits
and completion is published; exhausted budgets use the reserved continuation.

Published task completion now uses this path when eligible; its existing handler
still ensures child-frame retirement before entering the parent body. File completion
adapters also attempt inline delivery from their runtime retirement callback when
the selected executor is current; otherwise they use the reserved delivery ticket.
Socket completion follows the same rule after lease and cancellation retirement,
including admitted immediate completions. Resolver delivery is reserved before setup,
and waits for both setup commit and outcome publication before inline/deferred delivery.
It removes cancellation registrations and timeout storage before resuming. Queued
resolver work discarded by driver shutdown publishes a stopped scheduling fault
through the reserved delivery path; an already-published outcome is not delivered again. Legacy
InlineScheduler owns no queue: Execute and ExecuteAt return Rejected without
invoking or sleeping. Use Invoke for deliberate synchronous calls, or a cooperative,
thread-pool, or runtime executor for task startup. This replaces the former implicit
inline behavior of InlineScheduler submissions.

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

Cooperative pump calls rotate between reserved completions, due timers, and FIFO
ordinary work. Each continuously runnable source gets a turn within three successful
pump calls. Future timers do not prevent ready work from running. The source choice
advances before invoking a callback, including when that callback pumps recursively.
Callbacks must return or yield for other work to run; these source-selection bounds
do not by themselves bound the coroutine transfers performed within a callback.
Cooperative pumping, thread-pool dispatch, and runtime callbacks charge active work
budgets and share a limit of 16 active dispatch calls, including calls between distinct
runtime loops. A depth-limited runtime poll preserves and reports ready backlog. At either limit, RunOne returns false
without removing queued work; the enclosing callback must return before further
progress. RunUntilIdle can therefore return with runnable work when called inside
an exhausted execution scope. Thread-pool RunOne/RunUntilIdle follow the same rule,
including when helping a worker queue. The depth of arbitrary user function calls is not bounded.

Pool workers rotate between reserved completions, externally injected jobs, and
older/newer local jobs. A continuously rescheduling task therefore cannot keep
those ready sources from progressing on its worker. Callbacks must yield or
finish before the worker can service another source. `RunOne()` also services
reserved/injected work and can help any worker's local queue, including a pool
with only one worker.

Thread-pool destruction rejects new reservations and drains existing tickets on
its workers. Workers recheck stop before invoking dequeued ordinary work, so a job left behind during
the destructor's queue-clear pass is discarded and can publish its reserved cleanup. Outstanding tickets
must be dispatched or released by their owners;
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

ThreadPoolScheduler and CooperativeScheduler support `ScheduleTimer` and removable `TimerRegistration`
records. Cancel removes undispatched work and releases its captures outside the timer
lock; Detach preserves queued delivery. Async resolution with a positive timeout
requires this capability on its context executor. Runtime and thread-pool executors
support it, as does CooperativeScheduler. Cooperative timer registration, cancellation,
and timer counting are thread-safe; ordinary ready-queue submission and pumping
still require caller serialization.
