# Async Runtime Plan

Date: 2026-09-12

Status: proposed implementation plan. This document does not describe completed changes.

## 1. Objective and scope

Make ordinary async composition cheap, concurrent ownership explicit, and I/O completion reliable under load.
Keep tasks cold. Separate coroutine execution from executor dispatch, concurrent lifetime tracking, and
cancellation registration so that each cost is paid where it is needed.

This is a redesign of the async execution contract and its integration with the shared I/O runtime, not a
small task-startup optimization. Implement it in measured stages. Do not switch everything to eager tasks,
rewrite functioning OS backends, or build a general sender/receiver framework as part of this work.

This plan replaces conflicting execution and ownership policies from [IORuntimePlan.md](IORuntimePlan.md)
and [IORuntimeContracts.md](IORuntimeContracts.md) as the target for subsequent implementation. In particular,
mandatory dispatch of suspended continuations, a completion reservation for every started helper task,
rigid executor inheritance, and four infrastructure completion channels are not the new target.
The existing plan remains an unchanged historical input. Current API documentation remains a description
of current behavior until each implementation stage updates it.

Preserve the useful runtime work already completed:

- The `Execution`, `Async`, `IORuntime`, `IO`, and `Net` ownership boundaries and component exports.
- A shared event loop, externally driven operation, the optional runner, and host-loop integration.
- Native and fallback file/socket backends, lazy worker creation, and notification-driven wakeups.
- Bounded admission, completion delivery for admitted work, and backend retirement before resource release.
- Explicit resource binding through `Runtime`, sockets, and files.

Use C++23 and existing standard-library/first-party facilities. No C++26 requirement or new runtime dependency.
API/ABI changes are allowed within this redesign; replace superseded APIs and update consumers together,
without permanent compatibility machinery. Keep unrelated changes out of scope.

## 2. Starting evidence and unanswered questions

The current implementation couples several responsibilities in `Task` startup and completion:

| Current mechanism | Target question |
| --- | --- |
| Cold coroutine construction | How much is frame creation, separately from starting it? |
| Tracked startup reserves completion capacity and submits work | Which reservations and submissions can sequential awaits eliminate? |
| Promise lifetime and continuation tracking | Which state is required only when execution crosses a concurrency boundary? |
| Context and cancellation propagation | Can helpers borrow the environment without allocating, linking sources, or registering callbacks? |
| File gate wrapper starts and awaits a backend task | Can the existing backend operation own the gate lease and cleanup directly? |
| Completion and fairness dispatch | Which hops are required by an execution contract, and which are incidental? |

Empty cancellation tokens already skip callback registration. Do not describe every current task as allocating
a cancellation callback. Scope cancellation and linking active tokens have separate costs to measure.

The latest recorded file checkpoint still exceeds the original latency limits for several workloads.
Native read and fallback read/write mean latency are approximately 39%, 48%, and 54% above the original
NGIN baseline. These are comparisons against older NGIN, not .NET. The dominant cause has not been isolated;
the wrapper and task bookkeeping are hypotheses, not an established explanation.
See [IORuntimeMeasurements.md](IORuntimeMeasurements.md) for raw artifacts, fixture limits, and baselines.

## 3. Execution model

### 3.1 Cold computation and running work

`Task<T>` describes a lazy, move-only, single-consumer computation. Constructing it does not schedule its body.
The generic spelling may retain an error parameter where useful; `Task<T>` below is shorthand.

- `co_await child` starts a sequential child directly when possible. The parent owns the child until completion.
- `scope.Spawn(child)` creates concurrent work and returns a move-only `Operation<T>` result handle.
- The scope retains ownership of spawned work even if its result handle is dropped. Dropping the handle does
  not detach, cancel, or free a running child's state.
- A root runner or explicit application supervisor supplies ownership outside a lexical task scope.
- An unstarted task can be destroyed without starting it. A running operation cannot be abandoned by
  destroying the only object responsible for its execution lifetime.

Sequential composition does not require a child scope, queue item, stop source, observer list, or reusable
completion reservation per coroutine layer. Keep its state sufficient for parent/child suspension and
result propagation. Use symmetric transfer or an equivalent bounded-stack mechanism.

Concurrent operation state owns the additional publication, cross-thread completion, and scope membership
machinery. Do not duplicate this ownership in every helper frame or expose extra public task types solely
to distinguish storage optimizations.

Provide an inline ready-result representation within the ordinary task API with no required queue hop or
heap allocation. Arbitrary coroutine frame allocation remains compiler- and allocator-dependent; portable
frame elision is not a universal guarantee. Report frame allocation separately from runtime bookkeeping.

Single consumption is the default. Multiple result consumers require an explicit shared/fan-out facility
with documented synchronization and retained-result costs. Do not add that cost to ordinary tasks; implement
such a facility only when a concrete consumer needs it.

### 3.2 Execution environment

The starter supplies an `AsyncEnvironment`. Nested tasks inherit its logical properties:

- Suggested/default executor.
- Cancellation token and effective deadline, if present.
- Allocation resource for allocations performed after the environment becomes available.
- Optional tracing/task-local metadata and priority hints.

Use a small typed environment and existing context facilities. Do not introduce a dynamic property registry,
an observability subsystem, or priority scheduling algorithms just to populate these fields. Absent optional
properties must not allocate. Environment storage and referenced resources must outlive their borrowers.

A cold coroutine frame can be allocated before the starter's environment exists. Do not claim that a later
inherited allocator controls that allocation. Use an explicit construction allocator where supported, or
document the frame's default allocation policy; measure it separately.

The suggested executor is not permanent affinity. Do not capture arbitrary ambient UI/thread-local state,
or infer execution permission merely from the executor stored in a context. Executor transitions preserve
cancellation, deadline, allocator, and logical metadata unless a named boundary explicitly overrides them.

### 3.3 Explicit transitions

The following names describe target semantics; finalize their signatures before changing call sites.

| Operation | Contract |
| --- | --- |
| `StartsOn(executor, work)` | Start the inner computation on that execution resource and supply it as the inner default. The inner computation may subsequently transition. No implicit return to the caller's original executor. |
| `ContinuesOn(executor, work)` | Start the inner computation under the incoming environment; deliver its terminal value, error, or stopped completion on the specified executor. |
| `SwitchTo(executor)` | Move the current computation to that resource and make it the subsequent default, preserving logical context. May complete inline when already permitted there. |
| `YieldNow()` | Defer the current computation through its current execution resource so other eligible work can progress. It must not optimize into an inline no-op. |

Ordinary awaiting does not silently restore the pre-await execution resource. A thread-affine caller uses
an explicit completion transition when calling work that may switch resources. A leaf I/O API documents
its completion resource; backend threads must not arbitrarily run application continuations.

### 3.4 Inline dispatch and fairness

Both ready and previously pending completions may continue inline when the dispatch contract permits it.
Keep submission/enqueue distinct from a dispatch operation that may execute inline. Do not globally change
`Execute` into an inline call and thereby change existing queue and yield semantics accidentally.

Inline dispatch requires all of the following:

1. The current thread is in an authorized execution context for this continuation.
2. The operation has published its result and completed the required backend retirement.
3. No internal lock is held and no protected submission/publication transition is exposed to reentrant code.
4. There is no explicit isolation requirement, and the executor's depth and work budgets permit it.

Otherwise defer through a bounded delivery path. Track both nesting depth and work performed: symmetric
transfer can avoid stack growth while still starving timers or unrelated work. Runtime `PollOnce()` and
executor fairness budgets must account for inline continuation chains, not just dequeued items.

Reserve a deferred-delivery path at the owning root/spawn/suspension boundary before it is needed. A sequential
chain may reuse that guarantee; do not reserve one slot per helper. Publish resumable state before another
thread can complete it, and do not touch a potentially destroyed coroutine frame after publishing its continuation.

Queue saturation must never force execution on an unauthorized thread or strand accepted work. If a custom
executor cannot provide safe inline dispatch or guaranteed deferred delivery, use its queued contract and
reject unsupported admission before starting external work.

## 4. Cancellation, completion, and ownership

### 4.1 Cancellation cost and authority

The owner/scope normally holds the cancellation source. Descendants receive the token through the environment.
Passing it through an ordinary helper must not create another source, linked state, or registration.
Register only at operations that need notification while suspended, such as a pending backend request or timer.

For a token that cannot stop, require zero cancellation-specific allocations, registrations, and synchronization
on the execution path. This does not remove synchronization needed for cross-thread I/O or completion itself.
Poll the token at documented cooperative points where no callback is necessary.

An independent per-operation stop source is an explicit ownership option, not part of every result handle.
Link sources once at a real cancellation boundary. Scope stop propagates to owned descendants automatically.
Cancellation shielding is not required for the first implementation; if introduced, it cannot bypass joining
or runtime shutdown, and must make its bounded cleanup purpose explicit.

### 4.2 Exactly three terminal channels

Use `Value`, `Error`, and `Stopped` as the only infrastructure terminal categories. `Pending` is a lifecycle
state, not a fourth terminal channel.

Retain typed API errors where useful: the `Error` payload can distinguish an API-specific error from an
infrastructure fault/exception. Collapse today's separate `DomainError` and `Fault` terminal categories into
that one error channel. Expected domain outcomes may instead be normal `Result`/`Expected` values when that
fits the API. Do not force every API into a nested result or discard existing error information.

`Task<void>` remains awaitable with observable failure. All await adapters, generators, combinators, root
runners, and completion inspection APIs must agree on the three-channel model.

`RequestStop()` is a request, not completion. A success racing with a stop request may remain successful;
exactly one terminal publication wins according to the operation's documented synchronization point.
Do not overwrite a published value/error simply because cancellation is subsequently requested.

An operation is join-complete only when the backend and callbacks no longer borrow its operation resources.
Internal bookkeeping may retire later only if it no longer references caller-owned state. The caller must
keep borrowed buffers and resources alive through this completion; the runtime does not make arbitrary
early buffer destruction safe.

### 4.3 Canceling work versus canceling a wait

Stopping an owned operation requests cancellation and still requires joining actual completion before
releasing its borrowed resources. A cancellable observer wait may finish earlier only when another owner
retains the operation and its resources and guarantees eventual cleanup.

Keep these as distinct APIs. A wait-only cancellation path unregisters its observer safely before releasing
observer state. It does not steal a single-consumer result, release backend buffers, or implicitly detach work.

### 4.4 Structured scope closure and sibling failure

Provide an awaiting scope helper for the normal case: run the body, close admission, and asynchronously join
all children before returning. On body failure, child failure, or parent cancellation, request sibling stop
promptly and then join. A low-level explicit `Close`/`Join` path may support advanced callers.

A scope destructor cannot secretly block an executor or detach unfinished children. Destruction with live
children is a documented contract violation with a release-build fail-fast path; debug assertions alone are
insufficient. The normal awaiting helper must handle early return and exceptions without reaching that path.

Default scope policy is fail-fast cancellation with complete cleanup:

- Any child `Error` requests sibling cancellation as soon as it is observed.
- After joining, a body error is primary; otherwise choose the lowest spawn-index observed child error.
- Retain additional errors in stable spawn order, bounded by admitted child capacity. Error takes precedence
  over stopped outcomes; cancellation alone is not reported as failure.
- Without errors, return `Stopped` if the scope body or an owned child completed stopped; otherwise return value.

This ordering is deterministic for the errors actually produced. Which failures occur before cooperative
cancellation takes effect can still depend on scheduling. An independent-failure supervisor, if needed,
must be a named policy rather than an accidental consequence of dropping child handles.

### 4.5 Observation and competition

`FirstCompleted` observes externally owned operations. It returns the first terminal notification without
canceling other operations or consuming their values. Multiple already-terminal inputs are ordered by input
index; concurrent publications use one defined winner arbitration point.

Observation must not silently turn every task into a shared task. Use explicitly registered, bounded
observation state at this combinator boundary. The owner remains responsible for joining operations.

`Race` owns its competitors. The first terminal outcome wins, including an error or stopped outcome. It then
requests loser cancellation and joins all competitors before returning. This is distinct from first-success.
Concurrent winners use a single arbitration point; already-terminal ties use input order.

Preserve loser errors as bounded secondary diagnostics. If the winner succeeded but a loser reports an error
during termination, return an error with the winner information retained; otherwise preserve the winner's
error as primary. An error takes precedence over a stopped winner. Document this policy so cleanup failures
are never silently discarded.

Replace the existing owning/canceling `WhenAny` behavior with the explicit `Race` contract and update its
callers. Do not keep two incompatible meanings under the same name.

## 5. Deadlines, detached work, and shutdown

### 5.1 Deadlines and timeouts

Use monotonic absolute deadlines. Convert a relative timeout once at its ownership boundary; descendants
inherit the earlier of their own and their parent's deadline. Do not restart the timeout in every helper.

An inherited deadline shares its owner's timer/stop state. A stricter local deadline creates a new boundary
and pays for its own bounded timer. Expiry requests stop; it cannot release resources still used by the backend.
Record deadline expiry as a stopped reason when it causes stopped completion, not as a fourth terminal channel.
A value already won by the completion race remains a value.

A structured timeout returns only after owned work has terminated, so cleanup may extend past the deadline.
A strict limit on observer waiting is a separate wait-only facility requiring continued ownership elsewhere.
Timers must be removed or retired on early completion and count toward admission limits.

### 5.2 Explicit application-lifetime work

Detached work means transfer to an explicit application supervisor, not absence of ownership. That supervisor
provides bounded admission, cancellation, join-at-shutdown, and an unhandled-error sink. All retained buffers
and context must remain valid for the transferred lifetime; scope-local references cannot escape implicitly.

Do not provide an ordinary unobservable `async void` or a naked fire-and-forget detach. Normal `Spawn` always
attaches to an owner. Error reporting must have a defined fallback and must not block the only executor
needed for cleanup. Reuse the root runner/owner where possible instead of adding a separate global service.

### 5.3 Shutdown protocol

Keep user cancellation and runtime shutdown distinct. A cannot-stop user token does not exempt admitted
I/O from runtime shutdown and backend cleanup.

1. Close admission for new roots, spawned work in closing scopes, and new external I/O.
2. Request stop for work owned by the closing scope/supervisor/runtime, according to its ownership boundary.
3. Continue driving backend retirement, already-admitted completion delivery, unwinding, and asynchronous joins.
4. Release native resources only after their requests and callbacks have retired.
5. Finish application shutdown after application-owned roots/scopes and their delivery executors have drained.

Runtime shutdown does not implicitly join arbitrary application tasks on external executors. Distinguish
backend stopped, completion handed to an executor, and application joined in status and documentation.
External executors must remain alive and driven until their admitted continuations and joins finish.
Shutdown must not need new ordinary queue capacity for previously admitted cleanup.

There is no forced destruction of still-running cooperative tasks when a shutdown timeout expires. Report
incomplete shutdown and retain ownership; process-level termination policy belongs to the application.
Preserve fixed loop-thread ownership, non-reentrant loop driving, and notification-driven idle behavior.

## 6. I/O integration and resource limits

Keep backpressure at places where retained work can grow: roots/spawns, runnable queues, suspended delivery
state, timers, file/socket requests, blocking worker jobs, and explicit observers/shared results.
Reject overload before starting OS work, with an actionable error. Do not block an event-loop thread for capacity.

For every admission boundary, document the limit, when it is acquired/released, and the delivery/retirement
resources it guarantees. Ready operations and nested sequential helpers do not each acquire a generic
operation permit. A pending external request still needs a deliverable completion under full saturation.

Simplify file execution by placing the admission lease, result, cancellation hook, and backend retirement
in the existing operation state where possible. Remove the extra wrapper coroutine/spawn only after its
lifetime and close responsibilities have a concrete owner. Do not replace it with another equivalent layer.

Preserve documented file behavior while changing the implementation: shared-position ordering, independent
offset overlap, and flush/close barriers. The earlier discussion about prohibiting simultaneous reads and
writes did not change that contract. Do not silently serialize all file access or reject supported overlaps
to improve benchmark numbers. Cross-handle filesystem consistency remains a separate API/caller concern.

Preserve supported socket duplex concurrency and native cancellation retirement. Windows `CancelIoEx`,
for example, requests cancellation; it is not permission to free an outstanding request's buffers.

## 7. Implementation sequence and exit checks

Complete and measure each stage before broadening the next. Checkboxes represent future work, not past results.

### Stage 0 — Freeze evidence and specify the contract

- [ ] Record a reproducible snapshot of the current working implementation, including uncommitted inputs,
  compiler/options, fixture configuration, hardware, and source hashes, without altering the user's worktree.
- [ ] Retain original revision `203f704e97a704259ef2db1ee8c93bf92763a7e1` as the historical performance reference.
- [ ] Benchmark construction/destruction, direct child await, explicit spawn/join, context inheritance,
  cancellation registration/linking, ready/pending delivery, and file wrapper overhead separately.
- [ ] Compare cannot-stop, stoppable-but-not-stopped, and actively canceled execution. Use chains of depths
  1, 8, and 64, single/multiple workers, and runtime/external executors.
- [ ] Record allocations/bytes, frame sizes, queue hops, completion reservations, cancellation registrations,
  and latency distributions. Use profiles to locate CPU/atomic contention; counts alone are not timing evidence.
- [ ] Write target API examples and ownership/state diagrams for start, suspension, completion, cancellation,
  scope closure, and shutdown. Resolve dispatch publication and deferred-capacity ownership before coding it.

Exit: attributable cost measurements and a concrete API/state contract. Do not claim the main slowdown is
understood or that a chosen optimization will recover it without this evidence.

### Stage 1 — Cheap sequential tasks and execution environment

- [ ] Separate sequential task state from concurrent operation tracking; implement direct child transfer.
- [ ] Add the inherited environment and ready-result representation without mandatory helper allocations.
- [ ] Implement precise execution transitions and preserve logical metadata across them.
- [ ] Establish safe inline/deferred dispatch, publication handshakes, and depth/work budgeting together.
- [ ] Update `Task`, `TaskContext`, `ExecutorRef`, and affected scheduler paths with focused consumers/tests.

Exit: helper chains within the fairness budget add no queue hop, completion reservation, or cancellation
registration per layer; deep chains have bounded stack use and other work makes progress. Cross-executor
delivery, early completion during submission, and queue saturation preserve the documented contract.

### Stage 2 — Cancellation boundaries and terminal results

- [ ] Move cancellation authority to owners and register only at suspension boundaries that need notification.
- [ ] Replace four terminal categories with value/error/stopped while preserving typed error details.
- [ ] Update await adapters, generators, operation handles, runners, and exception translation together.
- [ ] Verify request/completion races and safe unregister/frame retirement across threads.

Exit: cannot-stop execution has no cancellation-specific allocation, registration, or synchronization;
active cancellation reaches suspended descendants and never permits early buffer release.

### Stage 3 — Structured concurrency and policy

- [ ] Implement the awaiting scope helper, bounded spawn ownership, asynchronous closure, and failure ordering.
- [ ] Separate `FirstCompleted` observation from owning `Race`; update existing combinator consumers.
- [ ] Add deadline inheritance and bounded timer retirement.
- [ ] Make wait-only cancellation and application-lifetime transfer explicitly owned operations.
- [ ] Define the supervisor error sink and complete shutdown ordering across external executors.

Exit: early return, exceptions, sibling failure, deadlines, dropped result handles, and race losers all
preserve ownership until actual completion. No executor-blocking destructor or silent detached task remains.

### Stage 4 — Integrate file and socket operations

- [ ] Adapt native/fallback completion delivery to the new dispatch and environment contracts.
- [ ] Eliminate redundant file task wrappers using the measured cost breakdown and explicit lease ownership.
- [ ] Move capacity guarantees to the real admission/suspension boundaries; retain bounded service limits.
- [ ] Preserve file ordering, close races, socket duplex behavior, cancellation cleanup, and host integration.

Exit: I/O completion paths survive saturation, cancellation, and shutdown without stranded operations,
premature resource release, unauthorized execution, or changed file/network concurrency semantics.

### Stage 5 — Validate performance and complete consumer updates

- [ ] Run focused Async/Execution/IO/Net tests and Linux ASan/UBSan checks, then required package consumers,
  public headers, portable-loop checks, and the canonical `Hello.IO` modes.
- [ ] Use controlled interleaving tests for races and bounded stress for lifetime issues. Do not rely only
  on timing sleeps or claim ASan proves absence of data races.
- [ ] Repeat matched Release measurements for original NGIN, the frozen pre-redesign implementation,
  and the redesign, with warmup and multiple process runs.
- [ ] Measure mixed-load fairness, idle CPU/wakeups, overload memory, cancellation, shutdown, and cold setup.
- [ ] Update [Async.md](Async.md), [Execution.md](Execution.md), [IORuntime.md](IORuntime.md),
  [IORuntimeContracts.md](IORuntimeContracts.md), affected public API docs, examples, and package consumers.
- [ ] Remove superseded machinery and temporary instrumentation after preserving reproducible evidence.

Exit: correctness checks and performance gates pass, or any unmet gate is explicitly recorded as remaining
work. A faster microbenchmark does not close a still-regressed file workload.

## 8. Acceptance criteria and platform verification

Retain the original performance gates in [IORuntimeMeasurements.md](IORuntimeMeasurements.md): at most
25% regression in p50/p95/p99, 20% in mean latency, and 50% in cold setup against the original matched baseline;
at least 80% less idle CPU, no periodic network wakeups, and median single-operation cancellation and idle
shutdown below 1 ms under the documented fixtures. Do not reset the baseline to the slower current implementation.

Additional structural checks must demonstrate:

- No mandatory scheduling or heap allocation for the explicit ready representation.
- No per-helper completion reservation, cancellation registration, or queue hop in eligible direct chains.
- Bounded stack use and progress for timers/unrelated work during long inline chains.
- Completion deliverability under saturation, including required deferral and cancellation cleanup.
- Exactly one terminal outcome and no callback/backend access after resources become releasable.
- Bounded retained memory at admission limits and return to baseline after complete fixture teardown.

Distinguish full-fixture allocations from per-operation allocations, ready from suspended paths, native from
fallback backends, and runtime from external executor delivery. Record unsupported/unavailable measurements
as such. This plan makes no .NET performance claim.

Run Linux verification locally using repository-defined workflows. The user will perform native Windows
execution and testing in Windows; do not run Windows binaries, Wine, or emulation here. Supply Windows
test/benchmark instructions and record the user's actual results when available. Native macOS behavior
also remains unverified until tested on macOS. Linux success does not imply native platform validation.

## 9. Design references

The target borrows concepts, not a dependency or a complete implementation, from
[P2300R10](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2300r10.html): lazy composition,
execution environments, explicit execution transitions, operation state, and value/error/stopped completion.
This is a dated proposal reference, not a claim that NGIN implements the C++26 library.

The distinction between logical context and execution placement is also informed by the
[.NET ConfigureAwait FAQ](https://devblogs.microsoft.com/dotnet/configureawait-faq/).
The separate cancel-work/cancel-wait contract is discussed in Microsoft's
[cancellation guidance](https://learn.microsoft.com/en-us/dotnet/standard/asynchronous-programming-patterns/cancel-non-cancelable-async-operations).
The concrete dispatch, scope-error, race, deadline, and shutdown policies above are NGIN design decisions.
