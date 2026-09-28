# Runtime resource bounds

Source audit: 2026-09-13. Limits are per runtime, service, executor, or explicit
owner as listed; they are not one global allocation budget. Capacities must be
positive. Backends independently check stop before OS submission, so a gate
acquisition racing shutdown cannot authorize new native work.

| Growing resource | Bound and acquisition | Release and delivery guarantee | Source |
| --- | --- | --- | --- |
| Ordinary runtime submissions | submissionCapacity (default 4096); Execute reserves before queueing | Node released after invocation/discard and callable destruction; no inline rejection fallback | RuntimeLoop.cpp; Execution/detail/CompletionQueue.hpp |
| Runtime continuation nodes | completionCapacity (8192); ReserveCompletion before starting a root or pending delivery | Reusable tickets hold capacity until reset and any queued/executing callback retires; delivery uses no fresh ordinary slot | RuntimeLoop.cpp; CompletionQueue.hpp; Async/Task.hpp |
| Runtime external-operation/control nodes | operationCapacity (4096); ReserveOperation before external work | Retained through backend/control completion; stop uses admitted control delivery | RuntimeLoop.cpp; IO/AsyncDispatch.hpp; IO/NativeFileDispatch.hpp; Net/NetworkDriver.cpp |
| Timers | timerCapacity (4096); inserted under loop lock before acceptance | Fired/canceled nodes and payloads are removed; canceling long timers does not wait for their deadline | RuntimeLoop.cpp |
| Poller registrations | registrationCapacity (4096); ReserveWatch/Watch before arming OS interest | Unwatch releases registration; generation checks reject stale readiness; owned handlers survive dispatch | RuntimeLoop.cpp; RuntimePoller platform implementations |
| File handle operations | files.queueDepthHint (1024), service-wide across handles/executors; AcquireHandleOperation in gate Enqueue | Integrated FileOperationLease releases after backend-local request and rollback guards; queued cancellation retains admission until cleanup executes | FileSystemDriver.cpp; FileOperationGate.hpp/.cpp; LocalFileSystem platform operations |
| Blocking file jobs | files.queueDepthHint; bounded ThreadPoolScheduler admission | Queued/active worker capture retained until work or discard finishes; completion delivery is reserved first; canceled running calls still retire normally | FileSystemDriver.cpp; AsyncDispatch.hpp; ThreadPoolScheduler |
| Linux native file requests | files.queueDepthHint request-slot vector; free slot and SQE before submission | Slot retained through native completion; cancellation requests do not free buffers early; eventual user delivery already reserved | FileSystemDriver.linux.cpp; NativeFileDispatch.hpp |
| Windows native file requests | files.queueDepthHint pending-request budget before native submission | IOCP completion retires submitted request; CancelIoEx is a request, not permission to release borrowed buffers | FileSystemDriver.win32.cpp; NativeFileDispatch.hpp |
| Scoped roots/children | explicit TaskScope capacity; Prepare allocates record, result, execution slot, and terminal notification before invoking factory | Scope retains producers through frame retirement; Join releases captures and returns bounded, admission-ordered errors | Async/TaskScope.hpp |
| Supervisor live children/errors | explicit TaskSupervisor capacity; same Prepare path | Successful/reported children recycle after record cleanup; unreported errors retain bounded records and close admission; Shutdown joins | Async/TaskSupervisor.hpp; TaskScope.hpp |
| FirstCompleted watches | one fixed watch per explicitly supplied input (including duplicates), allocated before registration | Wait completion/cancellation unregisters and joins callbacks; does not consume results or cancel owned work | Async/FirstCompleted.cpp |
| Race/deadline-owned work | explicit competitor set/scope capacity; deadline boundary owns one timer when required | Competitors, losing errors, timer callbacks, and cancellation registration retire before the boundary returns | Async/Race.hpp; Deadline.hpp; DeadlineState.cpp |

Paths in the source column are under include/NGIN or src/NGIN as appropriate.
Executor completion capacity applies separately to external executors. Runtime stop
may finish after handing them reserved delivery; the application must keep those
executors driven and join its owners.

## Explicitly retained result history

Public Operation and OperationRef retain shared result/notification state after
producer retirement. Scope reports and error sinks may also retain diagnostic
snapshots. A supervisor bounds live admissions, not an arbitrary vector of result
handles retained by its caller over successive slot reuse. Applications must bound
such history and keep its allocation resource alive. This storage is explicit
caller ownership, not a hidden unbounded runtime completion queue. FirstCompleted
similarly bounds each watch set by its supplied input count; it does not impose a
process-wide observer quota. Per-owner limits do not constrain the number of owners
an application chooses to create.

## Existing overload evidence

- Runtime allocation fixture: ordinary/completion overload, long timers, delayed
  tasks, socket operation/registration overload, and ready/pending receive cleanup.
  owned-io-allocation-final.log records the passing instrumented run.
- Integrated native/fallback file allocation fixture: four producers, two handles,
  64 accepted + 256 rejected operations per round, 16 rounds per backend. Latest
  integrated-file-overload-tests.log reports zero retained bytes above baseline.
- FileOperationGate tests: capacity shared across handles/executors, concurrent
  producers, canceled capacity recovery, registration failure, and saturated
  completion storage; file-admission-stop-origin-tests.log includes latest changes.
- ScopeAdmission/ScopeAllocation: reservation rejection before factory invocation,
  startup despite ordinary rejection, partial-allocation cleanup, diagnostic
  release. FirstCompleted checks failed watch allocation rollback and races between
  cancellation and notification. stage3-combined-* records their shared-contract run.
- Runtime loop/runner: bounded batches, notification-driven wakeup, reserved cleanup
  during stopping, timed incomplete drainage, and external joins. See timed-shutdown-*
  and shutdown-delivery-boundaries-*.

All named logs reside in build/async-runtime-evidence. These observations cover
specific Linux configurations; they do not prove every native platform or the final
full-load/performance gates. The local overload matrix is reconciled in [Stage4Audit.md](Stage4Audit.md).
T22/T25/T26 retain final Linux and native-platform verification.
