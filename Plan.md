# Implementation Plan: ConcurrentHashMap Reinvention

## Vision & Success Criteria

- Deliver a header-only `NGIN::Containers::ConcurrentHashMap` that meets the performance, scalability, and safety targets outlined in the technical requirements.
- Achieve linearizable `Insert`, `Upsert`, `Find`, `Contains`, and `Remove` operations without external locks while supporting custom allocators that model `NGIN::Memory::AllocatorConcept`.
- Match or surpass throughput of Folly F14 concurrent maps and Intel TBB concurrent hash map under mixed read/write workloads on 64-bit Windows and Unix.
- Provide maintainable, well-documented internals ready for further evolution (RCU variants, instrumentation toggles) without breaking ABI/ODR guarantees.

## Current Status (2025-02-14)

- Implemented cooperative resizing with migration descriptors; mutating threads assist bucket movement without stop-the-world coordination.
- Slot-level CAS protocol covers inserts, upserts, erases, and read paths with dual-table lookups while migration is active.
- Stress suite exercises resize storms and insert/remove churn; benchmarks highlight remaining scaling gaps vs. TBB at high thread counts.
- Outstanding: integrate epoch-based reclamation for deferred group release, add instrumentation toggles, and broaden benchmarks once reclamation lands.

## Constraints & Non-Negotiables

- Header-only core under `include/NGIN/Containers`; implementation resides in `ConcurrentHashMap.hpp` and supporting detail headers if required.
- C++23 language features only; avoid non-portable intrinsics. Depend solely on the standard library plus existing NGIN facilities (hashing, memory, utilities).
- No global state, no anonymous namespaces in public headers. All helpers live in `NGIN::Containers::detail`.
- All allocations route through the provided allocator instance; no direct `new`/`delete`.
- Operations marked `noexcept` only when enforced by invariants; throw only standard exceptions.

## Architecture Outline

### Table Layout

- Adopt segmented open addressing:
  - Table split into power-of-two bucket groups sized to cache lines (e.g., 16 entries aligned to 64 bytes) to preserve locality and reduce false sharing.
  - Each entry stores control byte (state machine), key, value pointer/reference wrapper. Control bytes encode `EMPTY`, `PENDING_INSERT`, `OCCUPIED`, `TOMBSTONE`, using `std::uint8_t` atomics.
  - Robin Hood style probing with fingerprinting to reduce variance and bound probe length.

### Concurrency Model

- `Find`/`Contains` use wait-free optimistic reads with snapshot of control bytes via `std::atomic<std::uint8_t>` using acquire semantics.
- `Insert`/`Upsert` leverage CAS on control bytes to claim slots; employ per-bucket lightweight spinlock (single-flag CAS) only when multiple writers collide in same bucket to guarantee progress.
- `Remove` marks tombstones via atomic exchange and publishes removal epoch for safe reclamation.
- Sharded metadata (size counters, rehash state) aligned to cache lines to avoid contention.

### Memory Reclamation

- Integrate epoch-based reclamation service:
  - Provide lightweight `EpochHandle` inside map; threads call `EnterEpoch`/`LeaveEpoch` via RAII guard when mutating.
  - Defers node deallocation (for closed addressing fallback) and deferred value destruction until all earlier epochs are quiescent.
- Offer policy hook (`detail::ReclamationPolicy`) so future hazard-pointer strategy can be swapped without redesign.

### Resizing Strategy

- Maintain capacity metadata as power-of-two bucket count; expand when load factor surpasses configurable threshold (~0.75).
- Resize proceeds in cooperative phases:
  1. Promote new bucket array using allocator (double capacity, allocate aligned storage).
  2. Flag resize state globally (atomic pointer to migration descriptor).
  3. Mutating threads assist by migrating a bounded number of bucket groups before continuing their operation (helping strategy).
  4. Readers detect migration descriptor and consult both tables until migration complete.
- Use phased tombstone reclamation: when load < shrink threshold and tombstones dominate, trigger compaction to reduce probe chains.

### API Surface (initial)

- Core type: `template<class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>, class Alloc = Memory::SystemAllocator>`
- Operations:
  - `Insert(const Key&, const Value&)`, `Insert(Key&&, Value&&)` returning `bool` success flag.
  - `Upsert` with callable to merge existing value or emplace new.
  - `Find(const Key&) const` returning `std::optional<Value>` or callback-based accessor to avoid copies.
  - `Contains(const Key&) const noexcept`.
  - `Remove(const Key&)` returning `bool` for removal success.
  - `Size() const noexcept` (approximate), `ExactSize()` requiring quiescent point if needed.
  - `Clear()`, `Reserve(size_t)`, `LoadFactor()`, `BucketCount()` for observability.
- Iteration: supply safe traversal via `ForEach(function)` that grabs consistent snapshot of control bytes; no raw iterators initially.

### Instrumentation & Debug Hooks

- Compile-time switch via template parameter or constexpr bool to enable debug assertions (probe depth, epoch misuse).
- Optional stats struct capturing collision counts, resize assists, and tombstone density for benchmarking.

## Workstreams & Milestones

1. **Foundational Design & Scaffolding (Week 1)**
   - Document public API in header skeleton with doxygen comments; outline `detail` structures (control byte enum, bucket type, metadata blocks).
   - Integrate allocator concept checks and `static_assert` guards.
   - Provide stubbed epoch manager interface ready for implementation.

2. **Core Table Mechanics (Week 2)**
   - Implement bucket group layout, control byte transitions, and probing strategy in isolation (single-threaded unit tests).
   - Add robin-hood insertion logic and tombstone handling without concurrency yet.

3. **Concurrency Layer (Weeks 3-4)**
   - Introduce atomic control bytes, per-slot CAS claims, and optimistic read path.
   - Implement writer backoff/spin behavior with exponential backoff under contention.
   - Ensure memory order contracts documented inline (acquire/release semantics per transition).
   - Develop epoch-based reclamation (per-thread handle pool, retire lists) and integrate with erase path.

4. **Resizing & Sharding (Weeks 5-6)**
   - Implement migration descriptor, cooperative resize loops, and dual-table lookup bridging.
   - Validate correctness under concurrent resizes via stress tests.
   - Tune shard count heuristics (based on CPU core count or user override) and align metadata to cache line boundaries.

5. **Public API Completion & Polishing (Week 7)**
   - Finalize `Insert`, `Upsert`, `Find`, `Contains`, `Remove`, `Clear`, `Reserve`, `ForEach` with allocator-aware overloads.
   - Ensure exception paths roll back control bytes and retire pending allocations safely.
   - Add instrumentation toggles and debug assertions.

6. **Testing & Verification (Weeks 2-8 overlapping)**
   - Unit tests (Catch2) for single-thread semantics, allocator propagation, load factor limits, tombstone reuse.
   - Concurrency tests using stress harness (multi-threaded inserts/reads/removes, resize storm scenarios, epoch retirement validation).
   - Negative tests for erroneous usage (double remove, mutation during callback) ensuring asserts/exceptions fire.
   - Performance benchmarks comparing against Folly/TBB baselines; include `benchmarks/Containers/ConcurrentHashMapBench.cpp`.
   - Static analysis (`clang-tidy`) and sanitizers (ASan/TSan) on representative builds.

7. **Documentation & Integration (Week 8)**
   - Update `include/NGIN/NGIN.hpp` to export the new header and document in `include/NGIN/Containers/README.md`.
   - Provide design notes and usage examples in component README.
   - Add migration guidance outlining differences from legacy attempt.

## Dependencies & Open Questions

- Confirm availability or need to implement shared epoch manager (reuse existing facility if present in repo?).
- Decide whether to expose snapshot iterator in v1 or delay until post-MVP.
- Determine default shard count heuristics (fixed power-of-two vs. dynamic per hardware concurrency) and provide overrides.
- Validate allocator traits support for aligned allocations required by bucket groups; add adapter if allocator lacks `allocate_bytes`.

## Risk Mitigation

- Early micro-benchmarks to validate probing strategy before full concurrency layer.
- Keep concurrency modeling encapsulated; provide compile-time switch to fallback to sharded-lock variant for debugging.
- Thorough assertions in debug builds for epoch discipline and control byte transitions to catch logic bugs before release builds.

## Verification Checklist

- [ ] Positive tests for each operation under single-thread and multi-thread scenarios.
- [ ] Negative tests covering contention, invalid usage, and exception safety.
- [ ] Benchmarks demonstrating parity or improvement vs. Folly/TBB baselines.
- [ ] clang-format and clang-tidy clean.
- [ ] Sanitizer runs (ASan, TSAN) with no reported issues.
