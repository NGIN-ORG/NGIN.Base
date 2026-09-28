# Stage 1 implementation evidence

## Direct-transfer checkpoint — 2026-09-13

T05 is **in progress**, not complete. Fresh sequential children on the same executor now borrow the running
root's reserved continuation slot. Successful child entry and return use coroutine-handle transfer in an
authorized executor context; unknown/custom contexts retain queued delivery. The provisional budget is 256
transfers per root delivery turn, allowing depth-64 entry/return within the budget. Budget exhaustion reuses
the admitted slot. The cooperative scheduler gives ordinary work/due timers turns between reserved deliveries.
Full PollOnce accounting, executor transitions, and general inline dispatch remain T06–T09 work.

Failure forwarding retires a consumed child before completing its parent. Retirement uses the root slot and
waits for the child's outstanding frame references, so deep failures do not recursively destroy a retained
frame tree and borrowed parent state stays alive through that retirement. A depth-10,000 failure initially
exposed recursive cleanup in the new regression fixture; that failure is retained locally in
`build/async-runtime-evidence/direct-deep-tests.txt`. The corrected implementation passes that test under
ASan/UBSan. The held-frame test distinguishes automatic locals destroyed by normal coroutine return from
locals retained when failure forwarding bypasses the parent's body.

Task value awaiters no longer declare unconditional noexcept when returning a potentially throwing T move.
A fixture whose move throws only after child final suspension verifies that this becomes an observable parent
fault rather than terminating the process. The discard-submission regression now explicitly spawns concurrent
children: sequential children no longer have an ordinary startup queue item to discard.

The verification run also exposed an existing thread-pool shutdown race. A worker released after admission
closed could dequeue an ordinary task before the destructor's clearing pass reached it. Worker dequeue now
rechecks stop and discards such ordinary work; reserved completion delivery still drains. The occupied-worker
shutdown fixture passed 100 separate ASan/UBSan process runs after the fix. Earlier failing evidence remains
in `build/async-runtime-evidence/direct-final-asan-tests-2.txt`; no assertion was relaxed.

### Source and verification

[Source identities](../measurements/async-runtime-direct-source.json) identify the modified headers and
fixtures on top of `b44612eb896ab013463876e22a40e221a4fd8c5f`. Exact checkpoint inputs are retained locally in
`build/async-runtime-evidence/direct-checkpoint/inputs.tar.gz`; their archive hash is in the manifest.

- [Debug CTest](../measurements/async-runtime-direct-tests.txt): 338 selected, 332 passed, six skipped, zero failures.
  Built all repository-generated targets for sources directly under Async/Execution/IO/Net, then ran
  `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'`.
  Skips remain native Windows IOCP and five unavailable TLS integration cases.
- [ASan/UBSan CTest](../measurements/async-runtime-direct-sanitizers.txt): 29 passed in RelWithDebInfo, selecting
  `^Base\.Async\.(Sequential|ChildLifetime|TaskContext|TaskScope)\.` after building those four test targets.
  This includes success/failure depth 10,000, one-slot admission, fairness, throwing result move, held child
  frame retirement, callback submission/discard, and scope cleanup cases.
- Shutdown stress: ran `build/tests-asan/tests/RelWithDebInfo/Async_TaskContextTests
  "Thread-pool shutdown retires an initial task while its worker is occupied"` 100 times serially, all passed.
  Raw output: `build/async-runtime-evidence/direct-shutdown-stress.txt`.

### Cost checkpoint

[Uninstrumented timing](../measurements/async-runtime-direct-timing.txt) and
[allocation/structural counts](../measurements/async-runtime-direct-allocations.txt) use the Stage 0 harness,
GCC 15.2, Release, 100 warmups and 1,000 samples per case. This checkpoint is one process per binary, not
final repeated original/checkpoint/redesign performance acceptance. The allocation binary's timings are
not timing evidence.

| Depth | Ordinary submissions | Completion reservations | Executor invocations |
| --- | ---: | ---: | ---: |
| 1 | 1 | 1 | 1 |
| 8 | 1 | 1 | 1 |
| 64 | 1 | 1 | 1 |

Depth-64 allocations fell from 128 to 65 on the external pool and 192 to 66 on the runtime. The remaining
allocations include 64 compiler frames and root delivery/submission storage. Measured promise size is 400
bytes; this intermediate promise still contains machinery that must move to concurrent ownership state.
One-process uninstrumented mean latency was 36.516 microseconds for the one-worker depth-64 chain and
7.286 microseconds for the runtime chain. These numbers do not close any file/network latency gate.

### Still open

T05 still requires compact sequential versus concurrent state, inline ready completion storage, complete
single-consumer enforcement, and validation of all suspension/retirement paths. Error propagation currently
uses deferred retirement; it does not yet have the same direct-path cost as successful helpers. This checkpoint
does not establish the final three-channel, environment, scope, race, deadline, supervisor, I/O, or shutdown
contracts. Stage 1 stays open until its full exit checks are met.

## Consuming-await checkpoint — 2026-09-13

Both value and void lvalue awaits now move the source Task into the owning awaiter. Cancellation-aware
awaiters likewise own their child. This removes the borrowed Task awaiter and prevents a second await from
moving the same result again or competing for a pending child's continuation. An empty/consumed task reports
InvalidTaskUsage even when that second attempt uses a canceled wait context. Access to a Task object from
multiple threads still requires caller synchronization; this change does not make the source object concurrent.

The new fixtures exercise value/void tasks, ordinary/cancellation-aware waits, cancellation before a second
attempt, and a second parent consuming the source while the first child is suspended on a timer. The first
parent completes normally; only the second parent fails. Existing lifetime and cancellation fixtures remain
unchanged and pass.

[Input hashes](../measurements/async-runtime-consumption-source.json) and the local archive
`build/async-runtime-evidence/consumption-checkpoint/inputs.tar.gz` preserve this checkpoint separately from
the earlier direct-transfer timing inputs. No new timing claim is made for this change.

- [Debug results](../measurements/async-runtime-consumption-tests.txt): 340 selected, 334 passed, six skipped,
  zero failures. Rebuilt all Async/Execution/IO/Net test targets and used the same label selection as above.
- [ASan/UBSan results](../measurements/async-runtime-consumption-sanitizers.txt): 31 passed, zero failures,
  after rebuilding Sequential, ChildLifetime, TaskContext, and TaskScope in RelWithDebInfo and using the same
  focused selection as above.
- Formatting and `git diff --check` pass. The prior direct-transfer archive's hash and each recorded input
  hash were also verified against that preserved archive.

T05 remains open for inline ready results, compact state at concurrent boundaries, and remaining retirement
coverage. The consuming-await change addresses the coroutine-backed Task path only; ready tasks must preserve
these consumption rules when introduced.

## Inline-ready checkpoint — 2026-09-13

Task now stores an inline Completion in addition to its coroutine representation. FromValue and
FromCompletion construct ready computations without a compiler frame. Direct awaits consume ready results;
Spawn converts them to an already-complete inline Operation without executor admission. Empty executors can
therefore run ready computations through SyncWait. The internal scope observer and retirement protocol also
accepts inline operations. Cancellation of a wait does not replace an already-published inline result.

Task/Operation moves now have payload-dependent exception specifications. Owning awaiter construction may
throw, allowing coroutine exception translation; Spawn catches throwing ready-result transfers and returns
an exception fault. Result/error payloads need not be copyable or move-assignable. The fixtures exercise
move-only values/errors, ownership replacement, repeated consumption, all current terminal categories,
value/void composition, canceled waits, throwing moves, and ready scope children/factory retirement.

[Input identities](../measurements/async-runtime-ready-source.json) and the local archive
`build/async-runtime-evidence/ready-checkpoint/inputs.tar.gz` preserve this implementation separately.

- [Debug](../measurements/async-runtime-ready-tests.txt): rebuilt every Async/Execution/IO/Net target, then ran
  `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'`.
  347 selected, 341 passed, six existing platform/TLS skips, zero failures.
- [ASan/UBSan](../measurements/async-runtime-ready-sanitizers.txt): built Ready, Sequential, ChildLifetime,
  TaskContext, and TaskScope in RelWithDebInfo; selected
  `^Base\.Async\.(Ready|Sequential|ChildLifetime|TaskContext|TaskScope)\.`. 38 passed, zero failures.
- [Allocation counts](../measurements/async-runtime-ready-allocations.txt): built and ran Release
  AsyncRuntimeAllocationBenchmarks, 100 warmups and 1,000 samples. Ready value, void, fault, domain-error,
  and stopped runs each measured zero allocations and zero bytes per sample, including SyncWait/Spawn/result
  consumption. Payloads in these counted cases are scalar; separate tests cover allocating move-only payloads.
  Instrumented timing is not performance acceptance evidence.
- Formatting and `git diff --check` pass. The initial Ready fixture compile failure used a nonexistent default
  TaskContext constructor; it was corrected to the supported empty ExecutorRef constructor. The failed build
  log remains at `build/async-runtime-evidence/ready-build.txt`.

The intermediate Task<int> grew from 64 to 144 bytes; promise size remains 400 bytes. This makes the pending
compact-state split consequential: inline storage currently sits alongside the old coroutine owner fields,
and sequential promises still contain concurrent machinery. T05 remains open for that split and complete
retirement validation. Ready-loop fairness/accounting remains part of T08; the current ready await fast path
does not close that gate. No end-to-end performance or platform acceptance gate is closed here.

## Compact-state and success-retirement checkpoint — 2026-09-13

Running roots, generator producers, and executor boundaries now allocate one ExecutionState containing
blocking-wait signaling, reusable/foreign completion tickets, pending work, observer storage, chain work
accounting, submission arbitration, and the concurrent continuation-install state. Ordinary sequential frames
borrow that state and install their continuation before starting, without an atomic installation handshake.
Frame reference and terminal arbitration remain local because suspended backends may retain and complete an
individual frame. Task no longer duplicates its promise's executor and start status.

The successful-child lifetime audit found that an extra frame lease could survive direct final transfer while
the parent destroyed locals borrowed by a coroutine parameter. The new regression first failed with premature
parent completion; [that failure is preserved](../measurements/async-runtime-success-lease-regression.txt).
The corrected slow path moves the successful completion into the owning awaiter's inline storage, retires the
child through the root's existing slot, and resumes the retained parent only afterward. A throwing result move
on that path publishes a parent exception fault and still retires the child. The final regression covers both
void and value children and checks that external lease release cannot run the parent outside its executor.

[Input identities](../measurements/async-runtime-split-source.json) and the local archive
`build/async-runtime-evidence/split-checkpoint/inputs.tar.gz` preserve the exact checkpoint.

- [Debug results](../measurements/async-runtime-split-tests.txt): rebuilt all Async/Execution/IO/Net targets;
  `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'` selected 348 tests:
  342 passed, six existing platform/TLS skips, zero failures.
- [ASan/UBSan results](../measurements/async-runtime-split-sanitizers.txt): rebuilt Ready, Sequential,
  ChildLifetime, TaskContext, TaskScope, AsyncGenerator, and ExecutorAffinity in RelWithDebInfo; selected
  `^Base\.Async\.(Ready|Sequential|ChildLifetime|TaskContext|TaskScope|AsyncGenerator|ExecutorAffinity)\.`.
  56 passed, zero failures. This includes depth-10,000 success/failure and the successful/failing lease cases.
- [Release allocation/structural counts](../measurements/async-runtime-split-allocations.txt): rebuilt and ran
  AsyncRuntimeAllocationBenchmarks with 100 warmups/1,000 samples. The five inline-ready cases remain at zero
  allocations/bytes. Depths 1/8/64 still use one submission, one reservation, and one delivery each.
- Promise size fell from 400 to 240 bytes; Task<int> fell from 144 to 88 bytes. ExecutionState is 192 bytes
  allocated once per root/boundary, not once per helper. External depth-64 allocates 66 times/34,096 bytes;
  the runtime depth-64 case allocates 67 times/34,208 bytes. The extra root allocation is explicit; no timing
  claim is made from the allocation-instrumented binary.
- Formatting and `git diff --check` pass. Earlier split build/test logs remain under
  `build/async-runtime-evidence/`; subsequent source changes are verified by the final split-lease logs above.

T05 remains open for extracting the new non-template execution-state implementation into the compiled
Execution component and the final task-level contract review. Environment inheritance, explicit transitions,
ready/pending fairness, and the remaining stages still retain their original gates.

## T05 completion — 2026-09-13

TaskExecutionState allocation and reservation now live in `src/NGIN/Async/TaskExecutionState.cpp`, compiled
into the existing Execution component. Its internal header has an exported interface and no dependency on
the per-consumer async exception-capture configuration. The installed Execution consumer now executes both
a coroutine Task and an inline-ready Task, ensuring the compiled symbols and transitive headers are usable.

The continuation publication race fixture now creates concurrent execution state before racing installation
and completion. That preserves its lost-wakeup assertions while testing the new ownership boundary; racing
an unstarted sequential promise's plain installation field would be invalid. A separate regression proves
that moving, replacing, and destroying cold Task objects never enters their bodies.

[Source identities](../measurements/async-runtime-compiled-state-source.json) and
`build/async-runtime-evidence/compiled-state-checkpoint/inputs.tar.gz` preserve this completed T05 checkpoint.

- [Debug](../measurements/async-runtime-compiled-state-tests.txt): 52 passed after building Task, Sequential,
  Ready, AsyncGenerator, and TaskScope. Command:
  `ctest --test-dir build/tests -C Debug --output-on-failure -R '^Base\.Async\.(Task|Sequential|Ready|AsyncGenerator|TaskScope)\.'`.
- [ASan/UBSan](../measurements/async-runtime-compiled-state-sanitizers.txt): 72 passed after building Task,
  Ready, Sequential, ChildLifetime, TaskContext, TaskScope, AsyncGenerator, and ExecutorAffinity in
  RelWithDebInfo. The CTest regex selected those eight groups under `^Base\.Async\.`.
- [Static installed consumer](../measurements/async-runtime-compiled-state-static-consumer.txt): passed via
  `CXX=/opt/gcc-15.2/bin/c++ CMAKE_BUILD_PARALLEL_LEVEL=4 ctest --test-dir build/tests -C Debug --output-on-failure -R '^Base\.Package\.Execution\.InstalledConsumer$'`.
- [Shared installed consumer](../measurements/async-runtime-compiled-state-shared-consumer.txt): passed in
  Release. Configured `build/async-runtime-shared` with Ninja Multi-Config, GCC 15.2, components=Execution,
  static=OFF, shared=ON, tests/examples/benchmarks=OFF, OpenSSL integrations=OFF, and an install prefix under
  that build tree. Built `--config Release --target install -j4`; configured the existing tests/PackageConsumer
  against that prefix with component=Execution and expect-aggregate=FALSE; built and ran its Release CTest.
  Exact configure/build logs are retained under `build/async-runtime-evidence/compiled-state-shared-*`.
- [Allocation/structural counts](../measurements/async-runtime-compiled-state-allocations.txt): the compiled
  path preserves zero allocations/bytes for all five ready cases and one submission/reservation/delivery for
  eligible chains of depths 1/8/64. Sizes remain promise=240, execution state=192, Task<int>=88 bytes.
- Final diff/format checks pass. Native Windows/macOS and end-to-end performance acceptance remain open.

T05's local acceptance is met: inert cold construction/destruction, single-consuming task/result paths,
direct eligible child composition, explicit allocation-free ready results, state retained at running
boundaries, and bounded deep success/failure with child-before-parent retirement. This does not complete
R08's later structured-owner API migration or the full Stage 1 exit: T06 environment inheritance, T07
transitions, T08 general fairness, and T09 validation remain pending. Earlier evidence is historical for its
identified inputs, not substituted for the current checks above.

## T06 environment inheritance — 2026-09-13

AsyncEnvironment now carries executor, cancellation token, optional monotonic deadline, PMR resource,
borrowed metadata, and optional priority. TaskContext validates/snapshots it and exposes GetEnvironment.
Spawn snapshots the starter before submission; child startup copies the running parent's logical properties.
CurrentEnvironment returns a value snapshot without suspension or scheduling. A producer without its own
explicit context inherits the active environment on its first advance. Existing explicitly selected delivery
executors remain until the T07 transition migration; no thread-local or ambient UI context is captured.

Execution-state allocation and pending TaskContext wait state use the inherited PMR resource. The compiled
state deleter returns storage to the same resource after retirement. Null resources are rejected by context
construction, and root state allocation failure produces a scheduling fault before body execution. Cold
compiler frames retain their normal construction allocator: no portable frame-elision or retroactive resource
selection is claimed. Executor/backend and structured-owner allocation integration remains with later tasks.

The environment fixture checks all properties through depth 64 and a pending yield, preserves the original
startup snapshot after context replacement, and checks cancellation replacement, invalid resources, and
allocation rejection. Its custom resource sees two allocations regardless of helper depth: root state and
pending yield state. It sees no allocation at cold construction and both allocations retire. Owners of borrowed
metadata/resources and cancellation links must outlive their use; copying a token is not ownership transfer
of registrations held by a separate linked source. Deadline/priority fields carry values here; deadline timers
and priority policy are not implemented by merely copying those values.

[Source identities](../measurements/async-runtime-environment-source.json) and the local archive
`build/async-runtime-evidence/environment-checkpoint/inputs.tar.gz` preserve this checkpoint.

- [Debug](../measurements/async-runtime-environment-tests.txt): built every Async/Execution/IO/Net test target;
  `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'`.
  354 selected, 348 passed, six existing Windows/TLS skips, zero failures.
- [ASan/UBSan](../measurements/async-runtime-environment-sanitizers.txt): built Environment, Task, Ready,
  Sequential, ChildLifetime, TaskContext, TaskScope, AsyncGenerator, and ExecutorAffinity in RelWithDebInfo;
  selected those nine groups under `^Base\.Async\.`. 77 passed, zero failures.
- [Static](../measurements/async-runtime-environment-static-consumer.txt) and
  [shared](../measurements/async-runtime-environment-shared-consumer.txt) installed Execution consumers pass.
  The consumer now validates an environment and reads its metadata inside a coroutine. Commands match the
  T05 installed-consumer procedure; shared install and consumer builds reused the configured Release trees.
- [Release allocation counts](../measurements/async-runtime-environment-allocations.txt): empty environment
  copies and all five ready-result runs remain allocation-free. Eligible chains of depths 1/8/64 retain one
  submission/reservation/delivery. Depth-64 external/runtime allocations remain 66/67 in all token modes;
  byte totals are 37,680/37,792. Environment size is 96 bytes; TaskContext 112; promise 296; execution state
  192; Task<int> 88. This instrumented run is structural evidence, not latency acceptance.
- Formatting and final diff checks pass. Public environment validation/allocation/deallocation logic is in
  compiled Execution sources; templated propagation and the environment query awaiter remain in headers.

T06's property-storage/inheritance and allocator-entry requirements are complete. R05 across explicit
transitions, conditional cancellation-boundary policy, scope-owner integration, and deadline enforcement
remain assigned to T07 and the later stages. Stage 1 is not complete until T07–T09 pass.

## T07 core transition checkpoint — 2026-09-13

AsCompletion consumes a Task and returns every terminal outcome to its parent, including across different
parent/child error types. It shares the existing sequential path; it does not spawn another operation. The
lease regression verifies that all outcomes wait for child frame retirement before the observing body resumes.

ExecutorTransitions.hpp provides StartsOn, ContinuesOn, SwitchTo, and an environment-based YieldNow.
SwitchTo acquires target delivery capacity before releasing the old chain guarantee. ContinuesOn reserves
its terminal ticket before starting the child, retains it while the child switches elsewhere, and activates
it for success/domain-error/canceled/fault delivery. Its ticket remains usable after target admission closes.
The callback refers to root-owned state rather than the boundary frame. Compiled TaskExecutionState methods
manage ticket preparation/activation and track the active executor binding.

Ordinary sequential completion carries the child's executor back into the parent environment. Context
arguments no longer override inherited child startup; explicit StartsOn replaces that behavior. The existing
Task affinity fixture now checks this requested contract through StartsOn. Independent Operation observers
retain their own executor. TaskContext yield/delay admission updates the active binding so their explicit
context selection does not silently restore the prior executor after child return.

The transition matrix covers all current terminal categories, incoming versus selected startup, an intermediate
SwitchTo, ready void outcomes, inherited token/deadline/resource/metadata/priority, saturated rejection before
child entry, and admitted completion after admission closes. These are controlled cooperative-executor tests;
they do not replace the still-required multithreaded transition/race checks.

[Source identities](../measurements/async-runtime-transitions-source.json) and the local archive
`build/async-runtime-evidence/transitions-checkpoint/inputs.tar.gz` identify this partial T07 checkpoint.

- [Debug](../measurements/async-runtime-transitions-tests.txt): built every Async/Execution/IO/Net test target
  and ran `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'`.
  360 selected, 354 passed, six existing Windows/TLS skips, zero failures.
- [ASan/UBSan](../measurements/async-runtime-transitions-sanitizers.txt): built ExecutorTransitions,
  CompletionAwait, Environment, Task, Ready, Sequential, ChildLifetime, TaskContext, TaskScope,
  AsyncGenerator, and ExecutorAffinity in RelWithDebInfo; selected those eleven groups under `^Base\.Async\.`.
  83 passed, zero failures. CompletionAwait was rebuilt after adding its held-frame regression before these
  final CTest runs; earlier run logs remain separate.
- [Allocation/structural counts](../measurements/async-runtime-transitions-allocations.txt): rebuilt and ran
  the Release allocation benchmark. Empty environment and five ready-result cases still allocate zero bytes;
  eligible chains of depths 1/8/64 retain one submission/reservation/delivery. ExecutionState is now 240 bytes
  because it records the active executor; promise/Task/environment sizes are unchanged. This harness has not
  yet added transition-specific timing/load cases; no performance gate is closed.
- An initial declaration-edit compile error is retained in `build/async-runtime-evidence/transitions-build.txt`;
  the corrected declarations pass the builds/checks above. Formatting and final diff checks pass.

T07 remains open. The audit located raw file dispatch, native file completion, socket submission, and resolver
awaiters whose selected delivery binding still needs coherent adoption on ready/pending/error paths. This
migration must preserve rejection-before-OS-access and each backend's retirement protocol. Additional
multithreaded transition, throwing terminal move, and installed shared-consumer checks are also pending.
General dispatch/ready-loop/PollOnce fairness and its measurements remain T08–T09 work.

## T07 leaf placement checkpoint — 2026-09-13

Public file/directory operations (including virtual filesystem resolution), socket submissions, and
asynchronous name resolution now explicitly SwitchTo their TaskContext executor before validation and
backend admission. The existing backend completion reservation remains responsible for native/fallback
retirement and delivery. No additional helper coroutine is introduced by the entry transition. The task
chain preserves its other logical environment fields and carries the leaf executor into ordinary parent
completion. A rejected entry transition cannot submit backend work.

New cross-executor cases cover a pending fallback metadata read, canceled file lookup, unbound runtime,
empty file/directory handles, unmounted virtual paths, numeric name resolution, canceled resolution,
unbound sockets, and a pending native UDP receive. The source executor is not pumped during terminal
delivery in the placement cases. The UDP fixture also checks the received payload.

Verification using the existing GCC 15.2 trees:

- Rebuilt all Async, Execution, IO, and Net test targets in `build/tests`, Debug, then ran
  `ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'`:
  **357 passed, six existing skips, zero failures** ([output](../measurements/async-runtime-leaf-placement-tests.txt)).
- Rebuilt `IO_ExecutorPlacementTests`, `Net_ExecutorPlacementTests`, and `Net_SocketOperationTestsTests`
  in `build/tests-asan`, RelWithDebInfo, then ran ctest with
  `-R '^Base\.(IO\.ExecutorPlacement|Net\.(ExecutorPlacement|SocketOperation))\.'`:
  **14 passed** under ASan/UBSan ([output](../measurements/async-runtime-leaf-placement-sanitizers.txt)).
- `git diff --check` passed. [Input hashes](../measurements/async-runtime-leaf-placement-source.json)
  identify the local archive at `build/async-runtime-evidence/leaf-placement-checkpoint/inputs.tar.gz`.

The matching Windows implementation and non-Linux POSIX branches were edited but were not built or
executed natively here. This checkpoint does not close T07: multithreaded transition/error-move tests
and installed-consumer transition validation remain, followed by the T08/T09 scheduling and performance
checks. Private raw awaiters still rely on their enclosing leaf coroutine to establish placement.

## T07 transition validation — 2026-09-13

The worker fixture runs 100 chains for each one- and four-worker target, with 32 executor switches and
explicit yields per chain. It checks logical/physical placement and metadata after each switch and terminal
delivery for success, domain error, cancellation, and fault. Worker bodies return their validation result
through SyncWait so Catch assertions run on the test thread. This is concurrent execution coverage, not
a substitute for a race detector or exhaustive interleaving proof.

A terminal value whose move throws after the child returns verifies that ContinuesOn still reaches its
reserved target and exposes UnhandledException. The installed Execution consumer now compiles, links,
and runs ContinuesOn and YieldNow across cooperative executors through both static and shared packages.

Verification:

- Built `Async_ExecutorTransitionsTests` in `build/tests` (Debug) and `build/tests-asan` (RelWithDebInfo).
  Ctest `-R '^Base\.Async\.ExecutorTransitions\.'` passed all six cases in
  [Debug](../measurements/async-runtime-transition-validation-tests.txt) and
  [ASan/UBSan](../measurements/async-runtime-transition-validation-sanitizers.txt).
- `CXX=/opt/gcc-15.2/bin/c++ CMAKE_BUILD_PARALLEL_LEVEL=4 ctest --test-dir build/tests -C Debug
  --output-on-failure -R '^Base\.Package\.Execution\.InstalledConsumer$'`
  passed the [static package consumer](../measurements/async-runtime-transition-validation-installed-static.txt).
- Rebuilt/installed the existing Release `build/async-runtime-shared` tree, rebuilt its `consumer` tree,
  and ran ctest there: [shared consumer passed](../measurements/async-runtime-transition-validation-installed-shared.txt).
- Final diff whitespace check passed; [source hashes](../measurements/async-runtime-transition-validation-source.json)
  identify `build/async-runtime-evidence/transition-validation-checkpoint/inputs.tar.gz`.

T07 is locally complete. General eligible inline dispatch, enqueue-only semantics, PollOnce accounting,
and final fairness/performance choices remain T08/T09 work. Native Windows/macOS acceptance is still open.
The previous broad Debug result remains the leaf placement checkpoint; this change only reran the focused
transition tests and installed consumers because production implementation did not change.

## Cooperative queue fairness — 2026-09-13

The cooperative scheduler now rotates among three runnable sources: reserved completions, due timers,
and ordinary work. Previously, continuously due timers could starve ordinary work, and ordinary LIFO
selection could indefinitely postpone older work. Ordinary work now uses FIFO order with an index into
its existing reserved vector storage. Consumed prefixes are compacted in batches; draining the queue
resets the index while retaining capacity. Source rotation advances before invoking a callback.

Focused regressions keep all three sources runnable for 30 calls and verify each receives every third
turn. A second regression checks 1,000 FIFO items across partial draining, prefix reuse, and further
submission. No runtime worker or new third-party dependency was added.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug, and ran ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **361 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-cooperative-fairness-tests.txt)).
- Rebuilt `Execution_CooperativeSchedulerTests` in `build/tests-asan`, RelWithDebInfo; ctest
  `-R '^Base\.Execution\.CooperativeScheduler\.'`: **four passed** under ASan/UBSan
  ([output](../measurements/async-runtime-cooperative-fairness-sanitizers.txt)).
- Final diff check passed. [Input hashes](../measurements/async-runtime-cooperative-fairness-source.json)
  identify `build/async-runtime-evidence/cooperative-fairness-checkpoint/inputs.tar.gz`.

This is partial T08/T09 work. The scheduler's queued-source bound does not yet account for nested
coroutine transfers or bound reentrant pump depth. Separate inline dispatch, enqueue-only semantics
(including the current InlineScheduler exception), and PollOnce accounting remain open. No timing or
allocation gate is claimed by these functional checks.

## Poll transfer accounting — 2026-09-13

A compiled Execution-owned WorkBudget now shares per-thread accounting between the runtime batch and
eligible sequential task entry/return transfers. Nested budget scopes charge every enclosing allowance;
a failed charge changes none. A selected runtime source consumes one unit before invocation and refunds
it only if no work was available. A task transfer that exhausts the allowance defers through its already
reserved chain slot. This state is execution accounting, not ambient application metadata.

The new runtime regression runs a 128-child chain with batch allowances 1, 4, and 16, checking that each
poll visits no more children than its allowance and eventually completes the whole chain. Existing runtime
shutdown, queue fairness, timer, and owner tests remain intact.

Verification:

- Rebuilt all Async/Execution/IO/Net test targets in `build/tests`, Debug, then ctest
  `-L '^(Async|Execution|IO|Net)$'`: **362 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-poll-transfer-budget-tests.txt)).
- Rebuilt `IO_RuntimeLoopTestsTests` in `build/tests-asan`, RelWithDebInfo, then ctest
  `-R '^Base\.IO\.RuntimeLoop\.'`: **23 passed** under ASan/UBSan
  ([output](../measurements/async-runtime-poll-transfer-budget-sanitizers.txt)).
- The first build exposed a missing Defines.hpp include for the export macro; the corrected build passed.
  Final diff check passed. [Input hashes](../measurements/async-runtime-poll-transfer-budget-source.json)
  identify `build/async-runtime-evidence/poll-transfer-budget-checkpoint/inputs.tar.gz`.

T08/T09 remain partial. The tests bound visited child bodies, not every possible observer/helper resume
path. General eligible inline dispatch, observer accounting, nested pump-depth limits, explicit enqueue-only
semantics, and performance measurements still need completion. Installed shared-package validation of the
new compiled budget state has not yet run. Arbitrary callback instructions remain non-preemptible.

## Cooperative nested dispatch — 2026-09-13

Cooperative RunOne/RunOneAt now enters a compiled DispatchBudget before removing work. It consumes
one enclosing work-budget unit, refunds empty probes, and refuses further reentrant dispatch at depth 16.
An exhausted scope leaves work queued; after the outer callback returns, later pump calls can make
progress. General callback instructions and arbitrary user recursion remain non-preemptible.

Two focused regressions check a 100-callback recursively pumping workload (maximum active depth 16,
all work eventually runs) and two cooperative schedulers sharing a three-unit enclosing allowance
(one outer callback plus two inner callbacks). The depth setting is provisional until T09 measurements.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **364 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-nested-dispatch-tests.txt)).
- Rebuilt `Execution_CooperativeSchedulerTests` in `build/tests-asan`, RelWithDebInfo; ctest with
  `-R '^Base\.Execution\.CooperativeScheduler\.'`: **six passed** under ASan/UBSan
  ([output](../measurements/async-runtime-nested-dispatch-sanitizers.txt)).
- Rebuilt/installed `build/async-runtime-shared`, rebuilt its installed consumer, and ran Release ctest:
  **consumer passed**, verifying the new compiled budget symbols through the installed shared package
  ([output](../measurements/async-runtime-nested-dispatch-shared.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-nested-dispatch-source.json)
  identify `build/async-runtime-evidence/nested-dispatch-checkpoint/inputs.tar.gz`.

This remains partial T08/T09 work. Thread-pool helper dispatch, complete inline eligibility and publication
integration, enqueue-only semantics, and performance gates remain open. Observer delivery through a
queued callback already receives that callback's budget charge; it must not blindly require a second
charge that prevents progress when batchSize is one.

## Thread-pool dispatch budgets — 2026-09-13

ThreadPoolScheduler now enters the same DispatchBudget used by cooperative pumping before selecting
reserved, injected, local, or stolen work. Both its worker loop and RunOne helper pass through this point.
Exhaustion leaves queue records intact; empty probes refund their work charge. Dequeued shutdown work
still retires without entering its body, and that retirement consumes a work unit.

The new recursive-helping fixture runs 100 callbacks on one worker and verifies that all execute with
maximum active depth at most 16. A second fixture parks the worker, publishes a reserved completion
and five ordinary jobs, and verifies that a two-unit caller budget invokes exactly two items before
refusing further dispatch. After the scope ends, the helper drains the remaining work. The fixture
releases the parked worker during failure cleanup and retains callback state through pool destruction.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **366 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-pool-dispatch-budget-tests.txt)).
- Rebuilt `Execution_ThreadPoolFairnessTests` in `build/tests-asan`, RelWithDebInfo; ctest with
  `-R '^Base\.Execution\.ThreadPoolFairness\.'`: **four passed** under ASan/UBSan
  ([output](../measurements/async-runtime-pool-dispatch-budget-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-pool-dispatch-budget-source.json)
  identify `build/async-runtime-evidence/pool-dispatch-budget-checkpoint/inputs.tar.gz`.

T08/T09 remain partial: general inline dispatch eligibility/publication integration, the InlineScheduler
enqueue-contract gap, and final fairness/performance acceptance remain open. No timing gate is claimed.

## Eligible inline dispatch — 2026-09-13

ExecutorRef now exposes TryDispatchInline(WorkItem&), separate from ordinary submission. It requires
IsCurrent and a shared dispatch-budget entry; refusal leaves the item untouched. Published task
completion attempts this path only after claiming completion and releasing execution ownership, then
falls back to its existing reserved chain delivery. The completion handler retains the existing child
retirement-before-parent-resume protocol. Explicit yields keep their deferred path.

The API documents caller obligations: publish the complete result, perform retirement required before
user continuation, and release internal locks. The primitive cannot infer whether an arbitrary external
caller holds a lock. The integrated task path performs its publication before the attempt and does not
hold a scheduler lock. No new queue admission is required for mandatory fallback.

New tests check refusal outside executor context, refusal under a zero work allowance, inline execution
inside context, ordinary Execute remaining queued on a cooperative executor, and 100 recursive callbacks
completing via one reserved fallback slot with bounded depth.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **368 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-inline-dispatch-tests.txt)).
- Rebuilt Execution_InlineDispatchTests plus Async CompletionAwait, Sequential, Task, and
  ExecutorTransitions in `build/tests-asan`, RelWithDebInfo. Ctest with
  `-R '^Base\.(Execution\.InlineDispatch|Async\.(CompletionAwait|Sequential|Task|ExecutorTransitions))\.'`:
  **35 passed** under ASan/UBSan ([output](../measurements/async-runtime-inline-dispatch-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-inline-dispatch-source.json)
  identify `build/async-runtime-evidence/inline-dispatch-checkpoint/inputs.tar.gz`.

T08/T09 remain incomplete: backend adapters still require inline-publication integration, InlineScheduler
still has its synchronous Execute exception, and end-to-end fairness/performance acceptance remains open.
The functional checks do not close latency or allocation gates.

## File inline delivery — 2026-09-13

Fallback and native file completion adapters now retain the selected executor and awaiting handle at
admission. Their existing runtime completion callback still performs cancellation cleanup and final status
selection after backend access ends. It then moves the delivery ticket into a local, attempts eligible
inline resume, and otherwise dispatches that ticket. No awaiter/state member is accessed after inline
resume can destroy the awaiting frame. The runtime callback retains its shared completion state.

A new counted-delivery fixture runs fallback file work on the runtime executor. With a batch allowance
of 16 it observes zero selected-executor reserved callbacks; with allowance 1 it observes exactly one.
Both paths return the same value. This distinguishes inline delivery from mere eventual completion and
checks that exhaustion still uses pre-admitted capacity. Existing native-runtime and cross-executor
placement cases also pass.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **369 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-file-inline-tests.txt)).
- Rebuilt IO_AsyncDispatchTestsTests, IO_NativeRuntimeTestsTests, and IO_ExecutorPlacementTests in
  `build/tests-asan`, RelWithDebInfo; ctest with
  `-R '^Base\.IO\.(AsyncDispatch|NativeRuntime|ExecutorPlacement)\.'`: **16 passed** under ASan/UBSan
  ([output](../measurements/async-runtime-file-inline-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-file-inline-source.json)
  identify `build/async-runtime-evidence/file-inline-checkpoint/inputs.tar.gz`.

T08/T09 remain partial. Socket/resolver completion adapters, enqueue-only semantics, and final
fairness/performance gates remain open. Native Windows/macOS validation has not run for this shared
adapter change. This functional checkpoint makes no latency or allocation acceptance claim.

## Socket inline delivery — 2026-09-13

Immediate and pending socket completions now use the selected executor's budget-aware inline dispatch
after cancellation and socket-lease retirement. The operation retains executor/continuation information
at delivery admission. Before invoking user continuation it releases control/common-ticket ownership,
moves the reserved delivery ticket into a local, and uses that ticket if inline dispatch is unavailable.
Admitted immediate completions now consult the budget rather than only IsCurrent. Early admission
failures without a delivery ticket retain their existing synchronous error path.

A pending UDP receive regression counts selected-executor reserved callbacks after admission. It checks
zero callbacks under an eligible 16-unit runtime batch and exactly one under an exhausted one-unit batch,
along with received bytes and payload. Failure cleanup shuts down the runtime while borrowed state is
still alive. Existing socket cancellation, overlap, readiness, and resource-retirement cases remain intact.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `-L '^(Async|Execution|IO|Net)$'`: **370 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-socket-inline-tests.txt)).
- Rebuilt Net_SocketOperationTestsTests and Net_ExecutorPlacementTests in `build/tests-asan`,
  RelWithDebInfo; ctest with `-R '^Base\.Net\.(SocketOperation|ExecutorPlacement)\.'`:
  **14 passed** under ASan/UBSan ([output](../measurements/async-runtime-socket-inline-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-socket-inline-source.json)
  identify `build/async-runtime-evidence/socket-inline-checkpoint/inputs.tar.gz`.

Matching Windows implementation edits are unverified natively. T08/T09 remain incomplete: resolver
publication/delivery integration, enqueue-only semantics, and final fairness/performance gates remain open.

## Resolver publication and timer retirement — 2026-09-13

Resolver completion no longer submits to the ordinary queue or directly resumes on an arbitrary thread
when submission fails. It reserves delivery before setup and uses a two-event publication handshake:
setup must commit and a complete outcome must publish before delivery runs. This prevents synchronous
or concurrent cancellation from resetting registration storage while setup is still writing it. Delivery
retains its state, resets both cancellation registrations, removes timeout storage, then attempts eligible
inline dispatch with its reserved ticket as fallback.

Positive resolver timeouts now reserve removable timer storage before lookup submission. ThreadPoolScheduler
implements ScheduleTimer with stable identifiers, cancellation removal, and Detach support. Timer removal
and bulk clearing release callback captures outside the timer lock. Existing ExecuteAt preserves its timed
submission behavior by detaching the timer registration. The first resolver cancellation-race test exposed
missing thread-pool timer support; that failure remains in `resolver-race-tests.txt`, and the added support
made the unchanged outcome assertions pass.

New regressions verify repeated successful lookups with one timer slot and a one-hour deadline, completion
after ordinary executor admission closes without attempting rejected ordinary delivery, 100 setup/cancellation
races on a two-worker executor, prompt release of canceled timer captures, and detached timer execution.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **374 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-resolver-delivery-tests.txt)).
- Rebuilt Net Resolve/ExecutorPlacement, Async TaskContext, and Execution ThreadPoolFairness in
  `build/tests-asan`, RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.(Net\.(Resolve|ExecutorPlacement)|Async\.TaskContext|Execution\.ThreadPoolFairness)\.'`:
  **26 passed** under ASan/UBSan ([output](../measurements/async-runtime-resolver-delivery-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-resolver-delivery-source.json)
  identify `build/async-runtime-evidence/resolver-delivery-checkpoint/inputs.tar.gz`.

T08/T09 remain incomplete. CooperativeScheduler does not yet support removable timers, so positive resolver
timeouts on that executor currently reject before lookup; preserving timed cooperative use requires its
removal support next. Discarded queued resolver jobs still need an explicit terminal-publication owner.
InlineScheduler enqueue semantics and final fairness/performance gates remain open. Native platform
validation has not run for these portable changes.

## Cooperative removable timers — 2026-09-13

CooperativeScheduler now implements ScheduleTimer with stable identifiers and TimerRegistration ownership.
ExecuteAt retains queued behavior by detaching its registration. Timer insertion, cancellation, selection,
and counting share a mutex; callbacks and canceled captures run/destruct after releasing it. Ordinary
ready-queue submission and pumping retain their caller-serialization requirement.

Regressions verify cancellation from another thread, capture destruction that reenters PendingTimers,
preservation of another timer's deadline after removal, detached timer delivery, and 20 successful timed
resolver operations on a cooperative executor with zero retained timers after each completion. This closes
the temporary positive-timeout rejection noted in the preceding checkpoint.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **377 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-cooperative-timer-tests.txt)).
- Rebuilt Execution CooperativeScheduler, Net Resolve, and Async TaskContext in `build/tests-asan`,
  RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.(Execution\.CooperativeScheduler|Net\.Resolve|Async\.TaskContext)\.'`:
  **29 passed** under ASan/UBSan ([output](../measurements/async-runtime-cooperative-timer-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-cooperative-timer-source.json)
  identify `build/async-runtime-evidence/cooperative-timer-checkpoint/inputs.tar.gz`.

T08/T09 remain partial. Discarded resolver jobs still need terminal-publication ownership; InlineScheduler
submission semantics and final fairness/performance acceptance remain open. These functional checks do
not establish timer complexity, latency, or allocation acceptance under saturation.

## Resolver job discard — 2026-09-13

Queued resolver work now owns a movable discard guard until its body starts. Destruction before execution
signals the existing publication state. A discard observed during setup waits for commit, allowing a
synchronous admission failure to publish its specific scheduling error first. Once setup commits, discard
publishes SchedulerDispatchFailed/Stopped unless another outcome already won. Delivery still requires
outcome publication and setup commit; a late discard does not invoke a completed continuation again.

The shutdown regression parks the resolver worker, queues a lookup, closes driver admission on another
thread, then releases and joins shutdown. It verifies a terminal stopped fault. A second section cancels
and delivers first, then discards the queued lookup during shutdown to check against repeated delivery.

Verification:

- Rebuilt all Net test targets in `build/tests`, Debug; ctest with `--timeout 30 -L '^Net$'`:
  **64 passed, five existing TLS skips, zero failures**
  ([output](../measurements/async-runtime-resolver-discard-tests.txt)).
- Rebuilt Net_ResolveTests in `build/tests-asan`, RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.Net\.Resolve\.'`: **nine passed** under ASan/UBSan
  ([output](../measurements/async-runtime-resolver-discard-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-resolver-discard-source.json)
  identify `build/async-runtime-evidence/resolver-discard-checkpoint/inputs.tar.gz`.

This check covers the changed resolver surface; the prior broad suite remains the cooperative timer
checkpoint. T08/T09 remain incomplete pending enqueue semantics, the remaining dispatch audit, and
fairness/performance acceptance. No native-platform or timing gate is closed here.

## Enqueue contract — 2026-09-13

InlineScheduler owns no queue. Execute and ExecuteAt now reject without invoking or sleeping; deliberate
synchronous calls use Invoke. ExecutorRef's submission documentation explicitly requires queued execution,
distinct from TryDispatchInline. Cold task startup requires a queued executor, while inline ready outcomes
still need no submission. This deliberately changes the old InlineScheduler submission behavior to meet
the plan's enqueue contract.

The installed consumer and the Operation fault test now use cooperative pumping for actual task execution.
Tests for missing continuation storage use queued executor references with reservation support omitted;
this preserves testing at suspension/worker admission rather than accidentally failing at initial startup.
A new test verifies explicit invocation and that both queued/timed submission leave the call counter unchanged.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **379 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-enqueue-contract-tests.txt)).
- Rebuilt Execution InlineDispatch, Async ContinueWith/TaskContext, and IO AsyncDispatch in
  `build/tests-asan`, RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.(Execution\.InlineDispatch|Async\.(ContinueWith|TaskContext)|IO\.AsyncDispatch)\.'`:
  **27 passed** under ASan/UBSan ([output](../measurements/async-runtime-enqueue-contract-sanitizers.txt)).
- Canonical Execution InstalledConsumer ctest passed with GCC 15.2 and parallel level 4
  ([static output](../measurements/async-runtime-enqueue-contract-installed-static.txt)). Rebuilt/installed
  the existing Release shared tree and its consumer; ctest passed
  ([shared output](../measurements/async-runtime-enqueue-contract-installed-shared.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-enqueue-contract-source.json)
  identify `build/async-runtime-evidence/enqueue-contract-checkpoint/inputs.tar.gz`.

T08 remains open for the final dispatch/publication/retirement audit, including timer completion eligibility
and remaining direct-resume paths. T09 performance and full native-platform acceptance remain unproven.

## Timer wait inline delivery — 2026-09-13

TaskContext pending waits now publish setup commit separately from terminal outcome. Eligible timer
completion can invoke its retained Resume callback inline only after both events, using the selected
executor's dispatch budget. Timer-record retirement precedes publication; cancellation registration reset
and the borrowed-frame lease release precede user resume. Budget/context refusal uses the existing
reserved delivery ticket. Yield waits deliberately disable this inline path.

The new counted-delivery regression checks an eligible timer (zero reserved delivery callbacks), an
exhausted one-unit work scope (one reserved callback), and a yield with available capacity (still deferred).
All paths eventually complete successfully. Direct-resume review identified an additional gap: distinct
nested runtime loops share work accounting but do not yet enter the common dispatch-depth guard.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **380 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-timer-inline-tests.txt)).
- Rebuilt Async TaskContext/ChildLifetime/Environment and IO RuntimeLoop in `build/tests-asan`,
  RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.(Async\.(TaskContext|ChildLifetime|Environment)|IO\.RuntimeLoop)\.'`:
  **46 passed** under ASan/UBSan ([output](../measurements/async-runtime-timer-inline-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-timer-inline-source.json)
  identify `build/async-runtime-evidence/timer-inline-checkpoint/inputs.tar.gz`.

T08 remains incomplete pending the cross-runtime depth guard and final dispatch audit. T09 performance,
full native-platform acceptance, and the later redesign stages remain open.

## Runtime callback depth — 2026-09-13

Runtime source selection now enters DispatchBudget before dequeuing or invoking work. That guard
replaces the manual work-unit charge/refund, so callbacks retain the same work accounting while sharing
the global dispatch-depth bound with cooperative and thread-pool execution. Refusal leaves work untouched;
PollOnce still refreshes/reports native and queued backlog. A large nested batch cannot bypass the bound.

The new fixture seeds 64 independent runtime loops with callbacks that poll the next loop. Batch allowances
of 1024 isolate depth limiting from work exhaustion. It checks bounded initial progress, ready backlog at
refusal, restoration of the outer executor context, and eventual execution of all 64 callbacks on later polls.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **381 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-runtime-depth-tests.txt)).
- Rebuilt IO RuntimeLoop/RunTask in `build/tests-asan`, RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base\.IO\.(RuntimeLoop|RunTask)\.'`: **30 passed** under ASan/UBSan
  ([output](../measurements/async-runtime-runtime-depth-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-runtime-depth-source.json)
  identify `build/async-runtime-evidence/runtime-depth-checkpoint/inputs.tar.gz`.

The direct-resume review found another outstanding path: AsyncGenerator::EndAdvance still always queues
consumer delivery after publishing its yield/terminal boundary and releasing producer execution ownership.
Its eligible inline behavior and the remaining dispatch audit must be resolved before T08 is complete.
T09 performance and native-platform acceptance remain unproven.

## Generator consumer inline delivery — 2026-09-13

AsyncGenerator::EndAdvance now checks the consumer executor and shared dispatch budget after publishing
the yield/terminal boundary, releasing its lock, resetting the producer continuation, and releasing producer
execution ownership. Eligible delivery resumes the retained consumer; refusal uses its existing chain slot.
The producer's executor does not replace the independent consumer's placement. No producer frame access
follows delivery, so the consumer can release its last producer owner during resume.

A new fixture covers yield and terminal boundaries with both available and exhausted work budgets. It
releases the generator handle before completion, verifies inline versus deferred observable completion,
checks the result, and checks eventual destruction of a producer-owned capture. Existing generator tests
also exercise distinct executor placement, closed admission, concurrent advances, cancellation, and repeated
four-worker delivery.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in `build/tests`, Debug; ctest with
  `--timeout 30 -L '^(Async|Execution|IO|Net)$'`: **382 passed, six existing skips, zero failures**
  ([output](../measurements/async-runtime-generator-inline-tests.txt)).
- Rebuilt AsyncGenerator in `build/tests-asan`, RelWithDebInfo; ctest with
  `--timeout 30 -R '^Base.Async.AsyncGenerator.'`: **15 passed** under ASan/UBSan
  ([output](../measurements/async-runtime-generator-inline-sanitizers.txt)).
- Final diff check passed; [input hashes](../measurements/async-runtime-generator-inline-source.json)
  identify `build/async-runtime-evidence/generator-inline-checkpoint/inputs.tar.gz`.

T08's remaining dispatch audit, T09 fairness/performance measurements, later redesign stages, and native
platform acceptance remain open. In particular, reusable and foreign OperationAccess observer/retirement
notifications still use reserved queued delivery and need explicit classification in the final dispatch audit.

## T08 dispatch audit and final joins — 2026-09-13

Completed the [dispatch audit](DispatchAudit.md) as one batch. TaskScope and WhenAny now attempt eligible
inline dispatch for the final user join after child retirement and release of the state lock, with the
parent's reserved chain slot as fallback. Observer, cancellation-handoff, submission-race, and frame-retirement
notifications remain queued for the reasons recorded in the audit. Public async documentation now reflects
the integrated dispatch behavior.

Verification used the existing build trees, with no intermediate benchmark or source archive:

- Rebuilt all `Async_*Tests` targets in `build/tests`, Debug. Ctest with `--timeout 30 -L '^Async$'`:
  **138 passed**, zero failures. Log: `build/async-runtime-evidence/dispatch-audit-tests.txt`.
- Rebuilt `Async_TaskScopeTests`, `Async_WhenAnyTests`, and `Async_ChildLifetimeTests` in `build/tests-asan`,
  RelWithDebInfo. Ctest with `--timeout 30 -R '^Base.Async.(TaskScope|WhenAny|ChildLifetime).'`:
  **18 passed** under ASan/UBSan. Log: `build/async-runtime-evidence/dispatch-audit-asan-tests.txt`.
- Reviewed the changed join paths and focused regression diffs; `git diff --check` passed.

The new/extended regressions test inline and exhausted-budget joins after a retained frame is released,
and reject completion before retirement or on the external release stack. T08 is complete locally.
T09 must still measure budget tradeoffs and close Stage 1; native acceptance and later stages remain open.
