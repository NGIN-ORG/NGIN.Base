# I/O runtime measurements

## Baseline and thresholds (before execution changes)

The initial baseline is recorded in
[ioruntime-baseline-linux.txt](measurements/ioruntime-baseline-linux.txt).
It uses GCC 15.2, Release, Linux 6.18 on WSL2, default allocation, no CPU pinning,
and a shared development host. Five separate processes each perform 2,000
sequential operations. Latency includes task start and blocking join; it is not
a measurement of syscall latency alone. UDP payloads are 64 bytes on loopback.
The continuation scheduler has one worker and one timer thread. Idle observation
lasts one second per scenario. Native selection succeeds with io_uring here;
metadata operations still use workers, so this does not measure native reads.

Medians across the five runs:

| Workload | p50 (µs) | p95 (µs) | p99 (µs) |
| --- | ---: | ---: | ---: |
| Spawn, yield, join | 25.878 | 41.912 | 73.140 |
| Ready UDP exchange | 28.269 | 49.494 | 83.170 |
| File stat, Auto | 52.988 | 87.202 | 140.079 |
| File stat, Fallback | 54.995 | 91.858 | 138.086 |

An initialized idle network runtime causes about 927 voluntary context switches
per second and 7–9 ms of process CPU per second. There are four total threads
(application, continuation worker, scheduler timer, network). File metadata with
Auto uses six threads after networking stops; Fallback uses five. Cancellation
of an idle receive has median 88.233 µs; stopping an idle network has median
588.940 µs. The mixed IO/Net measurement executable has 538,767 text bytes; this
is **not** a network-only binary-size baseline.

Thresholds fixed before implementation: compare median-of-five runs on the same
host/compiler/build configuration. For the same external-executor workloads,
p50/p95/p99 may increase at most 25%, mean latency at most 20%, and cold setup at
most 50%. Idle CPU must decrease by at least 80%; there must be no periodic
network wakeups. Cancellation and idle shutdown median must remain below 1 ms.
New single-thread entry workloads are reported separately because removing the
external executor changes what is measured. Correctness requirements are not
relaxed to meet these thresholds. Explain and justify any accepted tradeoff.

## Reproduction

The additional native-file baseline was captured from the preserved original
implementation before replacing its io_uring worker. Five Release processes
performed 2,000 sequential 64 KiB reads and writes at offset zero in a warm
temporary regular file, with one external continuation worker. Raw results are
in [ioruntime-file-baseline-linux.txt](measurements/ioruntime-file-baseline-linux.txt).

| Workload | p50 (µs) | p95 (µs) | p99 (µs) | Mean (µs) |
| --- | ---: | ---: | ---: | ---: |
| Native read | 49.080 | 73.567 | 106.417 | 53.857 |
| Native write | 72.924 | 108.224 | 152.053 | 79.389 |
| Fallback read | 48.206 | 70.272 | 104.080 | 52.507 |
| Fallback write | 48.542 | 72.937 | 104.254 | 52.978 |

Before the native backend change, the same limits are fixed for these workloads:
at most 25% increase in p50/p95/p99, 20% in mean, and 50% in cold open latency,
using medians of five runs on the same host. Native cold-open median was
312.883 µs; fallback was 140.695 µs. These are cached-file throughput/latency
workloads, not durable-storage bandwidth measurements.

From the NGIN.Base directory:

```sh
cmake --preset benchmarks -DCMAKE_CXX_COMPILER=/opt/gcc-15.2/bin/c++ \
  -DNGIN_BENCH_USE_SIMDJSON=OFF -DNGIN_BENCH_USE_RAPIDJSON=OFF \
  -DNGIN_BENCH_USE_PUGIXML=OFF -DNGIN_BENCH_USE_TINYXML2=OFF \
  -DNGIN_BENCH_USE_GLM=OFF
cmake --build build/benchmarks --config Release --target IORuntimeBenchmarks IORuntimeFileBenchmarks IORuntimeNetworkBenchmarks -j 4
build/benchmarks/benchmarks/Release/IORuntimeBenchmarks
build/benchmarks/benchmarks/Release/IORuntimeFileBenchmarks
build/benchmarks/benchmarks/Release/IORuntimeNetworkBenchmarks
```

The compiler path is specific to the measurement host. Optional unrelated
comparison libraries are disabled; the benchmark adds no dependency.

## Shared-loop integration checkpoint

This intermediate measurement predates reusable task reservations and TaskScope;
it is not final acceptance. The same five-process external-executor file workload
produced these median values after Linux file completions moved onto the shared
loop. Raw data is preserved in
[ioruntime-file-shared-loop-checkpoint-linux.txt](measurements/ioruntime-file-shared-loop-checkpoint-linux.txt).

| Workload | p50 (µs) | p95 (µs) | p99 (µs) | Mean (µs) |
| --- | ---: | ---: | ---: | ---: |
| Native read | 60.541 | 85.767 | 127.786 | 65.189 |
| Native write | 61.080 | 88.078 | 128.145 | 65.718 |
| Fallback read | 67.858 | 95.228 | 134.492 | 73.253 |
| Fallback write | 68.467 | 103.251 | 149.940 | 74.317 |

Native write improved. Native read mean slightly exceeded the 20% threshold;
fallback latency and cold-open time exceeded their thresholds. The extra transfer
through the runtime's completion queue is a candidate cause, not yet an isolated
measurement. Further tuning and final comparisons remain required. The recorded
thresholds are unchanged.

## Sequential refresh after ownership and host integration (2026-09-10)

The original tracked revision `203f704e97a704259ef2db1ee8c93bf92763a7e1`
was restored from Git after the earlier temporary build disappeared. This is the
same revision recorded in the initial raw baseline. Source/harness hashes are
recorded in [ioruntime-refresh-source.json](measurements/ioruntime-refresh-source.json).
Its benchmark-only harness
removes RuntimeRunner and calls the original Runtime::Stop; production source
is unchanged. Rebuilt original and current static Release executables use GCC
15.2, the same host and working directory, and the same workload parameters.
Five pairs ran serially, with no benchmark process overlapping another build or
measurement. They are additional controls; the original numeric acceptance
thresholds above remain unchanged. Raw data is in
[ioruntime-sequential-refresh-linux.txt](measurements/ioruntime-sequential-refresh-linux.txt).

Median-of-five current results (microseconds):

| Workload | p50 | p95 | p99 | Mean |
| --- | ---: | ---: | ---: | ---: |
| Spawn/yield/join | 23.283 | 31.706 | 58.760 | 25.003 |
| Ready UDP exchange | 45.693 | 71.906 | 108.115 | 50.076 |
| File stat, Auto | 64.552 | 91.216 | 139.399 | 70.161 |
| File stat, Fallback | 64.789 | 99.103 | 155.571 | 71.111 |
| Native read, 64 KiB | 64.034 | 114.694 | 156.101 | 71.501 |
| Native write, 64 KiB | 63.695 | 105.503 | 161.330 | 71.367 |
| Fallback read, 64 KiB | 70.792 | 116.692 | 164.941 | 78.216 |
| Fallback write, 64 KiB | 71.176 | 110.341 | 154.927 | 76.968 |

Performance acceptance still fails. Ready UDP, native reads, and fallback file
transfers exceed the recorded latency limits. Native cold-open median is
622.336 us (recorded baseline 312.883 us); fallback cold-open is 237.701 us
(baseline 140.695 us), both outside the 50% allowance. Native write latency
remains improved or within its limits. These regressions must be resolved or
supported by an explicit justified tradeoff before declaring completion.

The paired refresh helps separate host variation from implementation cost:
ready UDP mean increased 71.9%, native read mean 34.7%, and fallback read/write
means 48.5%/45.2% against the rebuilt original. No threshold was raised to absorb
those differences. Immediate POSIX socket operations currently register and
unregister native readiness even when the syscall succeeds immediately; this
is a concrete candidate for tuning, not yet an isolated causal result. A separate
[diagnostic syscall-count run](measurements/ioruntime-sequential-syscalls-linux.txt)
of the same full workload recorded 8,011 epoll_ctl calls currently versus two
originally, consistent with registering and removing readiness for each of the
2,000 send/receive pairs. Current code also generated more control wakes and
futex calls. The traced elapsed times are excluded from performance results.

Idle network CPU fell from a paired median 6,748 us to 30 us during a one-second
observation. Voluntary context switches fell from 935 to one (the observing
thread's sleep); both executables had four threads. Idle receive cancellation
was 118.095 us and idle shutdown 58.976 us, below the fixed 1 ms limits. These
idle improvements do not offset the latency failures above.

## Network-only scaling workload (2026-09-10)

`IORuntimeNetworkBenchmarks` links only Net and its transitive components. Each
process compares 8 and 1,024 loopback UDP receivers with 8 active receivers,
64-byte payloads, 2,000 sequential exchanges, and one external continuation
worker. Every receiver has a unique endpoint and one pending read. A submission
barrier outside each latency sample ensures the read is suspended before the
synchronous send. The sample ends after the receive is joined; reported
throughput additionally includes rearming and the barrier. This measures a
controlled request/response workload, not maximum concurrent network bandwidth.
Each population has a one-second idle observation, cold setup, cancel-and-join
of all pending receives, and shutdown after application joins.

Five serial original/current pairs completed. The current fixture also completed
both populations under Debug ASan/UBSan with no reported sanitizer failures; its
instrumented timings are excluded from the Release results. Medians are below; raw data is in
[ioruntime-network-scaling-linux.txt](measurements/ioruntime-network-scaling-linux.txt).
The initial fixture allowed reused ephemeral UDP ports and produced stalls in
both implementations. It was corrected before the accepted series; the failed
attempts and endpoint reproduction are recorded in
[ioruntime-network-fixture-validation-linux.txt](measurements/ioruntime-network-fixture-validation-linux.txt).

| Population | Implementation | p50 (us) | p95 (us) | p99 (us) | Mean (us) | Exchanges/s including rearm |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 8 | Original | 34.965 | 59.776 | 107.356 | 39.423 | 14,612.8 |
| 8 | Current | 37.201 | 60.787 | 108.712 | 41.498 | 12,621.5 |
| 1,024 | Original | 35.062 | 58.398 | 100.946 | 39.166 | 14,783.2 |
| 1,024 | Current | 36.216 | 57.525 | 102.770 | 39.822 | 12,911.4 |

Current suspended latency stays close across these populations. Relative to the
original, mean latency increased 5.3% with 8 sockets and 1.7% with 1,024; rearm-
inclusive throughput decreased 13.6% and 12.7%. This separates suspended receive
cost from the larger ready-exchange regression; it is not evidence that every
mixed or saturated workload has passed.

At 1,024 pending receives, one-second idle CPU fell from 12,118 us to 21 us and
voluntary switches from 931 to one. Both processes had four threads and reported
no filesystem backend. Canceling and joining all 1,024 reads took 28.100 ms
versus 26.824 ms originally; this batch includes 1,024 sequential application
joins and is distinct from the single-operation 1 ms cancellation gate. Median
idle shutdown afterward was 63.743 us versus 600.680 us. The fixture reports
process high-water RSS as contextual OS data, not a measurement of queue memory
or allocation counts; those require separate instrumentation.

GNU size reports 239,596 text bytes for the current network-only executable and
185,332 for the original, an increase of 54,264 bytes (29.3%). This is a comparable
whole-workload binary measurement, including runtime code and benchmark/standard
library support. It replaces no earlier threshold and is not a claim that
component isolation reduces total code size. No filesystem source is referenced
by the current benchmark; the runtime confirms no file backend is initialized.

## Deferred POSIX readiness checkpoint (2026-09-11)

Socket admission now reserves the bounded runtime registration before the first
nonblocking syscall, then arms native readiness only when the operation must
wait. Eligible immediate operations retire the reservation without touching the
platform poller. An immediate write alongside a pending read also leaves that
read's existing interest unchanged. Capacity exhaustion still rejects before OS
I/O; unarmed registrations participate in stop drainage.

Five serial pairs compared the preserved prior Release binary with this change,
using the unchanged sequential and Net-only harnesses. These are two versions
of the new runtime, not the original baseline. Executable hashes and all runs
are in [ioruntime-deferred-watch-linux.txt](measurements/ioruntime-deferred-watch-linux.txt).
The shared host showed higher latency than the September 10 series, including
in the unchanged yield workload. No runs were discarded and no thresholds were
changed. Median ready-UDP p50/p95/p99/mean, in microseconds:

| Version | p50 | p95 | p99 | Mean |
| --- | ---: | ---: | ---: | ---: |
| Before deferred readiness | 54.326 | 159.829 | 228.042 | 71.039 |
| Deferred readiness | 44.063 | 104.511 | 148.362 | 54.141 |

The paired mean improved 23.8%, but still exceeds the original limits. Suspended
receive means changed from 54.267 to 51.680 us with 8 sockets and from 49.912 to
52.434 us with 1,024. This change targets immediate operations; these suspended
results do not establish a consistent improvement. Idle observations retained
one voluntary switch per second without pending reads. Deferred-ready receive
cancellation and idle stop medians were 150.114 and 80.519 us.

A separate full-workload diagnostic reduced epoll_ctl calls from 8,011 to seven.
Futex calls fell from 123,612 to 100,460, but read/write wake traffic remained
near 12,000 calls each. Traced elapsed times are excluded from latency results;
raw counts are in [ioruntime-deferred-watch-syscalls-linux.txt](measurements/ioruntime-deferred-watch-syscalls-linux.txt).
This isolates the native-registration reduction while identifying remaining
completion-retirement notifications as a separate tuning target.

## Completion retirement notification checkpoint (2026-09-11)

Releasing completion capacity while a queue is open no longer wakes its executor.
Publishing runnable work still wakes it. Closing admission also wakes it, and
retiring the final outstanding reservation after close wakes any shutdown drain
waiter. This preserves accepted delivery and shutdown without notifying on every
unused reservation or completed callback.

Five serial pairs compared deferred socket readiness before and after this
change. The file-only comparison uses its preserved prior executable, which
predates the socket-only optimization and does not perform socket operations.
Executable hashes, every run, backend selections, cold setup, and idle results
are in [ioruntime-queue-wake-linux.txt](measurements/ioruntime-queue-wake-linux.txt).
All measurements use the existing GCC 15.2 static Release configuration and
unchanged harnesses on the shared host. These tuning comparisons do not replace
the original baseline or its limits.

| Workload | Before mean (us) | After p50 (us) | After p95 (us) | After p99 (us) | After mean (us) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Spawn/yield/join | 33.943 | 27.215 | 48.387 | 70.785 | 30.019 |
| Ready UDP | 53.369 | 34.063 | 63.678 | 96.125 | 38.784 |
| Stat, Auto | 87.664 | 77.674 | 131.451 | 172.056 | 86.350 |
| Stat, Fallback | 82.662 | 76.438 | 124.941 | 172.715 | 83.058 |
| Native read, 64 KiB | 80.753 | 72.847 | 130.626 | 177.351 | 81.180 |
| Native write, 64 KiB | 82.083 | 72.200 | 130.539 | 179.487 | 80.607 |
| Fallback read, 64 KiB | 88.799 | 82.522 | 137.839 | 182.329 | 90.821 |
| Fallback write, 64 KiB | 88.331 | 81.981 | 132.635 | 182.446 | 89.993 |

Ready-UDP mean improved another 27.3% in this paired series. Its p50 and p99 now
fit the original numeric allowances; p95 (63.678 versus a 61.868 us limit) and
mean (38.784 versus a 38.625 us limit) still exceed them. No allowance is rounded
up to classify a failure as a pass. File latency did not consistently improve
and still exceeds several original limits. Native and fallback cold-open medians
were 483.248 and 264.599 us, above their 469.325 and 211.043 us allowances.
Performance acceptance remains open.

Suspended receive means changed from 52.332 to 49.553 us with 8 sockets and from
50.026 to 52.269 us with 1,024. Rearm-inclusive throughput changed from 9,978 to
10,616 exchanges/s and from 10,392 to 9,862, respectively. These results do not
show a consistent suspended-path improvement. Current idle receive cancellation
and idle shutdown medians were 151.887 and 66.565 us; the 1,024-receiver idle
observation used 20 us of CPU and one voluntary switch over one second.

A separate syscall diagnostic reduced read/write counts from 12,023/12,019 to
4,019/4,015, epoll_wait from 24,036 to 12,030, and futex calls from 102,697 to
87,996. Epoll_ctl remained at seven. Traced elapsed times are excluded from
performance results; see [ioruntime-queue-wake-syscalls-linux.txt](measurements/ioruntime-queue-wake-syscalls-linux.txt).

## Allocation and overload checkpoint (2026-09-11)

`IORuntimeAllocationBenchmarks` uses benchmark-only replacement `new`/`delete`
operators and the existing first-party system allocator. It counts requested
C++ allocation bytes, allocations, and peak live requested bytes. It does not
count C/custom-allocator calls that bypass replacement `new`, allocator arenas,
instrumentation headers/padding, RSS, or kernel buffers. It therefore measures
tracked C++ storage rather than total process memory. Allocation instrumentation
changes allocation behavior; its timings are not latency acceptance data.
The target requires static Net components, including their transitive runtime
components, so allocation calls are covered consistently across library boundaries.

The workload drives one runtime on its owner thread. Its instrumentation
self-check additionally allocates from four concurrent threads, and verifies
ordinary, array, aligned, zero-size, standard-PMR, failed overflow, and matching
free paths. It rejects a platform whose standard PMR resource bypasses the
replacement allocation instrumentation.
Five GCC 15.2 static Release processes produced identical workload counts. The
complete fixture also passed Debug ASan/UBSan, with instrumentation and linked
components enabled. Raw measurements and hashes are in
[ioruntime-allocation-overload-linux.txt](measurements/ioruntime-allocation-overload-linux.txt).
This is a new runtime-executor workload, not an original/external-executor comparison.

Each queue has a tested capacity of 64. Direct submission, completion, and timer
workloads each perform 128 rounds of 64 accepted requests and 1,024 rejected
requests. Completion rounds alternate unused reservation retirement and dispatch.
Timers use a one-hour deadline and own a 1,024-byte payload. Delayed-task and
socket workloads each perform 16 measured rounds of 64 pending operations plus
256 rejected attempts; each round cancels and joins all accepted tasks. There is
one warm-up round before their counters start. Socket fixtures use separate
endpoints, 64-byte buffers, and no filesystem service.

| Workload | Attempts | C++ allocations | Peak live bytes above initial level | Bytes retained after cleanup |
| --- | ---: | ---: | ---: | ---: |
| Ordinary submissions | 139,264 | 8,192 | 7,168 | 0 |
| Completion reservations | 139,264 | 8,192 | 7,168 | 0 |
| One-hour callback timers with payloads | 139,264 | 16,384 | 74,240 | 0 |
| One-hour delayed tasks | 5,120 | 31,840 | 89,896 | 0 |
| Pending sockets, operation limit | 5,120 | 32,848 | 153,840 | 0 |
| Pending sockets, registration limit | 5,120 | 53,344 | 156,176 | 0 |

Rejected direct callbacks/reservations/timers allocate no additional queue
storage. Rejected tasks still allocate transient coroutine/admission state to
report failure. For delayed tasks and registration-limited sockets, the first
rejection retains an extra 1,024 bytes of reusable cancellation-table capacity
while the group is live. The table grows for the transient registration and
keeps its capacity after unregistering it. The fixture records this increase,
checks that every later rejection plateaus, and requires all tracked bytes and
allocation objects to return to the original level after group cleanup.
The initial stricter fixture assumption and its diagnostic are preserved in
[ioruntime-allocation-fixture-validation-linux.txt](measurements/ioruntime-allocation-fixture-validation-linux.txt).
The reported peaks include this growth; no production validation was weakened.

Canceled callback timers release their payloads and timer storage before another
`PollOnce`, and leave no deadline. Canceled delayed tasks release their retained
frames after cancellation delivery and joining. Repeated rounds also verify
capacity recovery and the expected overload errors. Every complete fixture,
including the runtime itself, returns to its initial tracked allocation level.
These are observed bounds for the stated payloads and admission counts, not a
universal byte limit on arbitrary application captures or buffers.

Ready and suspended 64-byte receives each perform 2,000 operations. Both use
22,000 allocations and 4,464,000 requested bytes: **11 allocations and 2,232 bytes
per receive**, with no retained bytes after joining. A separate executor decorator
counts actual selected-executor callbacks; its extra callable storage is excluded
from the raw allocation comparison. Each receive submits one task-start callback
and reserves two selected-executor completions. Ready receives invoke no completion
callback; suspended receives invoke exactly one. The benchmark asserts these
counts for the POSIX readiness path. The Windows backend still retires a primed
receive through an IOCP packet, so its fixture expects a completion callback in
both cases; Windows execution remains unverified here. Native event handlers and private runtime cancellation tickets are outside
this selected-executor callback counter.

Reproduce from NGIN.Base using the existing benchmark configuration with
`NGIN_BASE_BUILD_STATIC=ON` (and `NGIN_BASE_BUILD_SHARED=OFF` for the recorded setup):

```sh
cmake --build build/benchmarks --config Release --target IORuntimeAllocationBenchmarks -j 4
build/benchmarks/benchmarks/Release/IORuntimeAllocationBenchmarks
cmake --build build/benchmarks-asan --config Debug --target IORuntimeAllocationBenchmarks -j 4
build/benchmarks-asan/benchmarks/Debug/IORuntimeAllocationBenchmarks
```

The sanitizer tree must have `NGIN_BASE_ENABLE_ASAN=ON` so linked components are
instrumented too; it uses the same benchmark preset and optional-library settings
as the reproduction configuration above.

## Mixed load and thread-pool fairness checkpoint (2026-09-11)

`IORuntimeMixedBenchmarks` runs four independent 64-byte UDP exchanges, one
64-KiB explicit-offset file reader, one writer on a separate range, and two
continuously yielding tasks. The root records 1,000 relative one-millisecond
runtime delays, then signals every worker to stop and joins them before closing
resources. Overshoot includes delay construction and continuation dispatch; this
is not an absolute-period timer or a catch-up loop. Each worker must make progress,
and transfers validate their sizes and contents. Rates include the final joins.

Each process covers native and fallback files, idle and loaded timers, and child
continuations on either the runtime or one external worker. The root and timer
always use the runtime executor, so this does not compare external-root entry
costs. Cold setup includes runtime/pool/socket creation and opening a prepared
128-KiB file; preparation is outside the measured interval. Four receivers use
unique ephemeral endpoints without address reuse.

The first loaded external-worker case failed because a UDP worker made no
progress. Source inspection found strict completion priority and newest-local
priority in `ThreadPoolScheduler`: continuously replenished work could starve
injected tasks or older local tasks. Workers now rotate completion delivery,
injected work, oldest local work, and newest local work. Deterministic tests
require progress from all four sources within 512 callbacks, and verify that an
external `RunOne` helper can steal from the sole worker. The initial failed
measurement is preserved in
[ioruntime-mixed-fixture-validation-linux.txt](measurements/ioruntime-mixed-fixture-validation-linux.txt).

Five serial GCC 15.2 static Release processes passed all eight cases. Values below
are medians of each process's reported percentiles, not pooled percentiles; all
raw runs and hashes are in
[ioruntime-mixed-linux.txt](measurements/ioruntime-mixed-linux.txt).

| Files | Child executor | Load | Timer overshoot p50 / p95 / p99 (us) | UDP exchanges/s | File transfers/s |
| --- | --- | --- | ---: | ---: | ---: |
| Native io_uring | Runtime | Idle | 120.989 / 161.796 / 208.350 | 0 | 0 |
| Native io_uring | Runtime | Mixed | 9.949 / 22.881 / 56.966 | 66,562 | 26,877 |
| Native io_uring | External, one worker | Idle | 119.436 / 168.580 / 217.647 | 0 | 0 |
| Native io_uring | External, one worker | Mixed | 17.678 / 52.148 / 89.457 | 39,493 | 10,185 |
| Fallback | Runtime | Idle | 122.610 / 164.485 / 202.647 | 0 | 0 |
| Fallback | Runtime | Mixed | 12.280 / 25.642 / 69.035 | 59,516 | 29,258 |
| Fallback | External, one worker | Idle | 120.750 / 166.256 / 208.092 | 0 | 0 |
| Fallback | External, one worker | Mixed | 17.012 / 60.396 / 99.410 | 34,728 | 8,939 |

Loaded timers often overshoot less because yielding work keeps the loop awake;
this trades CPU activity for prompt dispatch and is not evidence that loading a
system generally improves latency. Progress counts span each complete run and
are not per-window fairness bounds. The complete mixed fixture also passed with
Debug ASan/UBSan, whose timing results are excluded.

Five additional serial before/after pairs isolate the pool policy change from
the preceding deferred-monitoring and completion-wake changes. Neither side is
the original baseline. Ready UDP p50 / p95 / p99 / mean changed from
30.686 / 61.867 / 91.690 / 35.997 us to
30.619 / 52.251 / 78.264 / 34.665 us. Those final checkpoint values meet the fixed
original ready-UDP limits. Native read mean improved from 78.575 to 74.048 us;
fallback read/write means remained around 82 us. File read/fallback and stat
percentiles still exceed original limits, and fallback cold-open remains over
its limit. These regressions remain unresolved.

Suspended receives with eight sockets worsened in this sample: p95 increased
from 67.909 to 90.787 us, mean from 44.701 to 49.275 us, and throughput decreased
from 11,346 to 10,503 operations/s. With 1,024 sockets and eight active, mean
changed from 44.962 to 44.428 us. The fairness fix is required for progress;
these measurements do not establish a universal speedup. All runs, including the
slower ones, are preserved in
[ioruntime-pool-fairness-linux.txt](measurements/ioruntime-pool-fairness-linux.txt).
This checkpoint predates the service-wide file-gate budget change.

Reproduce the mixed workload with the existing benchmark configuration:

```sh
cmake --build build/benchmarks --config Release --target IORuntimeMixedBenchmarks -j 4
build/benchmarks/benchmarks/Release/IORuntimeMixedBenchmarks
cmake --build build/benchmarks-asan --config Debug --target IORuntimeMixedBenchmarks -j 4
build/benchmarks-asan/benchmarks/Debug/IORuntimeMixedBenchmarks
```

## Shared file admission and memory checkpoint (2026-09-11)

The file gate now reserves a slot from its file service before queueing accepted
work. `files.queueDepthHint` bounds queued plus active handle operations across
all handles and continuation executors. This is separate from the worker-job and
native-request budgets with the same configured size, and from the runtime's
backend-completion budget. A canceled waiter releases its slot when its executor
processes cleanup; a slow executor can retain capacity but cannot admit unlimited
waiters through additional executors. Rejected close leaves admission open.

`IORuntimeFileAllocationBenchmarks` uses the existing replacement-new
instrumentation and static IO components. It opens two handles to a prepared
128-KiB file, then admits 64 sequential 64-byte reads across four externally driven
cooperative executors. During saturation only these executors are driven: the
first read on each handle remains admitted pending runtime delivery, so later
shared-position reads wait at their gates. It then rejects 256 additional reads,
cancels and joins every accepted operation, closes both handles, shuts down, and
destroys the complete fixture. Each backend runs one warm-up fixture and 16
measured fixtures. **Initialization and teardown are included**; this is not a
per-read steady-state allocation count or a count of gate-node storage alone.

Five GCC 15.2 static Release processes and the complete Debug ASan/UBSan fixture
passed. Each backend performs 5,120 read attempts per process, in addition to
fixture setup and close operations. Raw runs and hashes are in
[ioruntime-file-allocation-overload-linux.txt](measurements/ioruntime-file-allocation-overload-linux.txt).

| Backend | C++ allocations | Requested bytes allocated | Peak live bytes above initial level | Retained after each full fixture |
| --- | ---: | ---: | ---: | ---: |
| Native io_uring | 27,024 | 17,330,304 | 375,528–375,640 | 0 |
| Worker fallback | 27,184 | 17,311,744 | 372,944 | 0 |

The first rejection retained at most zero additional bytes for native and 1,024
for fallback in these runs. Subsequent rejections could release existing worker
captures but could not increase live allocation count or bytes beyond that first
rejection's level. Rejected operations still allocate transient coroutine/error
state. Every full fixture returned both live bytes and allocation count to its
initial level. The measurement excludes C/custom allocations bypassing replacement
new, allocator bookkeeping, instrumentation headers/padding, RSS, and kernel memory.
The producers here are multiple cooperative executors driven by one thread;
separate correctness tests exercise four concurrent producers, but this fixture
does not measure concurrent-producer peaks or compare the original baseline.

Five serial file-latency before/after pairs isolate the admission change from the
preceding thread-pool fairness checkpoint. Native read mean changed from 75.515
to 74.695 us and native write from 72.112 to 73.952 us. Fallback read changed from
77.970 to 77.904 us and fallback write from 78.882 to 81.408 us. Native and fallback
cold-open medians changed from 427.215 / 228.776 us to 415.496 / 236.956 us.
Native read and fallback latency, and fallback cold-open, still exceed original
limits. Those limits have not been relaxed.

The final mixed fixture passed all eight cases in five additional Release
processes and under ASan/UBSan. Loaded timer p95 / p99 overshoot was
22.747 / 58.009 us (native/runtime), 56.662 / 104.684 us (native/external),
24.308 / 64.095 us (fallback/runtime), and 60.092 / 114.641 us
(fallback/external). These are medians of reported process percentiles. Raw
before/after file results and all final mixed runs are in
[ioruntime-file-budget-linux.txt](measurements/ioruntime-file-budget-linux.txt).

Reproduce the allocation fixture using the static benchmark configurations:

```sh
cmake --build build/benchmarks --config Release --target IORuntimeFileAllocationBenchmarks -j 4
build/benchmarks/benchmarks/Release/IORuntimeFileAllocationBenchmarks
cmake --build build/benchmarks-asan --config Debug --target IORuntimeFileAllocationBenchmarks -j 4
build/benchmarks-asan/benchmarks/Debug/IORuntimeFileAllocationBenchmarks
```

## Outstanding measurement coverage

Complete concurrent TCP directional progress under load and comparable
external-executor versus runtime-executor entry workloads. Extend allocation and
overload measurements to concurrent producer pressure and original-baseline
comparisons. Audit remaining cleanup failure paths.
Resolve sequential latency and cold-start regressions against the fixed thresholds,
then repeat final comparisons. Native Windows execution remains assigned to the
user; macOS validation is also outstanding.
