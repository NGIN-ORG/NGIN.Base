# T23 — Linux performance acceptance, 2026-09-29

**T23 remains open: performance acceptance fails.** All 63 benchmark processes
completed successfully, including the three separate allocation-instrumented
processes. Successful workload execution does not mean the latency gates passed.
No production code was changed or acceptance threshold relaxed.

## Source and method

- Original: `203f704e97a704259ef2db1ee8c93bf92763a7e1`, reconstructed from the
  preserved `original.tar`.
- Checkpoint: frozen pre-redesign `source.tar.gz`, Base `b44612eb896ab013463876e22a40e221a4fd8c5f`,
  production-equivalent to `77928461d91a72b8a8c893f3c0ab27b6b6af3364`.
- Redesign: that Base HEAD plus the current working-tree implementation, identified
  by the full source hashes below. Production sources still match T22.
- GCC 15.2.0, C++23, static Release `-O3 -DNDEBUG`; x86_64 Linux
  6.18.33.2-microsoft-standard-WSL2 on an i7-13700KF. Shared development host,
  no CPU pinning, default allocator, OpenSSL disabled.

Historical production headers/sources/CMake were checked byte-for-byte against the
archives. Only measurement harnesses and benchmark-target declarations were adapted.
The original uses its own background runtime and `Runtime::Stop`, without a
RuntimeRunner; the checkpoint uses its original Spawn/SyncWait API. The redesign
uses the already-migrated explicit owners. These ownership costs stay in the
measurements; they are not subtracted to improve the result.

The three sequential I/O harnesses now perform 100 untimed warmup operations per
case, followed by the same 2,000 measured operations. Identical warmup was added to
both historical harnesses. Cold setup is measured before warmup. Suspended-receive
throughput excludes warmup but includes measured rearm/barrier work. Payloads,
worker count, file access, endpoints, idle intervals, and completion checks remain
unchanged. The Async fixture retains 100 warmups and 1,000 samples; the mixed-load
fixture retains its 1,000 one-millisecond timer samples and eight configurations.

Five processes per version ran for each of IORuntimeBenchmarks,
IORuntimeFileBenchmarks, and IORuntimeNetworkBenchmarks. Five checkpoint/redesign
pairs ran AsyncRuntimeBenchmarks; five redesign processes ran IORuntimeMixedBenchmarks.
Version order rotated across rounds. All processes ran serially from the same Base
directory, after all builds finished. Three separate allocation executables ran
last; their timings are excluded. Source hashes were checked again after execution.
Medians below are medians of five process statistics, not pooled percentiles.

## Matched external-executor latency

Mean latency in microseconds; the unchanged mean allowance is +20%. The final
column compares redesign with the freshly rebuilt original, not the checkpoint.

| Workload | Original | Checkpoint | Redesign | Change |
| --- | ---: | ---: | ---: | ---: |
| Spawn/yield/join | 27.042 | 27.938 | 32.218 | +19.1% |
| Ready UDP | 30.314 | 34.715 | 37.410 | +23.4% |
| File stat, Auto | 56.071 | 70.892 | 82.289 | +46.8% |
| File stat, fallback | 56.770 | 72.007 | 78.744 | +38.7% |
| Native read, 64 KiB | 65.353 | 76.467 | 79.514 | +21.7% |
| Native write, 64 KiB | 88.317 | 74.945 | 76.514 | -13.4% |
| Fallback read, 64 KiB | 64.454 | 84.526 | 84.609 | +31.3% |
| Fallback write, 64 KiB | 60.183 | 83.068 | 85.198 | +41.6% |
| Suspended receive, 8 sockets | 43.288 | 45.712 | 64.102 | +48.1% |
| Suspended receive, 1,024 sockets | 43.886 | 42.831 | 57.923 | +32.0% |

Percentile allowance remains +25%. Against the matched original:

- Spawn/yield/join mean passes, but p95/p99 regress 42.2%/42.9%.
- Ready UDP mean and p95 fail (+23.4% and +36.1%); p50/p99 pass.
- Both file-stat modes and both fallback transfer workloads fail all four latency metrics.
- Native reads fail mean and p99 (+21.7% and +32.5%); p50/p95 pass.
- Native writes pass all four latency metrics.
- Eight-socket suspended receives fail all four latency metrics. At 1,024 sockets,
  mean/p50 fail (+32.0%/+61.7%), while p95/p99 pass.

The historical numeric baselines in IORuntimeMeasurements.md remain binding and
are preserved separately in the gate calculation. Relative to those original
recordings, native-read mean is **+47.6%**, fallback-read mean **+61.1%**, and
fallback-write mean **+60.8%**. All their latency metrics fail. File-stat means
regress 40.0%/29.2%; spawn/yield/join p95 and p99 also exceed the historical limit
(p99 is only just over, +25.048%). Native writes pass. Ready UDP fits the historical
numeric limits but fails the contemporaneous matched control above; neither
comparison establishes complete acceptance. No slower checkpoint replaces the baseline.

## Cold setup, idle, cancellation, and shutdown

- Native file cold-open: original/checkpoint/redesign **301.696 / 449.812 / 465.154 us**.
  +54.2% against the matched original fails the +50% gate. The historical original
  is 312.883 us, against which this run is +48.7% and narrowly inside the gate.
- Fallback cold-open: **169.014 / 248.037 / 238.438 us**. +41.1% passes against the
  matched original but +69.5% against the historical 140.695 us fails. Overall
  cold-file acceptance remains open.
- Ready-network cold setup is 276.018 us (+49.5% matched); eight-/1,024-socket setup
  is 298.577 / 106,989 us (+36.5%/+4.7%). File-stat cold setup also fits matched limits.
- One-second initialized-network CPU: **6,859 -> 23 us**, a **99.7% reduction**.
  With a pending receive: **7,147 -> 93 us**, a **98.7% reduction**. Both exceed the
  required 80% reduction. Idle socket populations also reduce CPU by over 99%.
- No periodic network waking was observed: the redesign's median voluntary
  context-switch counts are one with no pending receive and three with a pending
  receive, versus hundreds per second originally. These aggregate observations
  are evidence for the fixture, not a proof about every possible workload.
- Median single-receive cancellation is **158.164 us** and idle shutdown **59.198 us**;
  both pass the **1,000 us** limits. Population-wide cancel/join measurements remain
  separate from the single-operation gate.

## Composition cost and structural checks

Cannot-stop child-chain mean latency, checkpoint -> redesign, in microseconds:

| Executor | Depth 1 | Depth 8 | Depth 64 |
| --- | ---: | ---: | ---: |
| One external worker | 27.592 -> 30.133 | 32.620 -> 31.732 | 69.978 -> 40.020 |
| Four external workers | 29.790 -> 39.036 | 81.248 -> 39.052 | 352.138 -> 57.899 |
| Runtime | 1.190 -> 2.293 | 4.826 -> 2.932 | 33.775 -> 10.620 |

Deep chains improve, while fixed root/ownership costs have increased. The scope
spawn/join depth-1 mean is 1.917 us versus 1.033 us at the checkpoint. The current
child fixture allocates **13 / 20 / 76** times at depths 1 / 8 / 64 for both runtime
and external entry: one additional frame allocation per helper, with 12 fixed
allocations. The recorded checkpoint counts are 2 / 16 / 128 externally and
3 / 24 / 192 on the runtime. Full root lifecycle cost must not be described as
per-helper cost, and promise size is not compiler frame size.

- Ready value/void/error/domain-error/stopped fixtures: **zero allocations and bytes**.
- Structural chains at all three depths: **zero ordinary submissions, two reserved
  completions, three deliveries**, constant with depth. The owner boundary differs
  from the older Stage 1 one-reservation fixture; no per-helper reservation returns.
- Explicit cannot-stop callback registration: zero allocations/bytes. Stoppable
  registration costs one allocation. This run does not re-instrument production
  registration nodes or prove absence of cancellation-specific synchronization;
  T10's dedicated evidence remains separate.
- All five fairness runs assert progress for a due timer and unrelated work before
  a 4,096-frame chain finishes, and bounded work per poll. At budgets 16 / 64 / 256,
  maximum charged entries per poll are 16 / 64 / 256 and total polls 513 / 129 / 33.

## Mixed load and retained memory

All 40 mixed configurations (eight per process) passed payload, progress, and join
checks. Median loaded timer overshoot and throughput are below. These are redesign
progress measurements, not an original-runtime latency comparison; the original
runtime lacks the shared-loop contract used by this fixture.

| Files / child executor | Timer p50 / p95 / p99 (us) | UDP exchanges/s | File transfers/s |
| --- | ---: | ---: | ---: |
| Native / runtime | 3.802 / 21.268 / 47.481 | 62,878 | 34,567 |
| Native / external_one_worker | 23.850 / 63.243 / 104.577 | 65,224 | 34,407 |
| Fallback / runtime | 7.077 / 24.267 / 71.510 | 28,029 | 60,723 |
| Fallback / external_one_worker | 15.754 / 55.941 / 98.870 | 25,508 | 21,711 |

Separate allocation instrumentation passed submission, completion, timer, delayed
task, socket-operation/registration, and native/fallback file overload checks.
Rejected work adds **zero retained bytes**, and complete fixture teardown returns
tracked live bytes to the original floor. Native/fallback file peak requested live
bytes above the floor are 588,264 / 583,664 for capacity 64, two handles, four
cooperative executors, and 16 overload rounds. These are full-fixture measurements,
not per-I/O costs or OS RSS. Instrumentation self-checks also passed.

## Findings and remaining work

The redesign meets the measured ready-result, depth-independent dispatch,
fairness/progress, overload-retirement, idle-CPU, cancellation, and shutdown checks.
It does **not** meet end-to-end latency/cold-setup acceptance. Improving deep helper
composition and removing a file wrapper did not recover the original file limits.
The matched checkpoint comparison also exposes a suspended-socket regression and
higher fixed root ownership costs. Extra root allocations are measured; attributing
the file/socket slowdown to them requires targeted profiling or controlled changes.
No causal claim or accepted tradeoff is made here.

Keep T23 unchecked. Next performance work should isolate fixed root/SyncWait costs,
pending-receive owner/join costs, and file runtime/executor transfers, then rerun
these unchanged gates. T24–T27 and native Windows/macOS evidence remain open. This
measurement pass did not run native platforms or a shared-library matrix.

## Reproduction and evidence

Use the benchmark preset and optional-dependency settings in
[IORuntimeMeasurements.md](../IORuntimeMeasurements.md#reproduction). Reuse the
current static build and build the eight measured targets:

```sh
cmake --build build/benchmarks --config Release --target \
  IORuntimeBenchmarks IORuntimeFileBenchmarks IORuntimeNetworkBenchmarks \
  IORuntimeMixedBenchmarks AsyncRuntimeBenchmarks AsyncRuntimeAllocationBenchmarks \
  IORuntimeAllocationBenchmarks IORuntimeFileAllocationBenchmarks -j2
```

Reconstruct historical sources in separate empty directories from the preserved
archives, then apply the corresponding harness patch below. Configure each with
that same benchmark preset/compiler/settings. Build the three IORuntime timing
targets on both versions and AsyncRuntimeBenchmarks on the checkpoint. The patches
preserve the full recreated original harness and the frozen completed Stage 0
Async fixture; no lost temporary harness is needed to reproduce this series.

Run all timing binaries serially five times, rotating version order, then the three
allocation binaries separately. Exact executable paths, order, exits, source and
binary hashes are retained in the source identity artifact. Cold and idle work are
included by the executables. Production source identities were unchanged afterward.

- [All raw process output](../measurements/async-runtime-t23-linux.txt).
- [Five-process medians and ranges](../measurements/async-runtime-t23-summary.json).
- [Matched and historical gate calculations](../measurements/async-runtime-t23-gates.json).
- [Source, compiler, hardware, binary identities, and process commands](../measurements/async-runtime-t23-source.json).
- [Original harness reconstruction patch](../measurements/async-runtime-t23-original-harness.patch).
- [Checkpoint harness reconstruction patch](../measurements/async-runtime-t23-checkpoint-harness.patch).

Local build/configuration logs are `build/async-runtime-evidence/t23-*.log`.
Per-process logs, caches, harness archives, and manifests are under
`build/async-runtime-evidence/t23/`; the serial runner and analyzer are
`build/async-runtime-evidence/t23-{run,analyze}.py`. Historical build trees are under
`/tmp/ngin-t23-20260929/`; preserved archives and repository evidence can reconstruct
these if temporary files are removed. No branches, commits, or resets were used.
