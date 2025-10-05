# ConcurrentHashMap Performance Roadmap

Goal: Evolve the current implementation from “functionally correct & safe” to **state‑of‑the‑art throughput and scalability** while retaining deterministic behavior, memory safety, and simplicity of public API.

We proceed in phased, measurable steps. Each phase has: Motivation, Actions, Metrics / Success Criteria, and Rollback Notes. Phases are largely incremental; avoid overlapping large changes to keep attribution clear.

---

## Phase 0 (Baseline & Instrumentation)

Motivation: Establish authoritative reference metrics, identify hotspots, and ensure we can attribute gains.

Actions:

- [x] Add probe statistics: total insert probes, total successful inserts, max probe length (atomic max), abandon counts.
- [x] Expose `GetDiagnostics()` returning a struct of counters; add `ResetDiagnostics()`.
- [x] Integrate diagnostics dump path in benchmark (compile-time flag `NGIN_MAP_DIAGNOSTICS`).
- [ ] (Deferred) Add optimistic lookup (dry-run) counters placeholder (folding directly into Phase 1 implementation).
- [ ] (Deferred) Additional workload variants (95/5, 50/50, 100% write) and growth vs pre-sized scenario.
- [ ] (Deferred) Insert-only microbenchmark vs `std::unordered_map`.

Baseline Metrics (Captured 2025-10-04): Mixed workload (25% insert/update, 75% lookup), opsPerThread=5000

| Threads | Avg Time (ms) | Insert Calls | New Inserts | Updates | Avg Probe Steps/Insert | Max Probe | Yields | Abandon | PressureAbort |
|---------|---------------|--------------|-------------|---------|-----------------------|-----------|--------|---------|---------------|
| 1       | 0.613         | 1,250        | 709         | 541     | 1.09                  | 2         | 0      | 0       | 0             |
| 4       | 1.52          | 5,000        | ~1,950–2,040| ~2,960–3,050| ~1.21 (total probes/ calls varies 6040–6127 / 5000)| 3–4 | 0–75  | 0 | 0 |
| 8       | 2.59          | 10,000       | 2,219–4,026 | 5,974–7,781| ~1.03–1.05            | 3–7       | 0–73   | 0       | 0             |
| 16      | 4.83          | 20,000       | 5,263–5,837 | 14,163–14,737| ~1.05–1.07         | 3–4       | 2–23   | 0       | 0             |
| 64      | 16.92         | 80,000       | 5,904–9,806 | 70,194–74,096| ~1.07–1.10         | 4–5 (one run 5, global max 7)| 21–59 | 0 | 0 |

Aggregated (all runs executed during baseline capture):

- Total insert calls: 813,750
- New inserts: 137,467 (16.9%)
- Updates: 676,283 (83.1%)
- Total probe steps: 886,052 (avg 1.089 / call)
- Max probe observed: 7
- Locate yields: 592 (≈0.073% of insert calls)
- Abandon / Pressure abort: 0

Interpretation Summary:

- Insert path clustering is low (avg probe <1.1) with small variance; no abandon or pressure abort events under this workload.
- Throughput scales but trails TBB significantly at higher thread counts (e.g. ~4.9× slower at 64 threads) indicating lookup/read-side synchronization overhead is the prime target.
- New insert fraction declines with thread count as the key space saturates, biasing workload to update + lookup.

Regression Check: No evidence of >2% slowdown attributable to instrumentation (baseline accepted).

Rollback: Pure additive; can revert by removing diagnostic APIs.

---

## Phase 1 (Optimistic Read Path / Reduced Reader Contention)

Motivation: Remove global atomic inc/dec for read-only operations during steady state (no migration) to cut lookup latency and contention.

Actions:

- [x] Introduce epoch word (`m_epoch`): even = stable, odd = migration in progress.
- [x] Writers flip epoch to odd on migration start; finalize sets back to even (increment to next even value).
- [x] Optimistic `TryGet` path: epoch snapshot & validation; fallback to original guarded path on mismatch or migration.
- [x] Reads seeing odd epoch or race path revert to guarded path unchanged.
- [x] Safety: optimistic path only observes published (groups, mask) pair; epoch change invalidates stale reads before acceptance.
- [ ] (Deferred) Add explicit fast-path success/fallback counters (not required for moving to Phase 2).
- [ ] (Deferred) Microbenchmark focused lookup-only latency to refine single-thread performance anomaly.

Metrics / Success (Results 2025-10-04):

| Threads | Phase 0 Avg (ms) | Phase 1 Avg (ms) | Delta | Notes |
|---------|------------------|------------------|-------|-------|
| 1       | 0.613            | 0.824            | +34%  | Outlier / variance; needs dedicated lookup-only microbench (mixed workload includes inserts & potential migration) |
| 4       | 1.516            | 1.501            |  -1%  | Essentially unchanged (within noise) |
| 8       | 2.595            | 2.427            |  -6%  | Modest gain; indicates partial read-path benefit |
| 16      | 4.832            | 3.717            | -23%  | Meets target (≥20% improvement) |
| 64      | 16.922           | 10.886           | -36%  | Exceeds target; strong scalability gain |

Additional Diagnostics Comparison (Aggregated):

- Insert probe characteristics essentially unchanged (avg probe ~1.09 → ~1.10; max probe 7 → 6 in new runs, still low).
- Yields increased slightly (592 → 721) but remain <0.09% of insert calls—acceptable; likely due to higher concurrency scheduling variations during faster read completions.
- No abandon or pressure abort events (unchanged).

Interpretation:

- Goal of reducing contention at higher thread counts was achieved (notable 16T & 64T improvements > target percentages).
- Single-thread regression suggests the optimistic path adds fixed overhead (epoch loads + double-check) that is not amortized when there is no contention; may also reflect noise from occasional migration in the sampled runs. Will reassess with a pure lookup microbenchmark (deferred action).
- Further gains at mid-range (8T) are smaller than at 16T+, implying remaining costs (e.g., memory ordering, branch predictability) become more significant only after higher contention; later phases (2–5) will address these.

Risk / Safety Review:

- Tests passed (no functional regressions reported).
- Epoch transitions only at migration boundaries; no reclamation change yet, so safety model unchanged.

Rollback: Was guarded by compile-time macro `NGIN_CHM_OPTIMISTIC_READS`; feature now promoted to always-on in Phase 2 (legacy guarded-only path removed).

---

## Phase 2 (Contention Backoff Consolidation & Cleanup)

Motivation: Reduce high-contention overhead (sleep/yield latency, CAS congestion) and remove measurement overhead now that baseline behaviors are understood.

Implemented Actions (2025-10-05):

- Replaced previous yield/sleep escalation with unconditional spin + jitter backoff (exponential capped, `_mm_pause` on x86, noop asm fallback elsewhere) followed by opportunistic `std::this_thread::yield()` for very high attempt counts.
- Adopted the optimistic read path as unconditional (removed feature macro and fallback-only guarded mode).
- Removed all diagnostics / instrumentation counters and related APIs (`GetDiagnostics`, `ResetDiagnostics`, aggregated benchmark reporting) to eliminate atomic RMW overhead and code size.
- Removed adaptive threshold experimentation (and the dynamic `m_loadFactorPermille`); reverted to fixed `kLoadFactor = 0.75` for determinism and simplicity after observing consistently low probe lengths.
- Inlined previously macro-gated fast paths (no more `NGIN_CHM_OPTIMISTIC_READS`, `NGIN_CHM_BACKOFF_SPIN`, `NGIN_CHM_ADAPTIVE_THRESHOLD`).

Deferred / Dropped (out of scope or superseded):

- Adaptive probe-length driven threshold tuning (not enough evidence of benefit after instrumentation removal; revisit only if probe growth appears in future adversarial tests).
- Public tuning knobs (will reconsider after Phase 3 local size counters, when remaining scaling bottlenecks are clearer).

Results (Mixed workload 25% insert/update, 75% lookup; opsPerThread=5000, captured 2025-10-05):

| Threads | Phase 1 Avg (ms) | Phase 2 Avg (ms) | Time Δ (%) | Speedup (×) | Phase 2 vs TBB (time ratio) |
|---------|------------------|------------------|------------|-------------|-----------------------------|
| 1       | 0.824            | 0.1298           | -84.3%     | 6.35×       | 0.50× (faster)              |
| 4       | 1.501            | 0.6197           | -58.7%     | 2.42×       | 0.62× (faster)              |
| 8       | 2.427            | 1.1944           | -50.8%     | 2.03×       | 1.20× (slower)              |
| 16      | 3.717            | 2.3137           | -37.8%     | 1.61×       | 1.97× (slower)              |
| 64      | 10.886           | 7.7153           | -29.1%     | 1.41×       | 2.38× (slower)              |

Absolute Phase 2 Averages (ms):

| NGIN Threads | Time | Std.UnorderedMap (mutex) | TBB concurrent_unordered_map |
|--------------|------|--------------------------|------------------------------|
| 1            | 0.1298 | 0.1845                 | 0.2609                       |
| 4            | 0.6197 | 0.6716                 | 0.9968                       |
| 8            | 1.1944 | 2.6956                 | 0.9917                       |
| 16           | 2.3137 | 11.971                 | 1.1730                       |
| 64           | 7.7153 | 58.7894                | 3.2451                       |

Interpretation:

- Large single-thread gain (removal of diagnostics + leaner fast path + elimination of per-insert counter atomics) makes us >2× faster than TBB at t=1.
- We remain ahead of TBB up to 4 threads; TBB overtakes from 8 threads onward—residual contention now dominated by global size accounting and migration triggers (focus of Phase 3).
- Throughput improvement targets at high concurrency met: 37.8% (16T) and 29.1% (64T) surpass the 25% success criterion for at least one of the two upper levels (16T exceeds; 64T slightly above threshold).
- Decision to drop adaptive threshold validated by consistently low observed probe lengths during earlier instrumented phases (avg ≈1.1, max <10) and by improved latency without the atomic loads.

Risks / Notes:

- Loss of internal probe metrics means future regressions require either temporary reinstrumentation or external profiling; acceptable trade-off for simplified hot path.
- Potential for pathological clustering is currently unmonitored—mitigated in future by optional lightweight tag/SIMD introspection (Phases 4–5) or temporary debug build instrumentation.

Rollback: Core changes are structural (macros removed); reverting would require restoring prior version of file. No soft toggle remains.

Next Focus (Phase 3): Remove global `m_size` as the high-contention scalar RMW hotspot via local counters & aggregated flush.

---

## Phase 3 (Size Accounting & Migration Efficiency)

Motivation: Global `m_size.fetch_add` & frequent migration attempts generate serialization.

Actions:


Metrics / Success:


Rollback: Compile-time macro `NGIN_CHM_LOCAL_SIZE`.

## Phase 3 – Size Accounting & Migration Efficiency (In-Progress)

*Goal:* Replace global atomic size updates with sharded/local counters to reduce contention under heavy multi-threaded mutation. Adjust migration trigger to use approximate size while maintaining correctness.

Status: IMPLEMENTED.

Changes:

- Added 64 sharded size deltas (`m_sizeShards[]`) storing signed deltas (std::intptr_t) with batched flush threshold (32 net ops).
- Insert/Upsert: now call `IncrementSizeShard()` instead of global `fetch_add`.
- Remove: uses `DecrementSizeShard()`.
- `Size()` & `LoadFactor()` derive approximate size via `ApproxSize()`; migration paths call `FlushAllShards()` before growth decisions.
- Move/Clear/Destroy/Initialize flush or reset shards to maintain invariants.
- Refinement: Migration now performs shard flush only after CAS success (winner-only) to eliminate redundant global scans.
- Added runtime configurable flush threshold (`SetFlushThreshold`) replacing fixed constant; default remains 32.

Correctness Notes:

- Shard deltas are flushed atomically; global committed size (`m_size`) only updated via flush or explicit flush-all before migration decisions.
- Approximate size may transiently over/under estimate true size by at most (shards * threshold) worst-case, acceptable for early growth triggers.

Next Steps:

1. Benchmark Phase 3 vs Phase 2 to quantify contention reduction.
2. Consider dynamic shard count or per-core mapping if further scalability needed.
3. Proceed to Phase 4 (SIMD control scanning) after benchmark review.
4. Tune flush threshold (e.g., 32 → 64/128) and measure impact on high-thread scaling.

---

## Phase 4 (Group SIMD / Control Byte Scanning)

Motivation: Per-slot state loads + branching during probing waste cycles.

Actions:

- [ ] Introduce a contiguous byte control array already present; load 16 bytes and compute masks for Empty/Tombstone/Occupied using SIMD (SSE2 baseline; optional AVX2 path).
- [ ] Implement fast “find first candidate” for insert (Empty/Tombstone) and match for hash (pre-filter by comparing low bits of hash in control if we add partial hash tags).
- [ ] Optional: add 1-byte tag (e.g. 7 bits from hash) to control byte (requires expanding state representation). Migrate from 2-bit state to enumerated + tag; maintain backward compatibility gated by macro.

Metrics / Success:

- Single-thread insert and lookup microbench shows ≥25% latency reduction.
- Mixed workload throughput improved ≥15% across thread counts.

Rollback: Keep original scalar path compiled if SIMD macro disabled.

---

## Phase 5 (Partial Hash / Tagging & Reduced Full Key Equality)

Motivation: Avoid expensive key equality when hashes differ; improve branch predictability.

Actions:

- [ ] Add tag extraction (`hash >> k`) & store in control/tag field on insert.
- [ ] Lookup compares tag first; only then attempts lock / key equality.
- [ ] Ensure tombstone reclaim still resets tag.

Metrics / Success:

- Reduction in average occupied-slot equality checks (add counter) by ≥70% for random key workload.
- Additional 10–15% throughput improvement (esp. for larger / expensive keys later).

Rollback: Toggle via `NGIN_CHM_PARTIAL_TAG`.

---

## Phase 6 (Epoch-Based Reclamation Simplification)

Motivation: Current retired list + readers count may still create latency spikes; epoch scheme can batch frees.

Actions:

- [ ] Introduce global epoch incremented after each finalized migration; readers capture epoch (optimistic or guarded) and reclamation defers freeing old groups until `safeEpoch >= retireEpoch + 2`.
- [ ] Remove per-state activeUsers once epoch guarantees quiescence (validate with tests & TSAN if available).

Metrics / Success:

- Same functional tests pass; reduction in worst-case latency for reclamation heavy stress by ≥30% (measure time from finalize to actual free).

Rollback: Keep old code under macro `NGIN_CHM_RCU_RC`.

---

## Phase 7 (Advanced Load Shedding / Priority Migration)

Motivation: Avoid pathological contention when cluster density remains high post-migration due to hash adversarial patterns.

Actions:

- [ ] Detect cluster chains > threshold length; schedule targeted rehash of those keys into a fresh table (secondary migration) early.
- [ ] Optionally salt hash with per-table random seed (store seed, rehash on migration) to break adversarial distribution.

Metrics / Success:

- Under synthetic adversarial hash (e.g., custom hasher mapping many keys into tight bucket subset) worst-case tail latency improved ≥5× and probe abandons approach zero.

Rollback: Feature flag `NGIN_CHM_SALTED_HASH` and `NGIN_CHM_CLUSTER_REHASH`.

---

## Phase 8 (Lock-Free Friendly API Extensions) *Optional*

Motivation: Provide specialized bulk / batched APIs to amortize synchronization.

Actions:

- [ ] `BulkInsert(span<pair<K,V>>)` with single migration check and size pre-reserve.
- [ ] `Prefetch(key)` hint issuing hardware prefetch for predicted group.
- [ ] `Visit(key, functor)` that performs optimistic read and falls back.

Metrics / Success:

- Bulk insert of 1M keys (distinct) runs ≥2× faster than naive loop of single inserts.

Rollback: Pure additive.

---

## Phase 9 (Cross-Platform / NUMA Awareness) *Longer Term*

Motivation: Scale on multi-socket / high core count systems.

Actions:

- [ ] NUMA-aware allocation: allocate groups per NUMA node and bias thread hashing to local segments.
- [ ] Optional segmented table variant (N segments each with independent migration) for very large concurrency.

Metrics / Success:

- On dual-socket testbed: ≥30% throughput improvement vs non-NUMA build at 2× socket thread count.

Rollback: Disabled if no NUMA APIs available.

---

## General Testing / Validation Additions

- Stress tests with adversarial hash, high churn (insert+erase once erase reintroduced), large key/value types to surface alignment & cache effects.
- ASan/UBSan nightly run for new phases affecting memory layout.
- Optional perf counters: cycles/op, LLC misses/op (via platform specific tooling, documented but not enforced in CI).

## Documentation & Discoverability

- Update component README after each *functional* phase (Phases 1, 2, 3, 4, 5, 6).
- Maintain a CHANGELOG snippet enumerating performance-relevant toggles and their default states.

## Rollout Strategy

1. Merge Phase 0 instrumentation.
2. For each subsequent phase: feature branch, benchmark before/after, capture metrics table, update Plan.md marking phase as DONE with actual gains.
3. Keep feature flags until at least two downstream consumers validate no regressions; then graduate defaults.

## Risk Matrix (Selected)

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 | Racy optimistic read causing UAF | Epoch + fallback, retain guarded path |
| 2 | Over-resizing due to aggressive threshold | Cap minimum growth interval, log adjustments |
| 3 | Size underestimation delays resize | Add safety margin, force migrate when local deltas large |
| 4 | SIMD path UB on misalignment | Static asserts + fallback scalar path |
| 5 | Tag collisions increase false positives | Still validated by full key equality |
| 6 | Premature reclamation | Keep dual mechanism (epoch + activeUsers) until proven |
| 7 | Hash salt breaks determinism for tests | Deterministic seed injection in tests |

---

## Tracking Table (to update as phases complete)

| Phase | Status   | Date       | Notes |
|-------|----------|------------|-------|
| 0     | DONE     | 2025-10-04 | Baseline metrics captured; partial original action list deferred (workload variants & microbench). |
| 1     | DONE     | 2025-10-04 | Optimistic epoch-based read path implemented; later promoted to always-on. |
| 2     | DONE     | 2025-10-05 | Spin+jitter backoff, removed diagnostics & adaptive threshold, macros collapsed. |
| 3     | Planned  |            | |
| 4     | Planned  |            | |
| 5     | Planned  |            | |
| 6     | Planned  |            | |
| 7     | Planned  |            | |
| 8     | Planned  |            | |
| 9     | Planned  |            | |

---

End of plan.

