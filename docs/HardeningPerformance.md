# NGIN.Base Hardening Performance Record

## Measurement setup

This record compares the hardening candidate with a detached `c0215e5`
worktree. Both builds used GCC 15.2, C++23, `Release` (`-O3 -DNDEBUG`), the same
standard library, and the same 13th Gen Intel Core i7-13700KF host under WSL2.
Optional benchmark comparison dependencies were disabled. Each listed
benchmark reports 100 measured samples after five warmups unless noted.

The baseline benchmark sources received only two harness corrections needed to
run the existing workloads: qualification of `Milliseconds` and selection of
the scheduler benchmark configuration before registration. No baseline library
code was changed.

These numbers are intended to identify material changes and their scale. They
are not portable performance promises or fixed regression thresholds.

## Baseline comparison

| Surface | `c0215e5` | Candidate | Interpretation |
|---|---:|---:|---|
| `FlatHashMap` sequential insert + get, 1,000 each | 0.00427 ms | 0.00585 ms | +0.00158 ms total; strict pre-insert load ceiling and transactional growth add under 1 ns per map operation in this workload |
| `FlatHashMap` random insert + get | 0.01112 ms | 0.01088 ms | No regression |
| `FlatHashMap` mixed workload | 0.158 ms | 0.166 ms | About +4.5%; within run-to-run system noise for this host |
| `Vector<int>` reserved append, 20,000 | 0.00380 ms minimum | 0.00443 ms minimum | About +0.63 microseconds total; no added allocations |
| `Vector<int>` middle insertion, 512 | 0.00675 ms average | 0.00562 ms average | Improved |
| `Vector<int>::ShrinkToFit`, 20,000 | 0.000040 ms | 0.00119 ms | The baseline effectively skipped compaction; the candidate performs the required relocation |
| Fiber scheduler, run 10,000 jobs | 71.4 ms | 57.6 ms | No throughput regression; this workload is noisy |
| Thread-pool scheduler, run 10,000 jobs | 67.8 ms | 67.5 ms | Flat |
| Fiber timed submission, 10,000 timers | 1.18 ms | 1.64 ms | About +46 ns per accepted timer for explicit validation and failure reporting |
| Thread-pool timed submission, 10,000 timers | 2.24 ms | 3.85 ms | About +162 ns per accepted timer for explicit validation and failure reporting |
| Cooperative task yield, 10,000 tasks | 2.41 ms | 2.79 ms | About +38 ns per task for race-safe lifecycle ownership |
| Cooperative task yield eight times, 2,000 tasks | 0.812 ms | 1.21 ms | About +25 ns per yield boundary |
| Cooperative `WhenAll(2 x Yield)`, 10,000 | 12.1 ms | 14.5 ms | About +0.24 microseconds per structured child pair |
| Allocator workloads | — | — | Minimum and average times remained effectively unchanged |
| Callable invocation workloads | — | — | Minimum and average times remained effectively unchanged |

The scheduler comparison showed that ordinary queue execution remains flat.
The material scheduler differences are concentrated where the new contract
checks shutdown/rejection and translates allocation failure, and where tasks
now retain and release frames across continuation races. Those bounded costs
are accepted for the correctness guarantees; they are not moved into unrelated
allocator, callable, or plain work-queue paths.

## Focused candidate measurements

The focused hardening executable uses 100 samples after 20 warmups and retains
raw timings for percentile calculation.

| Workload | Median | p95 | p99 |
|---|---:|---:|---:|
| Inline `WorkItem` construct + invoke, 10,000 | 13.2 microseconds | 13.3 microseconds | 13.3 microseconds |
| Heap `WorkItem` construct + invoke, 10,000 | 128 microseconds | 137 microseconds | 179 microseconds |
| Cancellation register + reset, 10,000 | 461 microseconds | 548 microseconds | 653 microseconds |
| Fire 256 cancellation callbacks | 3.09 microseconds | 3.21 microseconds | 3.50 microseconds |
| `Shared` create + copy + weak lock + release, 10,000 | 412 microseconds | 448 microseconds | 518 microseconds |
| `WhenAny`, responsive loser cancellation and drain | 1.04 microseconds | 1.06 microseconds | 1.07 microseconds |
| `WhenAny`, loser drains after 16 yields | 1.84 microseconds | 1.89 microseconds | 1.91 microseconds |

Direct heap storage for large `WorkItem` callables costs about 12.8 ns per
operation at the median, while the inline path remains allocation-free at about
1.3 ns per operation. This does not justify reintroducing a shared work-item
pool and its contention or ABA risk.

## Size checks

| Type or executable | `c0215e5` | Candidate |
|---|---:|---:|
| `WorkItem` | 80 bytes | 80 bytes |
| `Shared<int>` | 8 bytes | 8 bytes |
| `Ticket<int, SystemAllocator>` | 8 bytes | 8 bytes |
| `CancellationRegistration` | 72 bytes | 24 bytes |
| `Task<void>` | 40 bytes | 40 bytes |
| `Operation<void>` | 40 bytes | 40 bytes |
| Scheduler benchmark executable | 243,264 bytes | 258,208 bytes |
| Vector benchmark executable | 88,512 bytes | 92,400 bytes |
| Map benchmark executable | 104,048 bytes | 104,408 bytes |

The executable-size increase is concentrated in explicit error and lifecycle
paths. Dense public handle sizes did not grow, and cancellation registrations
became substantially smaller by moving stable state into owned nodes.

## Follow-up rule

Future optimization should first target timed-submission validation and task
transition count while preserving observable failure and ownership semantics.
Do not trade away the load ceiling, transactional rollback, or exactly-once
frame destruction to recover the measured differences. Re-run the same focused
workloads after any such optimization and compare medians plus p95/p99 tails.
