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

Rollback: Controlled by compile-time macro `NGIN_CHM_OPTIMISTIC_READS` (remove or undef to restore legacy guarded-only path).

---

## Phase 2 (Insert Contention Backoff & Adaptive Threshold)

Motivation: High contention currently inflates probe lengths and causes sleep/yield overhead.

Actions:

- [ ] Replace sleep-based backoff with: N (e.g. 64) `_mm_pause` spins → yield after budget; exponential but capped.
- [ ] Add a per-thread random jitter (xorshift) into pause loop counts to de-phase threads.
- [ ] Adaptive resize trigger: track moving average probe length; if avg > target (e.g. 4) shrink load factor threshold (0.75→0.60) for next migration.
- [ ] Publish configuration knobs (maybe template policy struct or runtime tuning API for tests).

Metrics / Success:

- Probe abandon counter decreases ≥50% at 64 threads vs Phase 1.
- 16/64-thread mixed throughput improves ≥25%.
- Insert-only throughput does not regress >5% at low thread counts (1–2 threads).

Rollback: Guard adaptive logic behind `NGIN_CHM_ADAPTIVE_THRESHOLD`.

---

## Phase 3 (Size Accounting & Migration Efficiency)

Motivation: Global `m_size.fetch_add` & frequent migration attempts generate serialization.

Actions:

- [ ] Introduce per-thread (or per-core) `LocalCounters` with `pendingInserts` (power-of-two sized TLS array hashed by thread id) flushed when reaching flushThreshold (e.g. 32) or before migration attempts.
- [ ] `Size()` computes `globalSize + sum(pending)` lazily for rare full counts; primary path uses approximate size (document semantics: may lag by < flushThreshold * threads).
- [ ] Use approximate size for migration trigger but add headroom (projected + safety margin).
- [ ] Opportunistic group migration batching: when a thread holds a migration state user slot, process up to K groups or until local fairness budget.

Metrics / Success:

- Reduction in `m_size` atomic RMW rate (measure via added counter) by ≥90% under high concurrency.
- Further 10–20% throughput gain at 32–64 threads mixed workload.

Rollback: Compile-time macro `NGIN_CHM_LOCAL_SIZE`.

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
| 1     | Planned  | 2025-10-04 | Proceed: optimistic read epoch path. |
| 2     | Planned  |            | |
| 3     | Planned  |            | |
| 4     | Planned  |            | |
| 5     | Planned  |            | |
| 6     | Planned  |            | |
| 7     | Planned  |            | |
| 8     | Planned  |            | |
| 9     | Planned  |            | |

---

End of plan.

