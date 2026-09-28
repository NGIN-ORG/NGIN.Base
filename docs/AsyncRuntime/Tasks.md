# Async Runtime Tasks

Date: 2026-09-12

Status: Stages 0–4 (T01–T21) and final Linux correctness/integration (T22) are complete locally. Final consumer/documentation, native-platform, and performance gates remain open.
The full redesign and its performance/native-platform acceptance gates remain open.

[Requirements.md](Requirements.md) defines acceptance; [Design.md](Design.md) defines the implementation model.
[AsyncRuntimePlan.md](../AsyncRuntimePlan.md) is the original proposal snapshot. Track progress here.

## Execution rules

- Complete stage exit checks before starting the next stage. Dependencies below identify additional ordering
  within a stage. Work may be batched where changes must compile together; do not mark a task complete early.
- Inspect the relevant implementation, nearest contributor instructions, and canonical workflows before edits.
  Use C++23, existing components/dependencies, focused changes, and proportional verification.
- Attach source revision, exact command/configuration, outcome, and artifact location when completing a task.
  Record failures, skips, and unavailable platform evidence explicitly. Past checkpoint results do not verify
  new code. A known unmet acceptance gate remains open.
- Update affected consumers and public API documentation with each behavioral change. Final cleanup is not
  permission to leave an earlier stage with incompatible callers.
- API names below specify semantics; T03 finalizes signatures. Do not add compatibility paths to avoid updating
  in-scope consumers. Do not implement optional framework features without a concrete consumer.

## Stage 0 — Baselines and concrete contracts

### T01 — Freeze reproducible source and workload identities

Requirements: R01, R20, R21. Design: [D1](Design.md#d1--architecture-and-change-boundaries). Dependencies: none.

- [x] Record original Base revision `203f704e97a704259ef2db1ee8c93bf92763a7e1`, implementation checkpoint
  `7792846`, and actual benchmark sources, including any later diffs, compiler/options, hardware, and fixtures.
  Preserve existing measurement artifacts without altering unrelated working files or resetting the baseline.

Done when: another run can reconstruct both reference workloads and source identities; the original performance
thresholds are recorded alongside the results. Creating these planning documents alone does not complete T01.

Evidence (2026-09-12): [source manifest](../measurements/async-runtime-source.json) records full revisions,
775 input hashes, original/checkpoint/source archive hashes, compiler, hardware, and build-cache identity.
The archives, original working diff, and cache are retained under
`build/async-runtime-evidence/pre-redesign/`. See [Measurements.md](Measurements.md) for reconstruction
and unchanged workload gates. No source reset, commit, or branch was used.

### T02 — Isolate costs and profile the current implementation

Requirements: R03, R04, R09, R20. Design: [D1](Design.md#d1--architecture-and-change-boundaries). Depends on: T01.

- [x] Measure task construction/destruction, child await, spawn/join, environment inheritance, cancellation
  registration/linking, ready/pending dispatch, and file wrapper execution separately. Exercise depths 1/8/64,
  one/multiple workers, runtime/external executors, and all three cancellation modes.

Done when: artifacts report frame sizes, allocations/bytes, queue hops, reservations, registrations, latency
distributions, and CPU/contention profiles. Separate counter instrumentation from timing and full-fixture from
per-operation costs. State what explains the file slowdown and what remains uncertain.

Evidence (2026-09-12): [Measurements.md](Measurements.md) records five Release processes per timing/allocation
binary, separate registration instrumentation, callback/submission/reservation counts, compiler frame allocation,
controlled pending cancellation, scope join, runtime/external executors, and CPU/synchronization profiles.
The synthetic gate/public wrappers are isolated from OS transfer; native/fallback profiles remain too sparse
to identify the dominant historical regression. No file performance gate is closed.

### T03 — Finalize API signatures and ownership protocols

Requirements: R02–R19. Design: [D2](Design.md#d2--execution-model), [D6](Design.md#d6--publication-and-ownership-transitions),
[D7](Design.md#d7--implementation-decisions-to-settle-from-evidence). Depends on: T02.

- [x] Specify task/operation/environment APIs, typed error payloads, executor transitions, scope helper/closure,
  observer/race results, deadline boundaries, supervisor error fallback, and shutdown status. Write concrete
  caller examples and the publication/cancellation/unregister protocol, including capacity reuse/transfer.

Done when: every running/pending object and borrowed resource has an owner and release point; completion before
submission returns and saturated deferral have explicit protocols. Resolve return/early-exit behavior of the
scope helper without relying on an awaiting destructor. Record implementation decisions in Design.

Evidence (2026-09-12): [Contracts.md](Contracts.md), linked from Design, specifies signatures, inline and
concurrent error storage, chain-slot reuse/transfer, submission publication, cancellation/unregister, scope
closure and C++ local-lifetime restrictions, notification arbitration, deadlines, and supervised shutdown.
Dispatch budget candidates remain subject to T08/T09 measurements; these are target contracts, not current API claims.

### T04 — Establish stage baselines and focused regression fixtures

Requirements: R20–R22. Design: [D8](Design.md#d8--validation-strategy). Depends on: T01–T03.

- [x] Add reusable measurement and controlled-interleaving fixtures, run the current implementation baseline,
  and identify the observable assertions each later stage must change or preserve.

Done when: evidence can distinguish compiler frame costs from runtime machinery and reproduce relevant
correctness/performance cases. No assumption about cold versus eager startup substitutes for measurement.

Evidence (2026-09-12): the two AsyncRuntime benchmark targets provide reusable cost, source-mode, controlled
pending-cancellation, and scope-join fixtures. The [contract regression map](Contracts.md#caller-examples-and-regression-map)
identifies preserved and changed assertions in existing controlled-interleaving tests. Both final benchmark
binaries passed Debug ASan/UBSan. The current focused Debug CTest run selected 332 tests: 326 passed, six
skipped (Windows IOCP and five configured TLS integration cases), zero failures. Commands and logs are in
[Measurements.md](Measurements.md#baseline-verification).

Stage exit: attributable measurements, concrete API/lifetime contracts, and reproducible fixtures exist.

## Stage 1 — Sequential execution and environment

### T05 — Separate sequential task state and add ready results

Requirements: R02–R04, R08. Design: [D2](Design.md#d2--execution-model). Depends on: Stage 0.

- [x] Implement direct child transfer and the ordinary API's inline ready representation. Keep concurrent
  ownership/publication state at operation boundaries; remove mandatory helper queue items and reservations.

Done when: cold construction is inert, starts/results are single-consumer, parent-child lifetime is retained,
and ready results require no heap allocation or scheduling. Deep transfer uses bounded stack space.

Partial checkpoint (2026-09-13): [Stage1.md](Stage1.md) records direct same-executor transfer, root-slot reuse,
256-transfer deferral, bounded failure retirement, throwing-value propagation, and a verified shutdown-discard
race fix. Debug: 332 passed/six skipped; focused ASan/UBSan: 29 passed; shutdown interleaving: 100 passed.
Follow-up checkpoint: lvalue and cancellation-aware awaits now consume their Task. Repeated completed/pending
awaits report InvalidTaskUsage without disturbing the first parent. Debug: 334 passed/six skipped; focused
ASan/UBSan: 31 passed. [Consuming-await evidence](Stage1.md#consuming-await-checkpoint--2026-09-13).
Inline-ready checkpoint: FromValue/FromCompletion and inline ready Operation transfer are implemented,
including throwing/move-only payload handling and scope retirement. Debug: 341 passed/six skipped;
ASan/UBSan: 38 passed. All five counted ready cases allocate zero bytes. Task<int> is now 144 bytes;
compact state remains necessary. [Ready evidence](Stage1.md#inline-ready-checkpoint--2026-09-13).
Compact-state checkpoint: promise size is 240 bytes, Task<int> is 88 bytes, and one 192-byte ExecutionState
belongs to each running root/boundary. Successful children with outstanding frame leases now retire before
parent resumption. Debug: 342 passed/six skipped; ASan/UBSan: 56 passed. [Split evidence](Stage1.md#compact-state-and-success-retirement-checkpoint--2026-09-13).
Completed (2026-09-13): compiled execution-state extraction and local acceptance review are recorded in
[T05 completion](Stage1.md#t05-completion--2026-09-13). Current checks: 52 focused Debug and 72 ASan/UBSan
passes, static/shared installed Execution consumers pass, and ready/chain allocation counts remain intact.
Later structured-owner, environment, transition, fairness, and platform gates remain assigned to their tasks.

### T06 — Introduce the inherited logical environment

Requirements: R04, R05, R09. Design: [D2](Design.md#d2--execution-model). Depends on: T05.

- [x] Adapt `TaskContext` and child startup to inherit scheduler suggestion, token/deadline, allocation resource,
  and optional metadata. Define storage lifetime and explicit construction allocator entry points where supported.

Done when: absent properties do not allocate; helpers do not create linked cancellation state; metadata remains
valid across suspension. Document that an inherited allocator cannot retroactively allocate a cold frame.

Evidence (2026-09-13): [T06 environment implementation](Stage1.md#t06-environment-inheritance--2026-09-13)
records property snapshots through depth 64/yield, PMR allocation/retirement, invalid-resource rejection,
348 Debug passes/six skips, 77 sanitizer passes, and static/shared installed consumers. Empty environment
copies add zero allocations; cold compiler frames retain their construction-time allocator. Transition,
owner/cancellation-boundary, and deadline policy remain with their later tasks.

### T07 — Implement explicit executor transitions

Requirements: R05–R07. Design: [D2](Design.md#d2--execution-model). Depends on: T06.

- [x] Implement `StartsOn`, `ContinuesOn`, and `SwitchTo` with their distinct contracts; retain a real `YieldNow`.
  Update affected awaiters and callers and document each leaf I/O completion resource.

Done when: value/error/stopped paths obey execution placement and preserve logical metadata; ordinary await
does not silently restore the previous resource; yielding permits other eligible work.

Partial evidence (2026-09-13): [core transitions](Stage1.md#t07-core-transition-checkpoint--2026-09-13)
implement AsCompletion, StartsOn, ContinuesOn, SwitchTo, and environment-based YieldNow. Current checks:
354 Debug passes/six skips, 83 sanitizer passes; ready/chain structural counts remain intact.
The [leaf placement checkpoint](Stage1.md#t07-leaf-placement-checkpoint--2026-09-13) adds public
file/socket/resolver entry transitions: 357 Debug passes/six skips and 14 focused sanitizer passes.
The [transition validation checkpoint](Stage1.md#t07-transition-validation--2026-09-13) adds one-/four-worker
transition coverage, throwing terminal value moves, and passing installed static/shared consumers. Six
focused transition cases pass in Debug and ASan/UBSan. T08/T09 retain the general dispatch and fairness
gates; native platform acceptance remains a final-stage requirement.

### T08 — Implement safe inline and guaranteed deferred dispatch

Requirements: R03, R07, R17. Design: [D2](Design.md#d2--execution-model),
[D6](Design.md#d6--publication-and-ownership-transitions). Depends on: T05–T07.

- [x] Separate enqueue from eligible inline dispatch; integrate the publication handshake, authorized-context
  check, depth/work budgets, and boundary-owned deferred capacity into `ExecutorRef`, schedulers, and the loop.

Done when: no continuation runs under internal locks or on an unauthorized resource; completion during submission,
executor rejection/discard, saturated deferral, and frame destruction preserve ownership and exactly-once resume.

Implementation evidence: [cooperative queue fairness](Stage1.md#cooperative-queue-fairness--2026-09-13)
rotates reserved completions, due timers, and FIFO ordinary work. Eligible inline dispatch and published
task completion now use shared dispatch budgets ([evidence](Stage1.md#eligible-inline-dispatch--2026-09-13)). [Poll transfer accounting](Stage1.md#poll-transfer-accounting--2026-09-13)
now charges eligible sequential transfers to the current batch. Cooperative pumps additionally share the
allowance and limit recursive dispatch depth to 16. Thread-pool worker/helper dispatch now uses the same
guard ([evidence](Stage1.md#thread-pool-dispatch-budgets--2026-09-13)); complete inline-dispatch integration
remains under audit. File retirement callbacks now attempt eligible inline delivery
with reserved fallback ([evidence](Stage1.md#file-inline-delivery--2026-09-13)); immediate and pending
socket delivery now follows the same contract ([evidence](Stage1.md#socket-inline-delivery--2026-09-13)).
Resolver publication now waits for setup commit and uses reserved delivery; positive timeouts use
removable timers, newly supported on ThreadPoolScheduler
([evidence](Stage1.md#resolver-publication-and-timer-retirement--2026-09-13)). Cooperative timer removal
is now implemented ([evidence](Stage1.md#cooperative-removable-timers--2026-09-13)). Discarded resolver
jobs now publish a terminal fault ([evidence](Stage1.md#resolver-job-discard--2026-09-13)). InlineScheduler
now rejects queued submission and offers explicit Invoke
([evidence](Stage1.md#enqueue-contract--2026-09-13)). Timer waits now support eligible inline delivery after
setup/retirement ([evidence](Stage1.md#timer-wait-inline-delivery--2026-09-13)). Runtime callbacks now share
the depth guard across distinct nested loops ([evidence](Stage1.md#runtime-callback-depth--2026-09-13)).
Generator consumer delivery now attempts eligible inline dispatch after releasing producer execution ownership
([evidence](Stage1.md#generator-consumer-inline-delivery--2026-09-13)). The [completed dispatch audit](DispatchAudit.md) classifies the remaining queued boundaries and covers
the final TaskScope/WhenAny join changes. Local verification: 138 Async tests and 18 focused ASan/UBSan
tests passed; see [stage record](Stage1.md#t08-dispatch-audit-and-final-joins--2026-09-13). T09 tuning and native-platform gates remain open.

### T09 — Verify composition cost and fairness

Requirements: R03–R07, R17, R20, R22. Design: [D8](Design.md#d8--validation-strategy). Depends on: T05–T08.

- [x] Run focused transfer/transition tests and depth/cost measurements; stress long inline chains alongside
  timers and unrelated runnable work. Include the bounded `PollOnce` contract and explicit yields.

Done when: eligible helpers add no per-layer hop/reservation/registration, stack use is bounded, and fairness
budgets provide observable progress. Record budget choices and measured tradeoffs in Design.

Stage exit: direct composition and transitions are verified together with lifetime and saturation behavior.

Evidence: [Stage 1 measurements](Measurements.md#stage-1-composition-and-fairness-t09-2026-09-13) record
structural counts, allocation separation, five-process timing, and poll-budget tradeoffs. Stage tests:
383 passed, six existing skips. The one-worker depth-1 mean increased about 20.8% against Stage 0;
retain that finding for T23's matched original-baseline investigation rather than claiming overall performance acceptance.

## Stage 2 — Cancellation and terminal results

### T10 — Move cancellation support to real boundaries

Requirements: R05, R08, R09. Design: [D3](Design.md#d3--cancellation-completion-and-ownership). Depends on: Stage 1.

- [x] Keep stop authority with owners; inherit tokens in helpers and register only at suspended notification
  sites. Link sources once per independent boundary and retain safe callback unregister behavior.

Done when: cannot-stop execution has zero cancellation-specific allocation, registration, and synchronization;
stoppable descendants receive owner cancellation without callback growth proportional to helper depth.

Evidence: [Stage 2 cancellation boundaries](Stage2.md#t10-cancellation-boundaries--2026-09-13) records
source/link ownership, depth-independent registration counts, and 52 passing Debug plus 52 ASan/UBSan tests.

### T11 — Unify value, error, and stopped completion

Requirements: R10, R23. Design: [D3](Design.md#d3--cancellation-completion-and-ownership). Depends on: T10.

- [x] Collapse `DomainError`/`Fault` terminal categories into one error channel while preserving typed payloads.
  Update task/operation outcomes, void tasks, generators, adapters, exception translation, combinators, and runners.

Done when: all callers inspect the same three terminal channels, expected domain values remain possible,
and cancellation requests cannot overwrite a published value/error. No obsolete fourth-channel API remains.

Evidence: [terminal-channel migration](Stage2.md#t11-terminal-channel-migration--2026-09-13):
390 Debug passes plus six existing skips; 93 ASan/UBSan passes; static/shared installed consumers and Hello.IO
smoke pass. NGIN.UI call sites are migrated, but its package build is stopped by unchanged UIError-to-expected
return errors; native Windows/macOS checks remain unverified.

### T12 — Verify cancellation, publication, and retirement races

Requirements: R07, R09–R11, R17, R22. Design: [D6](Design.md#d6--publication-and-ownership-transitions).
Depends on: T10–T11.

- [x] Exercise cancellation before registration, during submission, after native completion, during unregister,
  and after terminal publication. Cover concurrent delivery/discard and callback-triggered frame destruction.

Done when: exactly one terminal outcome is observed, all required completion paths progress, and borrowed
resources become releasable only after backend/callback access ends. Attach focused sanitizer/stress evidence.

Stage exit: cancellation is conditional in cost and cooperative in outcome, with one shared completion model.

Evidence: [coordinated Stage 2 race verification](Stage2.md#t12-coordinated-race-verification--2026-09-13)
records the ordering matrix, deterministic outcome/native-adapter additions, Debug checks, and sanitizer
runs (227 main, 13 socket, eight refreshed native cases; native coverage overlaps the main run).

## Stage 3 — Structured concurrency and lifecycle policy

### T13 — Implement structured spawn and asynchronous scope closure

Requirements: R08, R12, R13. Design: [D3](Design.md#d3--cancellation-completion-and-ownership). Depends on: Stage 2.

- [x] Implement the awaiting scope helper, bounded child ownership, admission closure, stop/join on early exit,
  and deterministic error ordering. Keep release-build fail-fast behavior for unjoined low-level scope misuse.

Done when: dropped result handles remain owned; body exits/exceptions and child failures join all children;
siblings receive stop promptly and aggregate results follow R13. Destructors never block or detach implicitly.

Implementation: [scope reports and asynchronous closure](Stage3.md#t13-scope-reports-and-asynchronous-closure--2026-09-13)
adds WithScope, deterministic retained reports, environment inheritance, and error/stop precedence.
The follow-up scoped Spawn API returns singly consumed result handles, supports cold Task inputs, and
publishes consumer readiness after frame retirement while retaining errors for Join. The focused batch
passes 35 Debug and 35 ASan/UBSan tests. Scoped admission now reserves execution storage and uses
its guaranteed continuation for startup. SyncWait and RunTask now use scoped outer ownership. Global Spawn callers and the API have now been removed; public Operation is result-only.
The [T13 closure audit](Stage3Audit.md) is complete locally. Owner/report allocation now uses
the environment resource, including cancellation ownership and retained diagnostic storage. Async
checkpoints and linked tokens now preserve the first stop origin; scope closure uses OwnerClosed. File, socket, and resolver adapters now propagate stop reasons;
Windows/macOS native execution remains open. The six Linux OpenSSL-enabled TLS behavior tests pass;
sequential file/TLS wrappers now consume Tasks directly instead of calling global Spawn. WhenAll now
uses scoped admission and joins on admission/result-consumption failure. Race now replaces WhenAny;
all global Spawn consumers have been migrated.

### T14 — Implement observation and wait-only cancellation

Requirements: R02, R11, R14, R17. Design: [D3](Design.md#d3--cancellation-completion-and-ownership). Depends on: T13.

- [x] Add bounded explicit observer state and `FirstCompleted` over externally owned operations. Provide separate
  wait-only cancellation that safely unregisters without consuming the result or canceling underlying work.

Done when: early observer return retains an independent cleanup owner; input-order ties and concurrent winner
arbitration are defined; ordinary tasks do not acquire shared-result/observer machinery.

Implemented for scope-owned Operation handles: notification references share existing result storage,
while pending observation allocates bounded records at its own boundary. Atomic publication arbitration,
wait-only cancellation, callback unregistration, retained terminal kinds, and allocation rollback are
covered by focused Debug and sanitizer tests. Legacy producer handles remain unobservable until their
removal in the T13/T15 consumer migration. Evidence: [Stage 3](Stage3.md#non-consuming-observation--2026-09-13).

### T15 — Implement owning Race and migrate WhenAny

Requirements: R10, R13, R14, R23. Design: [D3](Design.md#d3--cancellation-completion-and-ownership). Depends on: T13–T14.

- [x] Select the first terminal competitor, request loser stop, join all competitors, and preserve bounded
  secondary errors according to R14. Replace the old owning/canceling `WhenAny` call sites with `Race`.

Done when: value/error/stopped winners, precompleted ties, concurrent publication, and loser errors match the
contract; the race cannot return while a competitor still borrows resources.

Race now shares TaskScope child admission and joining, with an internal batch prepare/start/abort path.
Its atomic terminal hook selects before queued observers or held-frame retirement. Typed winner values,
error-winner priority, loser-error overrides, stopped winner information, move-only payload retention,
and rollback of every boundary allocation are implemented. WhenAny's header, symbol, tests, and benchmark
callers are migrated. Local validation and remaining matrix limits are recorded in [Stage 3](Stage3.md).

### T16 — Implement inherited deadlines

Requirements: R09–R11, R15, R17. Design: [D4](Design.md#d4--deadlines-detached-work-and-shutdown). Depends on: T13–T14.

- [x] Convert timeout durations once to monotonic deadlines, share inherited timer/stop state, and create bounded
  local state only for stricter deadlines. Retire timers on early completion and report stopped reasons.

Done when: helper depth does not multiply timers or extend a deadline; expiry preserves completion races and
structured ownership; cleanup may exceed the deadline while wait-only timeout requires another owner.

WithDeadline/WithTimeout now create a compiled, PMR-backed timer boundary only for stricter local
deadlines. Nested helpers share inherited token/timer state, duration conversion happens at execution,
and timer callback retirement is joined after inner completion. Expiry preserves produced values/errors;
allocation and timer admission failures start no inner work. Focused Debug/sanitizer evidence is recorded
in [Stage 3](Stage3.md#inherited-deadlines--2026-09-13); platform-wide and final performance gates remain open.

### T17 — Make application-lifetime transfer explicitly supervised

Requirements: R08, R13, R16, R17. Design: [D4](Design.md#d4--deadlines-detached-work-and-shutdown). Depends on: T13–T16.

- [x] Reuse/extend the root owner for bounded application-lifetime work, explicit lifetime transfer,
  cancellation, shutdown join, and a documented nonblocking unhandled-error sink/fallback.

Done when: no naked detach or unobservable void task remains; transferred resources outlive the work;
reporting failure cannot silently lose an error or block the sole cleanup executor.

TaskSupervisor now reuses TaskScope admission/retirement records with recycling after successful or
acknowledged completion. Spawn/SpawnOn return owned result handles; Transfer explicitly relinquishes
result interest. Nonblocking noexcept sinks receive retained errors after producer retirement; missing
or rejecting sinks close admission, request stop, and retain bounded shutdown diagnostics. The four Base
detach fixtures now use supervisor transfer and joining. Application now owns a supervisor; its UI
executor supports reserved delivery/removable timers, and the four gallery motion actions transfer to
that owner. Validation/command/view-model callers now require explicit supervisor ownership,
and naked Detach and its friend declarations are removed. Sequential UI Spawn/await
pairs use AsCompletion. The full UI library/test executable now builds against std::expected. Late UI
errors reach the supervisor after observer destruction/supersession; setup
failure cannot publish running state, and stopped validation remains unavailable.
Motion now reserves completion delivery and cancellation registration before
publishing an entry, reports admission faults, and targets cancellation by operation
identity. Global ownerless Spawn is removed. T17 is complete locally; the [Stage 3 audit](Stage3Audit.md) records the final 215-test UI integration pass.

### T18 — Integrate owner and runtime shutdown contracts

Requirements: R12, R16, R17, R19. Design: [D4](Design.md#d4--deadlines-detached-work-and-shutdown). Depends on: T13–T17.

- [x] Close the appropriate admission boundaries, request stop, and retain delivery/unwinding/join progress.
  Distinguish backend stopped, externally delivered, and application joined; expose incomplete timed shutdown.

Done when: external executors remain driven through cleanup, cannot-stop user tokens cannot bypass runtime
retirement, and no timeout destroys live cooperative tasks or requires fresh ordinary queue capacity to unwind.

Timed drainage checkpoint: [T18 timed runtime drainage](Stage3.md#t18-timed-runtime-drainage--2026-09-13)
adds ShutdownUntil with explicit incomplete status and retained cleanup ownership.
47 focused Debug cases pass. The [Stage 3 audit](Stage3Audit.md) records passing file-shutdown, UI owner, and combined async integration checks, completing T18 locally.

Stage exit: scopes, observations, races, deadlines, supervised work, and shutdown share one ownership protocol.

## Stage 4 — File and socket integration

### T19 — Adapt native and fallback completion delivery

Requirements: R05–R07, R09, R10, R17–R19. Design: [D5](Design.md#d5--io-integration-and-resource-limits).
Depends on: Stage 3.

- [x] Integrate the new environment/dispatch model into file and socket backends without replacing functioning
  OS mechanisms. Preserve native cancellation retirement, external executor delivery, and host notification.

Done when: ready/pending and native/fallback paths obey the same outcome/ownership rules, supported socket duplex
work still overlaps, and native cancellation cannot release buffers before backend completion.

### T20 — Integrate file admission and cleanup into the backend operation

Requirements: R03, R17, R18, R20. Design: [D5](Design.md#d5--io-integration-and-resource-limits). Depends on: T19.

- [x] Use the measured file breakdown to place gate lease, result, cancellation, and retirement with the actual
  backend operation. Remove the redundant wrapper/spawn after accounting for each lifetime/close responsibility.

Done when: shared-position ordering, independent-offset overlap, flush/close barriers, rejected admission, and
close/cancel races remain correct. Record the latency/allocation change; do not substitute blanket serialization.

### T21 — Audit and verify bounded admission under load

Requirements: R07, R17–R19, R22. Design: [D5](Design.md#d5--io-integration-and-resource-limits),
[D6](Design.md#d6--publication-and-ownership-transitions). Depends on: T19–T20.

- [x] Document acquisition/release and guaranteed delivery for every growing resource. Exercise saturated
  requests, worker jobs, timers, observers, runnable work, retained completions, and closing owners.

Done when: rejected work never starts OS access, admitted work always retires and delivers, required deferral
does not steal fresh ordinary capacity, and complete teardown returns retained allocation state to baseline.

Stage exit: integrated I/O preserves resource semantics and progresses under overload, cancellation, and shutdown.

Local completion: [Stage4Audit.md](Stage4Audit.md) maps T19–T21 to implementation,
resource bounds, and passing coverage (64 cases in the final Debug batch). Final
Linux integration and native Windows/macOS/performance acceptance remain T22–T26.

## Stage 5 — Final evidence and consumer completion

### T22 — Run the Linux correctness and integration checks

Requirements: R01–R19, R22. Design: [D8](Design.md#d8--validation-strategy). Depends on: Stage 4.

- [x] Run repository-defined focused Async/Execution/IO/Net tests, Linux ASan/UBSan, relevant controlled race and
  bounded stress cases, public headers, installed IO/Net/IORuntime consumers, portable-loop tests, and Hello.IO modes.

Done when: results identify the exact source/configuration and all failures/skips. Reuse valid build trees;
do not run unrelated broad suites without a reason or treat sanitizers as proof of race freedom.

Current evidence: [Stage5.md](Stage5.md) records the fixed-source Linux build,
466 passing CTest checks/six explicit skips, six TLS-enabled passes, independent
public-header compilation, and passing installed IO/Net/IORuntime consumers.
Completed (2026-09-29): the same source fingerprint passed the final ASan/UBSan batch
(466 passes, six explicit skips, zero failures), including stop-origin, observer-saturation,
controlled race, bounded stress, and portable-loop coverage. Canonical Hello.IO validation,
build, run, and smoke test passed with all three execution modes joined. Commands,
configuration, and evidence are recorded in [Stage5.md](Stage5.md#t22-completion--2026-09-29).
T23–T27 remain open; this closes Linux correctness/integration only.

### T23 — Demonstrate end-to-end performance acceptance

Requirements: R03, R04, R07, R09, R17, R20, R21. Design: [D8](Design.md#d8--validation-strategy). Depends on: T22.

- [ ] Repeat matched Release original/checkpoint/redesign runs with warmup and multiple processes. Measure file
  and socket latency, helper/spawn cost, mixed-load fairness, cold setup, idle CPU/wakeups, cancellation, shutdown,
  and overloaded memory. Publish raw evidence and analysis with unchanged baseline gates.

Done when: R21 and the structural cost/progress requirements pass. Leave this task open if a file workload still
regresses beyond its threshold, even when individual helpers improve. Make no unmeasured .NET comparison.

Measured (2026-09-29), **acceptance failed; task remains open**:
[Performance.md](Performance.md) records 60 successful serial Release timing processes
and three separate passing allocation processes, reconstructed original/checkpoint
controls, 100-operation latency warmups, and unchanged baseline gates. Against the
matched original, mean native read/fallback read/fallback write regress
21.7%/31.3%/41.6%; file metadata and suspended receives also fail latency gates.
Historical numeric file limits remain unmet. Ready results allocate zero bytes,
helper dispatch stays depth-independent, overload teardown returns tracked memory
to baseline, and idle CPU/cancellation/shutdown checks pass. Fixed root allocation
cost increased; its contribution to end-to-end regressions remains to be isolated.

### T24 — Finish documentation and remove superseded machinery

Requirements: R01, R23. Design: [D1](Design.md#d1--architecture-and-change-boundaries),
[D7](Design.md#d7--implementation-decisions-to-settle-from-evidence). Depends on: T22–T23.

- [ ] Reconcile Async, Execution, IORuntime, IORuntimeContracts, public API docs, package consumers, and examples
  with implemented behavior. Remove replaced APIs/paths and temporary instrumentation after preserving evidence.

Done when: no in-scope consumer or current documentation relies on superseded behavior; historical plans are
clearly identified; final diff and affected checks contain no unrelated churn or weakened contracts.

### T25 — Obtain native Windows verification

Requirements: R18, R19, R22. Design: [D8](Design.md#d8--validation-strategy). Depends on: T22–T24.

- [ ] Provide the user with repository-defined native Windows build/test/benchmark instructions for the exact
  revision. Record the user's results for IOCP, cancellation/retirement, runtime modes, and relevant performance.

Done when: native results are attached and material failures resolved/rechecked. Do not run Windows binaries,
Wine, or emulation locally. Until the user supplies results, this task remains awaiting native verification.

### T26 — Record native macOS verification

Requirements: R18, R19, R22. Design: [D8](Design.md#d8--validation-strategy). Depends on: T22–T24.

- [ ] Run or obtain repository-defined native macOS backend/host-loop, cancellation/shutdown, and relevant
  performance checks for the implementation revision and record results.

Done when: native evidence is available and material failures are resolved. If no environment is available,
record macOS as unverified and leave this task open; portable Linux tests do not replace it.

### T27 — Close the redesign against the requirements

Requirements: R01–R23. Design: [D8](Design.md#d8--validation-strategy). Depends on: T01–T26.

- [ ] Review every requirement against implementation and evidence, inspect the final diff, reconcile this
  checklist, and report any remaining limitations or open acceptance gates.

Done when: all required behavior, documentation, consumers, performance gates, and platform evidence are complete.
Linux implementation completion and outstanding native platform validation may be reported separately; they do
not justify marking the entire redesign complete.

## Requirement coverage

This table lists the primary delivery/check tasks. Additional requirement references appear in task bodies.

| Requirement | Primary tasks |
| --- | --- |
| R01 — Foundation and scope | T01, T19, T22, T24 |
| R02 — Cold and single-consumer | T05, T14 |
| R03 — Direct composition | T02, T05, T08, T09, T23 |
| R04 — Ready allocation | T02, T05, T06, T23 |
| R05 — Environment | T06, T07, T10, T19 |
| R06 — Transitions | T07, T09, T19 |
| R07 — Inline dispatch/fairness | T08, T09, T12, T21, T23 |
| R08 — Concurrent ownership | T05, T10, T13, T17 |
| R09 — Cancellation cost | T02, T10, T12, T16, T23 |
| R10 — Terminal result/retirement | T11, T12, T15, T19 |
| R11 — Wait cancellation | T12, T14, T16 |
| R12 — Scope closure | T13, T18 |
| R13 — Sibling failures | T13, T15, T17 |
| R14 — Observation/racing | T14, T15 |
| R15 — Deadlines | T16 |
| R16 — Supervised lifetime | T17, T18 |
| R17 — Bounds/delivery | T08, T14, T16–T18, T21, T23 |
| R18 — Resource semantics | T19–T22, T25, T26 |
| R19 — Shutdown | T18, T19, T21, T25, T26 |
| R20 — Cost attribution | T01, T02, T04, T20, T23 |
| R21 — Performance gates | T01, T04, T23 |
| R22 — Verification/platforms | T04, T09, T12, T21, T22, T25, T26 |
| R23 — Consumer transition | T11, T15, T24, T27 |

## Evidence record

Add one entry per completed task or verification checkpoint. Do not replace raw failing evidence with a later
passing result; identify the fix and new revision so the comparison remains reproducible.

| Task | Source/configuration | Command or procedure | Result and artifact |
| --- | --- | --- | --- |
| T05 partial | b44612e plus [checkpoint input hashes](../measurements/async-runtime-direct-source.json) | [Stage 1 commands](Stage1.md#source-and-verification) | 332 Debug passes, six skips; 29 sanitizer passes; 100 shutdown stress passes; task remains open |
| T05 partial follow-up | b44612e plus [consumption input hashes](../measurements/async-runtime-consumption-source.json) | [Consuming-await commands/results](Stage1.md#consuming-await-checkpoint--2026-09-13) | 334 Debug passes, six skips; 31 sanitizer passes; ready/compact state remain open |
| T05 partial ready results | b44612e plus [ready input hashes](../measurements/async-runtime-ready-source.json) | [Ready commands/results](Stage1.md#inline-ready-checkpoint--2026-09-13) | 341 Debug passes, six skips; 38 sanitizer passes; five ready cases allocate zero bytes; compact state remains open |
| T05 compact-state checkpoint | b44612e plus [split input hashes](../measurements/async-runtime-split-source.json) | [Split commands/results](Stage1.md#compact-state-and-success-retirement-checkpoint--2026-09-13) | 342 Debug passes, six skips; 56 sanitizer passes; successful leases retire before parent resumption; compiled extraction/review remain |
| T05 complete | b44612e plus [compiled-state input hashes](../measurements/async-runtime-compiled-state-source.json) | [T05 commands and review](Stage1.md#t05-completion--2026-09-13) | 52 focused Debug passes; 72 sanitizer passes; static/shared installed consumers pass; ready cases remain allocation-free |
| T06 complete | b44612e plus [environment input hashes](../measurements/async-runtime-environment-source.json) | [T06 commands and evidence](Stage1.md#t06-environment-inheritance--2026-09-13) | 348 Debug passes, six skips; 77 sanitizer passes; static/shared installed consumers; environment copies allocate zero bytes |
| T07 partial core | b44612e plus [transition input hashes](../measurements/async-runtime-transitions-source.json) | [Core transition commands/evidence](Stage1.md#t07-core-transition-checkpoint--2026-09-13) | 354 Debug passes, six skips; 83 sanitizer passes; I/O placement and remaining transition checks open |
