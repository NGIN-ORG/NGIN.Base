# ConcurrentHashMap – Evolution & Hardening Plan

Purpose: Provide a phased roadmap to harden correctness, reintroduce safe high-performance read paths, and evolve migration & reclamation mechanisms for `ConcurrentHashMap` while preserving current API and performance goals.

---
## Guiding Principles
- Lock-free / wait-free bias for read operations; bounded contention for writes.
- Predictable latency: avoid pathological probe/backoff explosions.
- Deterministic memory reclamation without hidden global pauses.
- Incremental changes: each phase is independently reviewable & testable.
- Header-only friendliness preserved; no external deps.

---
## Current State Summary (Baseline)
- Cooperative resizing via group migration.
- Sharded size accounting; approximate + flushed committed size.
- Memory safety fixes applied: finalized guards, deferred mask poisoning, removal of unsafe optimistic read path.
- Remaining issue: potential lost-key during migration due to early-return logic (phantom migrated group). (To be addressed first.)

---
## Phase Overview

| Phase | Goal | Key Changes | Risk | Metrics |
|-------|------|-------------|------|---------|
| 0 | Correctness patch | Remove finalized early-return in `MigrateGroup`; ensure accurate migratedGroups accounting | Low | All stress tests pass across seeds (>=1000 runs) |
| 1 | Safe optimistic reads v2 | Reintroduce snapshot reads guarded by dual-epoch + retired-list check | Medium | Latency reduction vs guarded read path; UAF-free under ASAN/TSAN |
| 2 | Migration protocol refinement | Two-phase migration (claim groups → mark complete → publish finalization) reducing slot-level races | Medium | Fewer CAS failures & yields during heavy resize |
| 3 | Reclamation robustness | Epoch-based deferred free or hazard-pointer-lite to handle long readers; eliminate retired list retries | Medium | Tightened memory usage curve under long-lived readers |
| 4 | Adaptive probe & relocation | Optional mini-relocation during insert to reduce future probe length (Robin Hood / partial) | Medium/High | Reduced 99p probe length; maintain lock-freedom |
| 5 | Instrumentation & telemetry (opt-in) | Lightweight counters (contention, migration time, probe histograms) under macro guard | Low | Visibility without perf regression when disabled |
| 6 | Benchmark & tuning pass | Systematic measurement across capacities, load factors, thread counts | Medium | Updated docs & recommended defaults |

---
## Detailed Phase Plans

### Phase 0 – Migration Correctness Patch
1. Remove finalized guard inside `MigrateGroup`; keep guard before claiming group (or remove entirely – finalization only after all groups migrated).
2. Add assert (debug only) after finalization: `migratedGroups == oldGroupCount`.
3. Add test that inserts disjoint ranges across threads, then verifies full key coverage.
4. Run stress test seeds (1..250 plus selected large seeds) under ASAN.
5. Add deterministic reproduction test for missing key scenario (seed 100) to ensure failure no longer occurs.

Acceptance Criteria:
- No missing keys across exhaustive range test.
- ASAN/TSAN clean.

### Phase 1 – Safe Optimistic Reads v2
Approach: Reinstate snapshot reads when (epoch even && no active migration && no retired list non-null). Add an atomic `retiredCount` or single flag to avoid dereferencing already freed memory.
Steps:
1. Introduce `std::atomic<bool> hasRetired;` flipped when pushing onto retired list; cleared when list fully reclaimed.
2. Optimistic read path:
   - Load `epoch` (acquire).
   - If `epoch` even && `m_migration == nullptr` && `!hasRetired.load(acquire)`:
     - Load published view; attempt copy; re-check invariants.
3. Fallback to guarded path on mismatch.
4. Provide compile-time macro `NGIN_CHM_OPT_READ` to disable for debugging.

Tests:
- Targeted race tests forcibly starting migration mid-read (inject hooks).
- Fuzz: random delays inserted around snapshot and verification.

Metrics:
- Benchmark single-thread read throughput and multi-thread read-heavy workload before/after.

### Phase 2 – Two-Phase Migration
Issue: Current scheme intermixes slot transfers with finalization condition evaluation, creating windows for high contention.
Design:
1. Phase A: Mark groups as claimed and perform payload moves; maintain per-group completion flag.
2. Phase B: Single thread scans completion flags; when all true, sets `finalized`.
3. Remove reliance on counting increments that might be skewed by early exits.
4. Simplify CAS patterns and backoff logic inside `MigrateGroup`.

Instrumentation (debug): per-group completion array (bitset) stored in `MigrationState`.

### Phase 3 – Reclamation Robustness (Epoch/Hazard Lite)
Motivation: Readers can stall old group memory reclamation (especially under long-running iteration). Retired list may pile up.
Design Options:
A. Epoch Counter per thread cached TLS; quiescent points (end of public API calls) update local epoch. Reclaim when min thread epoch > migration epoch.
B. Hazard pointer: Readers publish a pointer to active migration state; reclamation occurs when none reference it.
Chosen: Epoch (simpler, constant memory per thread; avoids per-node hazards).
Implementation Outline:
- Global `std::atomic<size_t> globalEpoch` incremented on migration finalize.
- Thread-local `size_t localEpoch` updated at start/end of operations.
- Retired migrations store epoch at retirement; reclaim when all thread epochs > retired epoch.
- Provide API `Quiesce()` to force update and reclaim.

### Phase 4 – Adaptive Probe & Optional Robin Hood
Goal: Reduce variance in probe length at high load without penalizing concurrent writers.
Design:
- On insert when encountering long probe (> configurable threshold), attempt small backward shift of earlier slot if its displacement < ours (classic Robin Hood heuristic) using CAS on control bytes.
- Abort if contention observed (limit to 1–2 relocations per insert).
- Provide macro toggle `NGIN_CHM_ROBIN_HOOD` default off.

### Phase 5 – Instrumentation (Opt-In)
Add counters guarded by `#if defined(NGIN_CHM_STATS)`:
- `size_t probeDisplacementsHistogram[...];`
- Migration duration (nanoseconds).
- Contention retries.
Public read API for snapshot stats (returns struct with fields; no dynamic alloc).

### Phase 6 – Benchmark & Tuning
Scenarios:
1. Read-only (Contains/Get) varying key set size.
2. Write-heavy (Insert) at different load factors.
3. Mixed (80% read / 20% write).
4. Migration surge tests (batch inserts triggering multiple resizes).
5. Long-running steady-state with periodic migrations.

Metrics:
- Ops/sec (aggregate) and p50/p95/p99 latency per op (microbench harness with steady timer).
- Memory overhead vs raw payload size.
- Probe length distribution.

---
## Testing Strategy & Matrix

Dimensions:
- Thread counts: 1, 2, 4, 8, 16, 32.
- Key range sizes: 1K, 16K, 256K, 1M.
- Load factor targets: 0.50, 0.70, 0.85 (over-threshold to test robustness).
- Operation mixes: 100% read, 100% insert, mixed (R/W), insert/remove churn.
- Migration frequency: Normal (default doubling), Aggressive (force by low threshold), Stress (rapid small capacity increases).

Test Categories:
1. Unit:
   - Deterministic migration (force small capacity -> insert N items -> verify).
   - Removal semantics (tombstone cleanup, reinsertion in tombstone slot).
   - Size accounting flush boundary conditions (delta negative > base, zero flush threshold).
2. Property:
   - All inserted keys present, no spurious keys.
   - Size() equals number of unique inserted keys after operations.
3. Concurrency:
   - Mixed churn (insert/remove/upsert) verifying invariants.
   - Reader-writer race with continuous migrations.
4. Performance regression guards (optional thresholds):
   - p95 probe length not > X at load factor 0.75.

Edge Cases:
- Empty map Get/Remove.
- Very large key causing hash collisions (custom hash stub).
- Highly skewed hash distribution.
- Max capacity growth until hitting `maxPowerOfTwo` boundary.

---
## Debug & Verification Hooks
- Macro `NGIN_CHM_VERIFY_AFTER_MIGRATION`: after finalize, linear scan ensuring no `Occupied` slot exists in oldGroups; confirm moved counts match expected size.
- Macro `NGIN_CHM_FAILFAST_ON_TORN_VIEW`: retains current debug `terminate()` checks.
- Optional injection points via lambdas stored in `MigrationState` (only in tests, not public API) for forcing race windows.

---
## Benchmark Harness Additions
Add new benchmark source: `benchmarks/ConcurrentHashMapBench.cpp` (if not existing) with scenarios enumerated above. Use steady clock, pre-sized vector of operations, run warmup + measurement phases.

Benchmark Output Targets:
- CSV (optional) and console summary.
- Provide simple scriptable invocation.

---
## Fish Shell Commands (Run Book)

### Configure Builds
```fish
# Standard build (multi-config Ninja) – ensure presets exist
cmake --preset tests-asan
cmake --preset benchmarks-release
```

### Build Targets
```fish
# Build tests (ASAN)
cmake --build build/tests-asan --config RelWithDebInfo -j (nproc)
# Build benchmarks (Release)
cmake --build build/benchmarks-release --config Release -j (nproc)
```

### Run Core Stress Tests
```fish
# Single seed
./build/tests-asan/tests/RelWithDebInfo/Containers_ConcurrentHashMapStressTests --rng-seed 100 --abortx 1
# Batch seeds (1..250)
for s in (seq 1 250)
    ./build/tests-asan/tests/RelWithDebInfo/Containers_ConcurrentHashMapStressTests --rng-seed $s --abortx 1
end
```

### Forced Migration Micro-Test
```fish
# Optional dedicated test (after adding) named Containers_ConcurrentHashMapMigrationTests
./build/tests-asan/tests/RelWithDebInfo/Containers_ConcurrentHashMapMigrationTests -r concise
```

### Benchmark Example
```fish
./build/benchmarks-release/benchmarks/Release/ConcurrentMapBench --scenario mixed_read_heavy --threads 16 --keys 100000
```

### TSAN Validation (slower)
```fish
cmake --preset tests-tsan
cmake --build build/tests-tsan --config RelWithDebInfo -j (nproc)
./build/tests-tsan/tests/RelWithDebInfo/Containers_ConcurrentHashMapStressTests --rng-seed 42
```

### Epoch/Hazard Experimental Toggle (once added)
```fish
set -gx CXXFLAGS "$CXXFLAGS -DNGIN_CHM_EPOCH_RECLAIM=1"
cmake --preset tests-asan
cmake --build build/tests-asan --config RelWithDebInfo -j (nproc)
```

---
## Acceptance Gates Per Phase
| Gate | Description | Tooling |
|------|-------------|---------|
| Safety | ASAN/TSAN clean | Sanitizer builds |
| Correctness | All stress/property tests pass | Catch2 test suite |
| Performance | No regression (>5%) vs previous phase baseline in read or mixed benchmarks | Benchmark harness |
| Complexity | Code diff minimal & documented | Review diffs |
| Documentation | Plan & README updated | Markdown review |

---
## Documentation Updates
- Update module README after Phase 1 (optimistic reads reintroduced) and Phase 3 (epoch reclamation).
- Add a section “Concurrency Model” describing epochs, migration, reclamation.

---
## Risk Mitigation
- Each phase gated by stress tests under ASAN & TSAN before merging.
- Introduce compile-time flags for experimental features; default off.
- Keep rollback points (git tags) per phase.

---
## Future Extensions (Beyond Plan)
- `EraseIf(predicate)` bulk removal with cooperative cleanup.
- Lock-free iteration snapshot (versioned array of pointers) for read-mostly workloads.
- Memory footprint reduction via key/value inline compression traits.

---
## Review Checklist (Per Phase PR)
- Motivation clearly stated.
- Invariants listed (e.g., migratedGroups == oldGroupCount before finalize).
- Diff minimal (no drive-by refactors).
- Tests: positive + negative + edge coverage.
- Benchmarks re-run (attach summary numbers).
- Sanitizer logs clean.

---
## Immediate Next Action
Apply Phase 0 correction (remove finalized guard in `MigrateGroup`, add debug assert) and create deterministic key coverage test.

---
## Debug Assert Examples (to be added in code)
```cpp
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
    if (state->finalized.load(std::memory_order_acquire)) {
        if (state->migratedGroups.load(std::memory_order_relaxed) != state->oldGroupCount) {
            std::terminate(); // invariant violation: finalized without full migration
        }
    }
#endif
```

---
End of Plan.
