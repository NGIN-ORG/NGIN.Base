# Phased async runtime optimization

Date: 2026-09-29; reordered 2026-10-04.

Status: Phase 0 and the Phase 1/3 checkpoint are complete; Phase 2 is complete for
native transfers and blocked on a contract decision for fallback files and stat. See
[Checkpoint report — 2026-10-04](#checkpoint-report--2026-10-04). T23 remains open.
This is follow-up work within **T23**, not a replacement acceptance contract.
[Requirements.md](Requirements.md) and the original performance limits still apply.
[Performance.md](Performance.md) records the current measurements and failures.

## Objective and baseline

Recover the failing T23 latency and cold-setup gates without changing observable
ownership, cancellation, admission, executor placement, fairness, result consumption,
or resource retirement. Work is ordered by the size of the regression each phase
can plausibly recover, not by implementation risk alone.

The saved starting implementation is Base `8afcf42`; `df9383d` adds its evidence.
Parent repository `6100f66` records the corresponding UI and example integration.
Use the full source identities from the T23 artifacts when identifying measured
inputs. Original NGIN `203f704e97a704259ef2db1ee8c93bf92763a7e1` remains the acceptance
reference; the frozen pre-redesign checkpoint (Base `b44612e`) remains a diagnostic
comparison.

## Where the regression is

T23 mean latencies (microseconds) split the regression into the part already present
at the checkpoint and the part introduced by the redesign:

| Workload | Original | Checkpoint | Redesign | Before redesign | In redesign |
| --- | ---: | ---: | ---: | ---: | ---: |
| Suspended receive, 8 sockets | 43.3 | 45.7 | 64.1 | +2.4 | **+18.4** |
| Suspended receive, 1,024 sockets | 43.9 | 42.8 | 57.9 | -1.1 | **+15.1** |
| File stat, Auto | 56.1 | 70.9 | 82.3 | **+14.8** | +11.4 |
| File stat, fallback | 56.8 | 72.0 | 78.7 | **+15.2** | +6.7 |
| Fallback read, 64 KiB | 64.5 | 84.5 | 84.6 | **+20.1** | +0.1 |
| Fallback write, 64 KiB | 60.2 | 83.1 | 85.2 | **+22.9** | +2.1 |
| Native read, 64 KiB | 65.4 | 76.5 | 79.5 | **+11.1** | +3.0 |
| Ready UDP | 30.3 | 34.7 | 37.4 | +4.4 | +2.7 |
| Spawn/yield/join | 27.0 | 27.9 | 32.2 | +0.9 | +4.3 |

Native file cold-open follows the same pattern: 301.7 / 449.8 / 465.2 us.

Consequences for this plan:

- The failing gates miss by roughly 10–25 us per operation. The redesign's entire
  fixed root ownership cost on the runtime executor is about 0.9 us (scope
  spawn/join 1.033 -> 1.917 us). Removing root allocations cannot close these gaps.
- Most of the file regression, and nearly all of the fallback transfer regression,
  predates the redesign. No change limited to redesign internals can recover it.
- Suspended receive is the largest regression the redesign introduced, and the
  spawn/yield/join regression is concentrated in p95/p99 tails.
- At these magnitudes the likely costs are executor transfers, cross-thread wakeups,
  and context switches (each typically several microseconds), not allocation.
  This is a hypothesis for Phase 0 to confirm or reject, not an established cause.

The columns compare means of five-process medians. They direct investigation; they
do not prove where time is spent.

## Rules shared by every phase

1. Change one mechanism at a time. Review and measure each phase before beginning
   the next; split a phase further if attribution would otherwise be ambiguous.
2. State the target before implementing: the T23 metric(s) the change is expected
   to move, the direction, and the approximate size, derived from Phase 0 evidence.
   A structural saving that does not move its stated target is not accepted as
   T23 progress, though it may be kept on its own merits (see Phase 5).
3. Preserve public APIs and existing multi-child behavior. Prefer existing local
   and first-party storage facilities; do not introduce a general container,
   allocator framework, dependency, or compatibility API for this optimization.
4. Keep admission failure before external work begins. Do not move required error
   storage allocation into failure/completion handling, where allocation failure
   could prevent guaranteed cleanup or error reporting.
5. Keep cancellation authority, stop origins, result/error ordering, and exactly-once
   publication intact. Retain borrowed resources until callbacks and backends retire.
6. Preserve allocation-resource provenance and documented resource lifetime.
   No global pools, hidden caches, or thread-local ownership shortcuts.
7. Preserve guaranteed deferred delivery under saturation. Never run a continuation
   on an unauthorized thread or while an internal lock is held to save a queue hop.
   Backend threads must not acquire general permission to execute application
   continuations.
8. Keep enqueue/YieldNow semantics and depth/work budgets. No optimization authorizes
   weaker structured concurrency or implicit detach.
9. Report failed experiments and unresolved gates. Improvements against the previous
   phase do not reset the original latency, cold-start, idle, or shutdown limits.

## Phase 0 — Attribute the regression to events

Scope: measurement only. No production optimization is included in this checkpoint.

- [x] Verify the saved T23 source/harness identities and reuse its completed matrix;
  repeat timing only when configuration or source changes require it.
- [x] For each failing workload (suspended receive at 8 and 1,024 sockets, file stat
  in both modes, native read, fallback read/write, ready UDP, spawn/yield/join) and
  for original, checkpoint, and redesign, count per measured operation: executor
  submissions, deliveries and reservations, cross-thread wake signals (futex/eventfd
  writes), poller returns, and voluntary/involuntary context switches. Reuse the
  existing context-switch measurement and allocation instrumentation; add counters
  only where none exist, in instrumented builds separate from timing runs.
- [x] For each workload, trace the thread sequence of one operation
  (caller -> runtime -> backend/worker -> runtime -> selected executor) and label
  each transfer as required by backend retirement, runtime bookkeeping, executor
  placement, fairness, or incidental.
- [x] Bisect `203f704..b44612e` on the file-stat and fallback read/write fixtures to
  find the change(s) responsible for the pre-redesign file regression. Use the
  existing harness reconstruction patches; record each probe's source identity.
- [ ] Profile on pinned CPUs (`taskset`) and, where the host exposes them, record
  instruction, cycle, and context-switch counters alongside wall time. Use these
  for attribution only; acceptance timing keeps the T23 protocol below. Not done:
  this host has no `perf`; syscall counts and timestamp probes were used instead.
- [ ] Record a lifetime table for root/scope storage (creator, owners, final borrower,
  destruction point, allocation resource, whether results can escape). This feeds
  Phase 5 only and is not on the critical path.

Exit: a table that links each failing workload's microsecond regression to its extra
transfers/wakeups/switches (or shows that it does not), the bisect result for the
file paths, and a stated target for Phases 1–4. If the event hypothesis is rejected,
revise the remaining phases from the evidence before implementing anything.

## Phase 1 — Suspended receive owner/delivery path

Risk: higher. Target: the redesign's +15–18 us suspended-receive mean and the
8-socket p95/p99 failures. Begin only after Phase 0 identifies the extra events.

- [ ] Trace terminal observation -> producer retirement -> consumer/join publication
  for a pending receive, including early completion and executor discard/rejection.
- [ ] Remove a separate owner delivery or executor transfer only where retirement is
  already complete. Retain two-stage handling whenever producer leases/callbacks are
  still outstanding; terminal result publication alone is not permission to join.
- [ ] Reuse authorized dispatch and its depth/work accounting, with an already reserved
  deferred fallback. Prove no access to owner/child/frame follows possible destruction.
  Do not remove a reservation solely because the inline path usually succeeds.
- [ ] Preserve socket duplex behavior and shutdown progress under saturation.
- [ ] Test synchronous completion, suspended completion, held frame leases, completion
  before submission returns, callback-triggered destruction, sibling failure, saturated
  delivery, shutdown, and nested/reentrant dispatch on runtime and external executors.
  Add controlled-interleaving coverage for any changed handoff.

Exit: fewer measured events per receive, the stated latency movement, and unchanged
retirement and error visibility. If no transfer can be removed safely, record that
finding and retain the current implementation.

Result (2026-10-04): target met by a different mechanism than the items above
assumed. No delivery or transfer was removed; the extra cost was a lock convoy on the
worker's completion queue. See the checkpoint report.

## Phase 2 — File paths

Risk: depends on the bisect result. Target: file-stat, native-read, fallback
read/write, and native/fallback cold-open gates.

- [ ] Act on the Phase 0 bisect. If the cause is a pre-redesign change, evaluate
  reverting or reworking it against the current contracts; it is not exempt from
  rules 3–9 because it predates the redesign.
- [ ] Separately address the redesign's own file-stat increment (+6.7 / +11.4 us)
  if Phase 0 attributes it to an incidental transfer.
- [ ] Preserve file ordering and flush/close barriers. Keep native and fallback
  behavior separate in evidence.
- [ ] Rerun affected platform-independent and Linux-native tests. Record Windows/macOS
  paths as unverified until native checks are supplied; do not infer success from Linux.

Exit: the stated file metrics move as predicted with unchanged ordering, lifetime,
and placement guarantees, or a documented decision that the cost is required.

Result (2026-10-04): native read and write pass. Fallback read/write and file stat
still fail; their remaining cost is runtime-routed delivery, which the current
cancellation contract requires (see the checkpoint report). Cold-open was not
investigated.

## Phase 3 — Spawn/yield/join and ready UDP tails

Risk: moderate. Target: spawn/yield/join p95/p99 (+42%) and ready UDP mean/p95.

- [ ] Use Phase 0 traces to determine whether the tails come from additional wakeups,
  worker parking/unparking, or queue contention rather than mean-path work.
- [ ] Change only an evidenced cause; keep YieldNow always queued and budgets intact.

Exit: tail metrics move as predicted, or a documented finding that they do not
share a removable cause with Phases 1–2.

Result (2026-10-04): spawn/yield/join and ready-UDP percentiles pass after the
Phase 1 checkpoint changes; no separate change was needed.

## Phase 4 — Remaining incidental transfers

Risk: higher. Begin only if gates still fail after Phases 1–3 and Phase 0 identified
further incidental transfers. Optimize one transfer per checkpoint, with the
controlled-interleaving and native-platform rules of Phases 1–2. If no safe transfer
remains, record that finding.

## Phase 5 — Root storage and coallocation (optional, decoupled from T23 gates)

Root allocation reduction is justified by memory footprint, allocation pressure,
and throughput, not by the end-to-end latency gates. It may proceed independently
of Phases 1–4 and does not block them.

- [ ] Capacity-one child bookkeeping and bounded error storage: isolate the child-slot
  and error-slot changes, and measure object size, retained memory, and matched
  latency for each before combining them. Keep report storage independently
  retainable; an inline error slot belongs to the report's lifetime.
- [ ] Coallocate one proven pair at a time from the lifetime table, only where
  destruction and allocation-resource rules remain valid. A shared allocation must
  not introduce a reference cycle or keep completed frames, factories, buffers,
  cancellation callbacks, or execution permits alive. Release a child's resources at
  the original retirement point even when a result handle keeps the allocation alive.
- [ ] Do not replace shared ownership with raw pointers merely because a common case
  joins immediately. Preserve cross-thread result/observer behavior and single
  consumption.
- [ ] Test capacities 1 and greater than 1, admission exhaustion, factory/move failures,
  success/error/stopped results, dropped and retained handles, OperationRef observers,
  ScopeReport objects retained after owner destruction, throwing payload moves,
  cancellation/unregister races, PMR failure, supervisor reuse, and allocator
  lifetime through final release.

Exit: fewer allocations with justified object-size and retained-memory cost, no new
completion-path allocation, and no unexplained regression in the T23 timing set.

### Reverted experiment — 2026-09-29

The first capacity-one implementation used variants to select inline child/error
slots or the existing multi-child PMR vectors. Error storage remained independently
owned by the report. It was tested and measured, then **reverted** because the
allocation savings did not establish a short-task performance benefit. The current
source does not contain this optimization.

| Finding | Result versus the saved pre-experiment implementation |
| --- | --- |
| Capacity-one scope construction | Allocations 7 -> 5; requested bytes 872 -> 840 |
| Runtime child, depth 1 | Allocations 13 -> 11; requested bytes 3016 -> 2984 |
| Runtime child, depths 8 / 64 | Allocations 20 -> 18 / 76 -> 74; 32 fewer requested bytes per operation |
| Capacity-one construction mean | 13.7% faster in the primary series; 20.4% faster in the diagnostic repeat |
| Short runtime-child mean, depth 1, cancellation=0 | 4.3% slower in the primary series; 7.8% slower in the diagnostic repeat |
| Multi-child scope storage | 40 additional requested bytes per scope; allocation count unchanged |
| Multi-child construction mean, capacities 2 / 256 | 7.0% / 11.4% slower initially; 0.9% faster / 3.3% slower in the repeat |
| Internal NoError report storage | Object size increased from 40 to 80 bytes |

The primary timing series used five serial Release processes per version, with
matched before/after harnesses and original I/O controls. One bounded diagnostic
repeat used three processes per version for async, general I/O, and file timing.
Percentages above compare medians of process means; they are not confidence
intervals. Measurements used GCC 15.2.0 on an i7-13700KF under Linux WSL2, without
CPU affinity. Allocation measurements ran separately from timing.

All 104 focused Debug tests and 104 ASan/UBSan tests passed. Ready paths remained
allocation-free, root reservation/delivery counts were unchanged, and tracked
overload memory returned to baseline. Those results cover the reverted experiment,
not a future replacement. Full T22 and Windows/macOS native verification were not
repeated. The original T23 I/O performance gates still failed.

The short-runtime-child slowdown is roughly 100–150 ns on a ~2 us operation, which
an unpinned WSL2 wall-clock series cannot reliably separate from layout effects.
Any retry must use the pinned, counter-based attribution from Phase 0 before
drawing conclusions. Larger storage and added branching remain candidate
explanations, not established causes.

Local raw results remain under `build/async-runtime-evidence/phase1/` in this working
environment; that generated directory is not a durable repository artifact.

## Checkpoint report — 2026-10-04

Scope: Phase 0 attribution, the Phase 1/3 wake and handoff changes, and the native
part of Phase 2. All measurements are from one x86_64 WSL2 host (i7-13700KF, GCC
15.2.0, static Release). On this host a futex wake that actually wakes a sleeping
thread costs the waker about 7.5 us, so wake and handoff effects are larger here than
on typical native Linux. Results should be re-measured on native hosts.

### Findings

- **Syscall count was not the cause.** In the network fixture the redesign made
  151k futex calls, against 77k at the checkpoint and 71k originally; most of the
  extra calls woke no thread. Removing them cut the count to 48k, but
  external-executor latency moved by at most 3%. Runtime-executor roots, which never
  cross threads, became 15–20% faster.
- **Suspended receive: a lock convoy.** Timestamp probes showed a bimodal latency
  (p50 about 57 us against the checkpoint's 36 us). SyncWait's root start (main
  thread) and the receive completion (runtime thread) are both published into the
  worker's completion queue, which signaled the worker while holding its mutex. The
  second producer blocked behind that wake for about 12 us; meanwhile the worker ran
  the root, found nothing else and slept, and then needed a second wake.
- **Bisect.** `203f704..b44612e` is a single commit, `7792846` ("Unify file and
  socket execution under a shared I/O runtime"). Its native-read cost is the
  `IOSQE_ASYNC` flag, which forces every request through a kernel io-wq thread (A/B:
  about 65 to 54–58 us). Its fallback and stat cost is that results travel worker ->
  runtime thread -> selected executor; the original went worker -> executor.
- **Multi-worker pools behave differently.** In the original queue, an external
  publisher's broadcast wake leaves every worker awake and blocked on the mutex.
  The worker that takes the item then runs the whole continuation chain itself,
  and its intermediate wakes find no sleeper.

The raw syscall traces and probe timelines were not retained (the scratch area was
cleared); the numbers above are recorded observations. The timing, test, and
sanitizer evidence below is retained.

### Changes

1. `AtomicCondition` counts blocked waiters; `NotifyOne`/`NotifyAll` skip the platform
   wake when none is blocked.
2. Removed `TaskExecutionState::m_finishedCondition`, which was notified on every task
   completion but never waited on.
3. `SyncWait` wakes its caller with a one-shot futex signal instead of a mutex and
   condition variable that was notified while locked. The notifier's final access
   is one atomic exchange, so the caller may return immediately.
4. The runtime loop does not wake itself for queue work or registration changes made
   by its own dispatch; the same batch rescans them. Off-loop changes still signal
   hosts, as documented in [IORuntime.md](../IORuntime.md#host-loop-integration).
5. A `CompletionQueue` with a single driver (a one-worker `ThreadPoolScheduler`, each
   runtime loop queue) signals after releasing its lock. An atomic in-flight count
   makes the destructor wait for that signal. Multi-worker pools keep signaling
   under the lock.
6. io_uring reads and writes submitted from application executor threads are no longer
   forced into io-wq; io_uring attempts them nonblocking and punts anything that
   would block. Submissions from the runtime thread, flush, and close keep
   `IOSQE_ASYNC`.

Tests were added for: skipped wakes and a lossless ping-pong; SyncWait retiring before
and after its caller blocks; out-of-lock single-driver signaling and destruction
waiting for it; and the loop dispatching work and readiness registered by its own
callbacks.

### Results

Medians of five matched processes per version, mean latency in microseconds.
"Redesign" is the unmodified `HEAD`; "current" adds the changes above.

| Workload | Original | Checkpoint | Redesign | Current | Gate (vs original) |
| --- | ---: | ---: | ---: | ---: | --- |
| Suspended receive, 8 sockets | 39.5 | 40.8 | 56.7 | 44.5 | pass (+12.7%) |
| Suspended receive, 1,024 sockets | 39.7 | 40.5 | 53.8 | 42.8 | pass (+8.0%) |
| Native read, 64 KiB | 55.5 | 68.1 | 68.7 | 57.3 | pass (+3.3%) |
| Native write, 64 KiB | 79.4 | 68.3 | 69.3 | 68.1 | pass |
| Spawn/yield/join | 25.5 | 25.5 | 28.6 | 27.1 | pass (+6.0%) |
| Ready UDP | 29.0 | 31.7 | 33.4 | 32.9 | pass (+13.2%) |
| File stat, Auto | 50.2 | 70.2 | 75.6 | 74.5 | **fail** (+48.3%) |
| File stat, fallback | 52.1 | 70.4 | 73.3 | 73.6 | **fail** (+41.3%) |
| Fallback read, 64 KiB | 52.3 | 74.8 | 75.5 | 75.1 | **fail** (+43.7%) |
| Fallback write, 64 KiB | 54.0 | 75.9 | 76.0 | 76.7 | **fail** (+42.1%) |

The passing rows pass all four percentile/mean limits. Cold-setup gates still fail:
native and fallback file cold-open (+52.5% / +79.1%) and the 8-socket network setup
(+53.6%, which the unmodified redesign also fails). Idle CPU remains 98.8–99.8% below
the original, with no periodic wakeups. Single-receive cancellation (161 us) and idle
shutdown (56 us) stay under 1 ms.

Other effects compared with the unmodified redesign:

- Runtime-executor child/yield roots are 12–18% faster at every depth (depth 1:
  2.08 to 1.71 us). External one- and four-worker chains range from 9% faster to
  1% slower; no AsyncRuntime case is more than 8% slower.
- Structural dispatch counts, fairness progress, and allocation counts are
  unchanged. File-fixture peak live bytes rose by 176 bytes.
- Mixed load with native files and one external worker trades throughput: UDP
  68.6k to 50.6k exchanges/s, file transfers 35.2k to 44.4k/s. Inline io_uring
  issue copies cached data on the submitting worker instead of an io-wq thread,
  so that worker has less time for UDP. With fallback files and one external
  worker both improve (UDP 27.4k to 92.5k/s, files 23.4k to 39.9k/s).
  Runtime-continuation mixed load is unchanged.

### Rejected experiments

- **Delivering fallback results directly from the pool worker.** It removed the
  runtime hop, but three tests failed. They require a cancellation or stop that
  arrives after the worker finished, but before delivery, to produce Stopped and to
  close an unclaimed opened descriptor. Reverted.
- **Signaling outside the lock for every queue.** Four-worker chains went from 33
  to 43 us, and 24-worker pools from 30 to 313 us, because a broadcast wake outside
  the lock sends every sleeping worker after one item. Replaced by change 5.
- **Waking one worker per publication.** 47 us for four or more workers: each chain
  step migrated to a newly woken worker.
- **Deferring wakes for publications by a pool's own worker.** Rejected without
  implementation: a worker that publishes and then blocks, such as `SyncWait` from a
  pool worker, would deadlock.
- **Releasing the fallback job's empty runtime reservation instead of dispatching
  it.** It removed one runtime wake per operation but did not move any latency
  target. Reverted under rule 2.

### Verification

- Focused Debug tests: 492/492 Async, Execution, Sync, IO, and Net tests, plus five
  repeated runs of the 156 most timing-sensitive tests.
- ASan/UBSan (UBSan errors fatal) and TSan: the same 492 tests, with no sanitizer
  report.
- Not run: full T22 verification, installed-consumer checks, the shared-library
  matrix, and Windows/macOS. The Windows IOCP path is affected by change 4 and is
  unverified.

Evidence:
[raw process output](../measurements/async-runtime-wake-handoff-linux.txt),
[gate calculations](../measurements/async-runtime-wake-handoff-gates.json),
[source, binary, and command identities](../measurements/async-runtime-wake-handoff-source.json),
[tests](../measurements/async-runtime-wake-handoff-tests.txt), and
[sanitizers](../measurements/async-runtime-wake-handoff-sanitizers.txt). The runner and
analyzer scripts are under the generated `build/async-runtime-evidence/wake-handoff/`.

### Open decisions

- **Fallback files and stat.** Either change the delivery contract so pool workers
  deliver directly (cancellation after completion would then no longer produce
  Stopped), or accept the cost as documented.
- **Cold-open.** Not yet attributed.
- **Brief worker spinning before sleeping.** Likely to cut latency on hosts with
  expensive wakes, at a CPU cost. This is a policy decision and has not been tried.

## Benchmark and correctness checkpoint after each implementation phase

Use repository-defined commands and existing build trees. Batch edits, then build
the affected targets once before verification. Never overlap builds, profiling,
sanitizer checks, or another benchmark with acceptance timing.

**Correctness before timing:** focused SyncWait, TaskScope, scoped operation,
scope-allocation/admission, supervisor, observation, and cancellation tests under
Debug and ASan/UBSan. Add deadline/race/dispatch/backend tests when their paths change.
Exercise nontrivial payload failures, allocation failure, saturation, and retained
handles. For handoff changes include controlled races and bounded lifetime stress.
Use existing UI ownership/scheduler checks when owner publication or delivery changes.

**Event counts:** rerun the Phase 0 per-operation event counts for the affected
workloads and report them against the phase's stated target.

**Timing after every phase:** run five serial Release processes each for
AsyncRuntimeBenchmarks, IORuntimeBenchmarks, IORuntimeFileBenchmarks,
IORuntimeNetworkBenchmarks, and IORuntimeMixedBenchmarks. Preserve T23's payloads,
worker counts, 100 warmups, measured samples, idle intervals, and execution modes.
Mixed load retains its documented 1,000-timer fixture. Collect p50/p95/p99/mean,
cold setup, cancellation, shutdown, idle CPU/wakeups, and fairness progress.

Run the immediately preceding accepted implementation with identical harnesses in
the same measurement session; rotate before/after order. If instrumentation cases
are added, adapt the previous-version harness without altering production semantics.
Also run the original control's three supported I/O timing fixtures five times at
each acceptance checkpoint. Keep the pre-redesign comparison from T23, refreshing
it if a harness change makes the previous result incomparable.

**Structural and memory:** separately run AsyncRuntimeAllocationBenchmarks,
IORuntimeAllocationBenchmarks, and IORuntimeFileAllocationBenchmarks. Record ready
allocation, depths 1/8/64, root fixed costs, queue/delivery/reservation counts,
cannot-stop versus stoppable execution, peak retained memory, escaped-handle memory,
overload rejection, and complete teardown. Instrumented timings are never acceptance
timings; extend the existing instrumentation only where the phase needs missing data.

**Per-phase report:** record the implementation commit or working diff and source hashes,
compiler/options/hardware, exact commands, raw results, five-process medians/ranges,
the stated target versus the observed change, previous-phase deltas, original-baseline
gates, failures/skips, and native-platform limitations. Preserve unfavorable runs.
Confirm sources remained unchanged during runs.

Accept a phase only when its stated target moved, correctness checks pass, and
measurements show no unexplained material regression elsewhere. When timing is noisy
or contradictory, perform a bounded matched diagnostic repeat and report uncertainty;
do not repeat selectively until a desired result appears. Reject or isolate
unsuccessful experiments without discarding unrelated user work.

## Final acceptance and tracking

- [x] Phase 0 event attribution, file-path bisect, and stated targets.
- [x] Phase 1 suspended-receive checkpoint, or documented decision to retain it.
- [ ] Phase 2 file-path checkpoint(s), or documented decision to retain them. Native
  transfers done; fallback/stat awaits the delivery-contract decision.
- [x] Phase 3 spawn/yield/join and ready-UDP tail checkpoint, or documented finding.
- [ ] Phase 4 remaining transfer checkpoint(s), if required.
- [ ] Phase 5 root storage checkpoint(s), optional and independent of the gates.
- [ ] Final T22 verification against the changed implementation: required Linux suites,
  sanitizers, public headers, installed consumers, portable loop, and Hello.IO modes.
- [ ] Final T23 review against the original gates: p50/p95/p99 at most +25%, mean
  at most +20%, cold setup at most +50%, idle CPU reduced at least 80%, no periodic
  network wakeups, and median single-operation cancellation/idle shutdown below 1 ms.

T22's current evidence covers the saved implementation, not future changes.
Do not mark T23 complete until all required end-to-end and structural gates pass.
Phase improvements may be retained while T23 remains open. T24 documentation/consumer
closure and T25/T26 native verification retain their existing responsibilities;
T27 remains the final redesign audit.
