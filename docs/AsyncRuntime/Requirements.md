# Async Runtime Requirements

Date: 2026-09-12

Status: proposed target; these requirements do not describe completed implementation.

## Purpose and document ownership

Make sequential async composition cheap, concurrent ownership explicit, and admitted I/O completion reliable.
This document defines the required behavior and acceptance criteria extracted from
[AsyncRuntimePlan.md](../AsyncRuntimePlan.md). [Design.md](Design.md) describes the proposed implementation;
[Tasks.md](Tasks.md) tracks delivery and verification. Requirement IDs are stable references for both.

Maintain future requirement changes here, design changes in Design, and progress in Tasks. The original plan
is the proposal snapshot. Conflicting older execution policies in [IORuntimePlan.md](../IORuntimePlan.md)
and [IORuntimeContracts.md](../IORuntimeContracts.md) are superseded as design targets; current API documentation
must continue to distinguish implemented behavior from this target until the code changes.

## Constraints

### R01 — Preserve the runtime foundation

The redesign must retain C++23, standard-library/first-party production dependencies, existing component
ownership and exports, resource binding, the shared loop, external driving, the optional runner, native and
fallback backends, lazy workers, and notification-driven wakeups. Working OS backends must be adapted rather
than replaced without evidence. API/ABI changes within this scope must update consumers together.

Acceptance: component/public-header and installed-consumer checks pass; canonical runtime entry modes work.
No full sender/receiver framework, C++26 dependency, compatibility layer, dynamic environment registry,
new tracing system, or priority scheduler is required. Unrelated changes remain out of scope.

## Computation and execution

### R02 — Cold, single-consumer tasks

Task construction must not schedule or execute the body. Tasks and running result handles must be move-only
and single-consumer by default. Destroying an unstarted task must not start it. Multiple result consumers
must use an explicit shared/fan-out facility whose additional costs are documented; such a facility is only
implemented for a concrete consumer.

Acceptance: construction/destruction causes no execution; awaiting or explicitly spawning starts once;
the API prevents accidental result copying or repeated consumption according to its documented contract.

### R03 — Direct sequential composition

An eligible child await must transfer directly without a mandatory queue hop, scope, completion reservation,
stop source, or observer registration per helper layer. The parent must retain the child through completion.
Deep sequential chains must use bounded stack space and permit fairness deferral.

Acceptance: chains of depths 1, 8, and 64 within the dispatch budget add no per-helper queue hops, completion
reservations, or cancellation registrations; deeper stress remains stack-bounded and other work progresses.

### R04 — Ready results and allocation accounting

The ordinary task API must support an explicit ready representation with no mandatory queue hop or heap
allocation. Portable coroutine-frame elision must not be promised. Frame construction/allocation and runtime
bookkeeping must be measured separately. A later inherited allocator must not be claimed to control a frame
already allocated during cold task construction.

Acceptance: the explicit ready fixture schedules and allocates zero times; coroutine fixture results identify
compiler, construction allocator policy, frame allocations, and any additional runtime allocations.

### R05 — Inherited logical environment

The starter must supply the suggested executor, cancellation token, effective deadline, allocation resource,
and optional logical metadata/priority hints. Nested helpers must inherit them without allocating absent
properties or creating cancellation boundaries. Referenced environment resources must outlive their borrowers.

Acceptance: nested work and executor transitions preserve the inherited properties; explicitly overridden
properties change only at named boundaries. No arbitrary ambient UI/thread-local context is captured.

### R06 — Precise execution transitions

`StartsOn` must control inner startup; `ContinuesOn` must control terminal delivery; `SwitchTo` must change the
current execution resource and subsequent default. Ordinary await must not implicitly restore the caller's
previous resource. Leaf I/O APIs must specify their completion resource. `YieldNow` must defer and cannot be
an inline no-op.

Acceptance: value, error, and stopped paths run on the documented resource, logical context survives each
transition, and a yield allows other eligible work to execute.

### R07 — Safe inline completion and fairness

Ready and previously pending completion must be eligible for inline continuation when the executor contract
permits it. Inline dispatch must require authorized execution context, complete result publication and required
retirement, no held internal locks, and available depth/work budgets. Queue submission semantics remain distinct.

Acceptance: immediate and pending paths obey the same execution/lifetime rules; forced deferral, long inline
chains, and reentrant completion cannot overflow the stack, starve other sources, or bypass `PollOnce` budgets.

## Ownership, cancellation, and results

### R08 — Concurrent work always has an owner

Normal spawning must attach work to a scope/root owner. The owner must retain execution state through actual
completion even if a result handle is dropped. Concurrent lifetime tracking must not be duplicated in ordinary
sequential helper frames.

Acceptance: dropping an operation handle neither detaches, cancels, nor frees running work; owner closure still
joins it. Borrowed operation state cannot outlive its owner.

### R09 — Cancellation cost follows cancellation boundaries

Scopes/owners normally own stop sources; descendants inherit tokens. Helpers must not create sources, linked
state, or callbacks merely to pass cancellation onward. Register only where a suspended operation needs
notification. A cannot-stop token must incur zero cancellation-specific allocation, registration, and
synchronization. Independent operation cancellation must be an explicit boundary.

Acceptance: compare cannot-stop, stoppable-but-not-stopped, and actively canceled work; registration counts
reflect pending notification sites rather than coroutine depth. Owned descendants receive scope stop.
Optional shielding must not bypass joining or runtime shutdown.

### R10 — Three terminal channels and safe completion

Each accepted operation must publish exactly one of `Value`, `Error`, or `Stopped`. Typed API errors and
infrastructure faults may differ within the error payload; they must not be separate terminal channels.
Expected domain failures may be ordinary values. `Task<void>` and all adapters remain observable/awaitable.

A stop request must not imply completion or overwrite an already-published result. Join completion must mean
the backend/callbacks no longer borrow operation resources. Callers must keep borrowed buffers alive until then.

Acceptance: success/error/stop races publish once; later requests do not replace results; no backend or callback
accesses releasable caller state. Generators, combinators, exception translation, and runners use the same model.

### R11 — Cancel work and cancel waiting separately

Stopping owned work must still require joining before resource release. Canceling only a wait may return early
only when another owner retains the operation/resources and guarantees cleanup. Observer state must unregister
safely without consuming the single-consumer result or canceling the operation implicitly.

Acceptance: a canceled observer can be destroyed while externally owned work continues and is eventually joined;
the wait-only API cannot create unowned running work.

### R12 — Asynchronous scope closure

The normal awaiting scope helper must close admission and join every child before returning. Early return,
failure, and cancellation must request stop for unfinished work before joining. A destructor must neither block
the executor nor silently detach. Destruction with live children must fail fast in release builds as a contract
violation; the normal helper must avoid it on all supported exit paths.

Acceptance: body success, early exit, exception, child error, and parent cancellation leave no live owned children.
The only executor thread never blocks waiting for work it must itself execute.

### R13 — Defined sibling failure policy

A child error must promptly request sibling stop. After joining, a body error takes precedence; otherwise the
lowest spawn-index observed child error is primary. Additional errors are retained in stable spawn order within
admitted child capacity. Errors outrank stopped outcomes; without errors, a stopped body/child makes the scope
stopped. Ordering is deterministic for errors actually produced, not a promise of identical race outcomes.

Acceptance: controlled concurrent failures verify precedence, bounded aggregation, prompt sibling cancellation,
and complete cleanup. An independent-failure supervisor requires a named policy.

### R14 — Observation is separate from racing

`FirstCompleted` must observe externally owned work without canceling other operations or consuming their values.
`Race` must own its competitors, select the first terminal notification, stop losers, and join all competitors.
Already-terminal ties use input order; concurrent selection uses one arbitration point. Neither means first-success.

Loser errors must remain observable. A successful/stopped winner plus a loser error returns an error while
retaining winner information; a winner error remains primary. Secondary diagnostics must be bounded.
Existing owning/canceling `WhenAny` callers must migrate to `Race`.

Acceptance: value/error/stopped winners, simultaneous completion, already-terminal ties, and loser cleanup
failures follow this policy; observation introduces storage only at its explicit boundary.

### R15 — Deadlines request stop and preserve ownership

Deadlines must use monotonic absolute time. Convert relative timeouts once; children inherit the earlier deadline.
Inherited deadlines must share owner timer/stop state; stricter local deadlines create bounded boundaries.
Expiry requests stop and may produce a stopped reason, not a fourth channel or forced resource destruction.

Acceptance: nested work does not restart timeouts or allocate timers per helper; early completion retires timers.
Structured timeout waits for cleanup even beyond its deadline; wait-only timeout satisfies R11. A result that
already won completion remains unchanged.

### R16 — Application-lifetime work remains supervised

Explicit detachment must transfer work to an application owner providing bounded admission, cancellation,
shutdown joining, and an unhandled-error sink with a defined fallback. It must not implicitly retain dangling
scope-local references. No ordinary unobservable `async void` or ownerless fire-and-forget operation is allowed.

Acceptance: supervisor shutdown joins transferred work; errors are reported without blocking cleanup progress;
retained buffers/context have an explicit lifetime covering the work.

## I/O, overload, and shutdown

### R17 — Bound real growth and guarantee delivery

Admission must bound roots/spawns, queues, suspended delivery, timers, external requests, worker jobs, retained
completions, and explicit observers/shared results. Document each limit and acquisition/release point. Reject
overload before OS work with an actionable error; never block the event loop waiting for capacity.

Accepted work must retain a deliverable deferred completion and cleanup path under saturation. Sequential
helpers/ready results must not each reserve generic capacity. Saturation must not force unauthorized inline
execution; unsupported custom-executor admission must fail before external work begins.

Acceptance: saturated cancellation, fairness deferral, and shutdown still complete admitted work within bounded
retained memory; complete fixture teardown returns allocation state to baseline.

### R18 — Preserve resource concurrency and retirement

The redesign must preserve shared-file-position ordering, independent-offset overlap, flush/close barriers,
supported socket duplex concurrency, and native/fallback retirement semantics. File wrapper simplification
must give its admission/cleanup responsibilities a concrete owner before removing it. A native cancellation
request is never permission to release an outstanding request's buffers.

Acceptance: ordering, close/submission races, independent operations, partial completion, and cancellation
behave consistently across supported backends. Benchmarks must not improve by rejecting supported overlaps.
Cross-handle filesystem consistency and premature caller buffer destruction remain caller/API concerns.

### R19 — Shutdown drains owned work

Shutdown must reject new work, request stop within the closing owner's boundary, and keep admitted completion,
unwinding, and joins progressing before releasing native resources. A cannot-stop user token must not bypass
runtime shutdown. Backend retirement, external delivery, and application joining must be distinguished.

External executors must stay alive/driven through their pending delivery and joins. Runtime shutdown must not
claim to join unrelated external application tasks. A timeout must report incomplete shutdown and retain
ownership rather than forcibly destroying running tasks. Preserve fixed loop-thread ownership and non-reentrant
driving; admitted cleanup must not need fresh ordinary queue capacity.

Acceptance: shutdown races, saturated queues, pending I/O, and external delivery obey these boundaries;
notification-driven idle behavior is retained.

## Evidence and completion

### R20 — Attribute performance costs before redesign

Preserve reproducible original, pre-redesign, and redesigned measurements with source identity, build options,
hardware, warmup, and multiple processes. Measure construction, sequential await, spawn/join, context flow,
cancellation, dispatch, and file wrapper costs separately. Record allocations/bytes, frame sizes, queue hops,
reservations, registrations, latency distributions, and CPU/contention profiles.

Acceptance: results distinguish ready/pending, native/fallback, runtime/external executor, worker counts, and
cancellation modes. Full-fixture allocation is not presented as per-operation cost. The current file slowdown's
dominant cause remains unknown until attributed; there is no implied .NET performance comparison.

### R21 — Preserve the original performance gates

Use matched fixtures and the original baseline revision `203f704e97a704259ef2db1ee8c93bf92763a7e1`, as recorded in
[IORuntimeMeasurements.md](../IORuntimeMeasurements.md). The slower pre-redesign implementation cannot reset them.

| Metric | Required result |
| --- | --- |
| p50/p95/p99 latency | At most 25% above original baseline |
| Mean latency | At most 20% above original baseline |
| Cold setup | At most 50% above original baseline |
| Idle CPU | At least 80% reduction |
| Periodic network wakeups | None |
| Median single-operation cancellation / idle shutdown | Each below 1 ms under the documented fixtures |

Acceptance: raw repeated Release evidence satisfies these gates plus R03/R04/R07/R09/R17 structural checks.
An improved helper microbenchmark cannot close a still-regressed file workload.

### R22 — Verify the affected surfaces and report platform limits

Use focused Async/Execution/IO/Net tests, controlled interleavings, bounded lifetime stress, and Linux ASan/UBSan;
verify public headers, installed consumers, the portable loop, and canonical Hello.IO modes using repository
workflows. Do not treat sanitizer success as proof that races are absent.

The user performs native Windows execution/tests in Windows. Do not execute Windows binaries, Wine, or emulation
locally. Supply native Windows instructions and record actual results. macOS stays unverified until native tests
run. Linux success must not be reported as validation of other native platforms.

Acceptance: evidence names the exact revision/configuration and passed, failed, skipped, and unverified checks.

### R23 — Complete the consumer and documentation transition

Update public APIs, error inspection, adapters/generators, combinators, runners, package consumers, examples,
and runtime/async/execution documentation with the implementation. Remove superseded machinery and temporary
instrumentation after preserving reproducible evidence; avoid permanent compatibility paths.

Acceptance: no in-scope consumer relies on superseded semantics; documentation states actual behavior.
Tasks remain incomplete while correctness/performance gates are unmet or required platform evidence is missing.
