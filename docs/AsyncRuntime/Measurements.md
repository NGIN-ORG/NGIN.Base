# Async runtime redesign evidence

Stage 0 cost measurements are recorded below. These are baseline measurements, not evidence of a completed redesign.
The frozen baseline production source is `b44612eb896ab013463876e22a40e221a4fd8c5f` (production-equivalent to
`77928461d91a72b8a8c893f3c0ab27b6b6af3364`).

## Frozen inputs (T01)

The [source manifest](../measurements/async-runtime-source.json) records every tracked and nonignored
untracked input before changes, including the user's planning-document edits, with SHA-256 identities.
Local immutable evidence is under `build/async-runtime-evidence/pre-redesign/`:

- `source.tar.gz`: all 775 working input files, including uncommitted planning inputs.
- `original.tar`: `git archive 203f704e97a704259ef2db1ee8c93bf92763a7e1`.
- `checkpoint.tar`: `git archive 77928461d91a72b8a8c893f3c0ab27b6b6af3364`.
- `working.patch`, `CMakeCache.txt`, and `manifest.json`: original tracked diff and configuration/identities.

Extract archives into separate empty directories to reconstruct source without changing this worktree.
The archives are local build artifacts, not committed source. Retain them when cleaning build trees.
Historical raw workloads and the refresh harness hashes remain in [IORuntimeMeasurements.md](../IORuntimeMeasurements.md)
and `docs/measurements/ioruntime-*`; none has been replaced. The original revision did not contain the later
I/O benchmark sources. Historical refresh harness hashes identify those adapted harnesses; the historical
JSON alone does not preserve their contents. A newly reconstructed original harness must be hashed and its
adaptation recorded before matched final comparisons.

The unchanged gates against the original matched baseline are: latency percentiles at most +25%, mean at
most +20%, cold setup at most +50%, idle CPU reduced by at least 80%, no periodic network wakeups, and median
single-operation cancellation and idle shutdown each below 1 ms. The checkpoint cannot reset these limits.

## Cost fixtures (T02)

Build using the existing benchmark preset/settings documented in IORuntimeMeasurements:

```sh
cmake --build build/benchmarks --config Release --target AsyncRuntimeBenchmarks AsyncRuntimeAllocationBenchmarks -j 4
build/benchmarks/benchmarks/Release/AsyncRuntimeBenchmarks
build/benchmarks/benchmarks/Release/AsyncRuntimeAllocationBenchmarks
```

Both targets require the existing static IO component. The allocation executable links the repository's
replacement-new instrumentation; its latency output must not be used as uninstrumented timing evidence.
Each case uses 100 warmups followed by 1,000 samples. Run processes serially without overlapping builds.
The host/compiler/cache are in the frozen manifest; Release currently uses `-O3 -DNDEBUG -std=c++23`.

`construction` constructs and destroys only the outer cold frame, regardless of requested eventual depth.
`child` starts and joins the whole chain. `yield` adds one leaf suspension. External cases use one/four pool
workers; runtime cases explicitly drive PollOnce. Cancellation modes 0/1/2 are cannot-stop, stoppable, and
already requested. The existing implementation cooperatively checks at yield, not every startup; a chain
without a check can succeed after a stop request. Mid-submission cancellation needs separate fixtures.

`registration` measures callback register/unregister; `link` measures explicit linked-source construction
and destruction. `context-copy` measures the current typed context, not a future environment implementation.
The compiler may optimize copies with no observable effects; tiny timing values are not evidence of the
unoptimized copy cost. Allocation counters still distinguish empty/stoppable token paths.

`structural` counts calls through a cooperative executor adapter. Submissions and reservations are distinct:
reusable reserved completion delivery is not counted as an ordinary submission. The adapter also counts actual callback invocation, including reusable reserved delivery. It runs only in
the structural case, outside timed samples; its wrapper allocations are not attributed to production work.

`file-synthetic` compares a completed backend leaf, the real FileOperationGate wrapper around that leaf, and
the public AsyncFileHandle wrapper around both. It performs no OS transfer and cannot attribute native/fallback
worker or kernel costs. Setup and driver creation occur outside the sample. It preserves the actual gate's
admission and release behavior rather than substituting another wrapper implementation.

The promise-size field excludes compiler frame storage. In the first GCC allocation run the outer Chain
construction allocated 624 bytes once; that is an observed allocation for this exact coroutine, not a portable
frame-size guarantee. External direct chains allocated twice per depth and runtime chains three times per
depth. Cooperative structural counts were one submission and one reservation per task at depths 1/8/64.

## Recorded results

[Raw five-process results](../measurements/async-runtime-pre-redesign-linux.txt) and
[fixture/binary/cache identities](../measurements/async-runtime-fixture-identities.json) identify the completed
matrix. The exact baseline harness is additionally retained locally as
`build/async-runtime-evidence/pre-redesign/complete/AsyncRuntimeBenchmarks.cpp` so later API migrations do
not erase its identity. Historical baseline thresholds are unchanged.

Median of five **uninstrumented mean** latencies, in microseconds:

| Fixture | Depth 1 | Depth 8 | Depth 64 |
| --- | ---: | ---: | ---: |
| External, one worker, cannot-stop child chain | 28.664 | 34.532 | 71.264 |
| External, four workers, cannot-stop child chain | 30.736 | 85.859 | 340.187 |
| Runtime, cannot-stop child chain | 1.111 | 4.497 | 32.208 |

| Synthetic file fixture | Backend leaf | Gate + leaf | Public handle + gate + leaf |
| --- | ---: | ---: | ---: |
| Runtime executor | 1.097 | 2.145 | 2.545 |
| One external worker | 28.538 | 30.359 | 31.122 |
| Four external workers | 30.898 | 51.791 | 64.757 |

External chain allocations are 2/16/128 and runtime allocations 3/24/192 for depths 1/8/64. Cooperative
structural counts are 1/8/64 submissions, 1/8/64 reservations, and 1/15/127 callback invocations.
The synthetic file leaf/gate/public layers allocate 3/6/9 times on the runtime and 2/4/6 on the external pool.
Ready task storage does not exist in the baseline API, so a zero-allocation explicit-ready case is unavailable
until T05. A leaf coroutine that immediately returns is still a cold frame, not that representation.

`active-cancel` first drives a future delay into suspension, then cancels and drains its continuation without
sleep-based synchronization. It includes source creation and teardown. The cooperative scheduler retains
its canceled future timer notification until scheduler teardown; fractional allocation counts include
occasional timer-container growth, not fractional callback registrations. `scope-spawn-join` includes scope
creation, one admitted child, and asynchronous join, with the child's chain at each depth. Full scheduler and
driver setup is outside timed windows. This is not an overload-memory or final timer-retirement acceptance test.

[Registration-node counts](../measurements/async-runtime-registration-counts.txt) come from a separate build
with an atomic counter after successful registration-node allocation. Every cannot-stop case creates zero
nodes. Plain sequential chains create zero nodes regardless of depth or token state; stoppable yielded leaves
and actively canceled pending delays create exactly one per invocation, independent of depth. Already-stopped
yield skips registration. Explicit nonempty callback registration allocates one node; linking allocates only
when its input needs a live callback. This corrects any assumption that every helper already registers cancellation.

The temporary [instrumentation patch](../measurements/async-runtime-registration-instrumentation.patch)
and [identities](../measurements/async-runtime-registration-identities.json) are preserved; production instrumentation
has been removed and Cancellation.hpp was checked byte-for-byte against the frozen input. Reapply the patch
only to the saved baseline harness/source in an isolated directory, configure with the normal benchmark preset
and `-DCMAKE_CXX_FLAGS=-DNGIN_ASYNC_MEASURE_REGISTRATIONS=1`, then run AsyncRuntimeAllocationBenchmarks.
The counter configuration defines the macro in every translation unit, including static runtime code, avoiding
mixed inline definitions. Its timings are not acceptance evidence.

## CPU and synchronization profiles

The local GNU profiler was available without installing a dependency. Commands, each run serially:

```sh
gprofng collect app -p hi -s off -o build/async-runtime-evidence/pre-redesign/async-cpu.er build/benchmarks/benchmarks/Release/AsyncRuntimeBenchmarks
gprofng collect app -p hi -s on -o build/async-runtime-evidence/pre-redesign/async-profile.er build/benchmarks/benchmarks/Release/AsyncRuntimeBenchmarks
gprofng collect app -p hi -s on -o build/async-runtime-evidence/pre-redesign/file-profile.er build/benchmarks/benchmarks/Release/IORuntimeFileBenchmarks
gprofng display text -functions build/async-runtime-evidence/pre-redesign/async-cpu.er
```

[CPU-only profile](../measurements/async-runtime-async-cpu-functions.txt): 48.35% exclusive sampled CPU was
in syscall; queue execution and child-await paths account for large overlapping inclusive totals.
[Synchronization profile](../measurements/async-runtime-async-profile-functions.txt) records 306,810 wait
observations in the aggregate helper fixture, including completion-queue paths. Wait totals aggregate threads
and are not wall-clock latency. Synchronization tracing adds substantial collector overhead; do not use its
CPU proportions or printed workload timings as uninstrumented performance results.

[Native/fallback file profile](../measurements/async-runtime-file-profile-functions.txt) sampled 0.089 CPU
seconds across the four 2,000-operation file workloads; 56.18% was in syscall. The short sample and unresolved
libc frames cannot reliably attribute the historical file regression to a particular lock or atomic operation.
Experiments retain the exact sampled load objects locally. The helper CPU profile predates the final additional
runtime-cancellation/external-file cases; its exact executable is archived in its experiment and corresponds
to the earlier `final/` fixture snapshot, not the later `complete/` snapshot.

The evidence establishes per-helper queue/reservation cost and separately measures wrapper overhead. More
workers substantially increase serial-chain and synthetic-wrapper cost in this fixture. It supports removing
unnecessary helper dispatches and keeping concurrent state at ownership boundaries. It does **not** establish
that wrappers explain the entire native/fallback regression, or guarantee that the redesign meets file gates.
Matched end-to-end original/checkpoint/redesign comparisons, overload memory, fairness, and shutdown remain
T21–T23 work. Native Windows/macOS execution remains unverified.

## Baseline verification

The final benchmark matrix passed both Debug ASan/UBSan executables in the existing `build/benchmarks-asan`
configuration. [Raw fixture output](../measurements/async-runtime-fixture-sanitizers.txt) includes positive,
pre-canceled, actively canceled suspended, and scoped cases. No production behavior was modified.

```sh
cmake --build build/benchmarks-asan --config Debug --target AsyncRuntimeBenchmarks AsyncRuntimeAllocationBenchmarks -j 4
build/benchmarks-asan/benchmarks/Debug/AsyncRuntimeBenchmarks
build/benchmarks-asan/benchmarks/Debug/AsyncRuntimeAllocationBenchmarks
ctest --test-dir build/tests -C Debug --output-on-failure -L '^(Async|Execution|IO|Net)$'
```

Before CTest, the corresponding repository-generated `<group>_<source-stem>Tests` targets for each `.cpp`
directly under `tests/Async`, `tests/Execution`, `tests/IO`, and `tests/Net` were built with
`cmake --build build/tests --config Debug --target <targets> -j 4`.
[CTest output](../measurements/async-runtime-focused-baseline-tests.txt): 332 selected, 326 passed, six skipped,
zero failures. Skips were native Windows IOCP and five TLS integration cases unavailable in this configuration.
Full build logs remain in the local evidence directory (`final-asan-build.txt`, `focused-baseline-build.txt`).
These baseline checks do not verify the upcoming redesign or native Windows/macOS behavior.

## Stage 1 composition and fairness (T09), 2026-09-13

Measured the completed T05–T08 working tree with GCC 15.2, C++23, Release `-O3 -DNDEBUG`, using the
existing static benchmark build. Five uninstrumented processes ran serially after builds/tests finished;
each fixture used 100 warmups and 1,000 samples. One separate allocation-instrumented process followed.
[Timing output](../measurements/async-runtime-stage1-timing.txt) and
[allocation output](../measurements/async-runtime-stage1-allocations.txt) retain the full results.
No baseline or acceptance threshold changed.

Median of five uninstrumented mean latencies, microseconds:

| Cannot-stop child chain | Depth 1 | Depth 8 | Depth 64 |
| --- | ---: | ---: | ---: |
| External, one worker | 34.615 | 34.921 | 41.993 |
| External, four workers | 35.944 | 41.549 | 44.899 |
| Runtime | 1.230 | 1.769 | 10.161 |

The one-worker depth-1 mean is about 20.8% above the recorded Stage 0 28.664 us; deeper composition improves
substantially. This is a retained performance follow-up, not a passed end-to-end performance gate. These
processes are not a matched rerun of the original historical I/O baseline. T23 must investigate root startup
and apply the unchanged original mean/percentile/setup limits across the required workload matrix.

Structural depths 1/8/64 each make **one submission, one reservation, one callback delivery**. Ordinary
sequential Task code performs no cancellation registration; explicit notification boundaries own those
registrations. Scalar ready value/void/error/domain/stopped fixtures each allocate **zero times and bytes**.
External chains allocate 3/10/66 times; runtime chains 4/11/67. Only one allocation per added coroutine frame
remains; root/runtime bookkeeping does not grow per helper. Outer construction is measured separately.
Promise/state/task/context/environment sizes are 296/240/88/112/96 bytes; promise size is not compiler frame size.

The new fairness fixture runs a 4,096-frame chain, starts it with one poll, then queues a due timer and
unrelated work. Every sample asserts both execute before the chain completes, eventually drains the chain,
and checks that entries per poll do not exceed the allowance. All five processes produced identical progress
counts. Whole-chain mean includes allocation, startup, all polls, result consumption, and teardown.

| Poll allowance | Median whole-chain mean, us | Timer/unrelated entry observed | Total polls | Maximum entries per poll |
| --- | ---: | ---: | ---: | ---: |
| 16 | 894.541 | 32 / 32 | 513 | 16 |
| 64 | 515.270 | 128 / 128 | 129 | 64 |
| 256 | 435.300 | 512 / 512 | 33 | 256 |

Keep default poll allowance **64**: compared with 256, this fixture services competing work after one
quarter as many chain entries, at about 18% greater whole-chain time; 16 costs about 74% more time than 64.
These are work-count tradeoffs, not hard wall-clock latency bounds or proof of a globally optimal setting.
Keep the chain transfer allowance **256**: 64-frame entry/return composition fits without mandatory delivery
hops, while 10,000-frame tests defer and permit unrelated work. Keep nested dispatch depth **16**, verified by
recursive reserved fallback and cross-runtime nested-poll tests. These are internal tuning values.

Stage verification rebuilt all Async/Execution/IO/Net targets and selected **389 tests: 383 passed, six
existing Windows/TLS skips, zero failures**. Log: `build/async-runtime-evidence/stage1-final-tests.txt`.
This includes transfer/transition outcomes, 10,000-frame success/failure, one-slot saturation, explicit yields,
inline refusal, bounded/reentrant polling, retirement, and concurrent scope/race joins. Relevant sanitizer
coverage is recorded in the T08 audit and preceding implementation records; no production change was made
for this measurement pass. Native-platform and full original-baseline acceptance remain later gates.


The current pre-integration file-layer baseline and ownership map are recorded in
[Stage4.md](Stage4.md). Use `--file-only` on either AsyncRuntime benchmark executable
for this focused fixture; the default still runs all workloads.

## T23 final-stage comparison (2026-09-29)

[Performance.md](Performance.md) records five-process matched original/checkpoint/redesign
Release measurements, mixed-load progress, and separate allocation/overload results.
The three sequential I/O timing fixtures now include 100 untimed warmups before
2,000 measured operations; the same adaptation applies to both historical controls.
All 63 processes passed workload checks, but latency/cold-setup acceptance still
fails. T23 remains open, with original thresholds and historical raw results retained.
