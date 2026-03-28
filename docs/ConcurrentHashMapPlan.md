# ConcurrentHashMap Rewrite Plan

## Goal

Replace the current `ConcurrentHashMap` with a new family built on one shared core and a pluggable reclamation layer:

- `ConcurrentHashMap<..., ReclamationPolicy::ManualQuiesce>` for ECS and frame-based usage
- `ConcurrentHashMap<..., ReclamationPolicy::HazardPointers>` for the safest automatic reclamation path
- `ConcurrentHashMap<..., ReclamationPolicy::LocalEpoch>` as the likely default

The design rule is:

**Reclamation is policy-pluggable, but the map algorithm is not.**

This should remain one concurrent map architecture with interchangeable reclamation behavior, not three separate map implementations.

## 1. Freeze The Architecture

The rewrite should use:

- sharded concurrent map
- per-shard writer serialization
- lock-free reads
- immutable published table for readers
- table-swap resize
- node-based buckets
- policy-driven reclamation

That means:

- the top-level map is split into shards
- each shard owns its own published `Table*`
- readers pin or protect the shard table through the selected reclamation policy
- writers mutate through a shard-local lock
- resize builds a new shard table, publishes it atomically, and retires the old one

This keeps the algorithm simple and makes reclamation replaceable.

The following are explicitly rejected:

- concurrent open addressing
- slot state machines
- per-slot raw object lifetime management
- dual-table searching during migration
- cooperative migration in normal operations
- global background reclamation thread

## 2. Public API

Initial template surface:

```cpp
template<
    class Key,
    class Value,
    class Hash = std::hash<Key>,
    class Equal = std::equal_to<Key>,
    class Allocator = Memory::SystemAllocator,
    ReclamationPolicy Policy = ReclamationPolicy::LocalEpoch,
    std::size_t ShardCount = 64>
class ConcurrentHashMap;
```

Initial operations for v1:

- constructor and destructor
- `Insert`
- `InsertOrAssign`
- `Upsert`
- `Remove`
- `Contains`
- `TryGet`
- `GetOptional`
- `Clear`
- `Reserve`
- `Size`
- `Empty`
- `Capacity`
- `ForEach` or `SnapshotForEach`
- `Quiesce` where supported

Do not provide initially:

- STL-style iterators
- returned references or pointers to internal values
- `operator[]`
- mutation through returned references
- move operations while concurrent access may exist
- lock-free erase without shard writer lock
- stable iterators

## 3. Reclamation Policy Abstraction

Introduce:

```cpp
enum class ReclamationPolicy
{
    ManualQuiesce,
    HazardPointers,
    LocalEpoch
};
```

The core map should depend only on a narrow reclaimer contract. A policy implementation should provide the equivalent of:

- `ReadGuard Enter()`
- `T* Protect(std::atomic<T*>& ptr, ReadGuard&)`
- `void Retire(T* object, Deleter)`
- `void Poll()`
- `void Quiesce()`
- `void Drain()`

The map code should not know whether the implementation is based on manual quiescence, epochs, or hazard pointers.

## 4. Internal Data Model

Top-level map shape:

```cpp
template<...>
class ConcurrentHashMap
{
    struct Shard;
    Shard m_shards[ShardCount];
    Hash m_hash;
    Equal m_equal;
    Allocator m_allocator;
    std::atomic<size_t> m_size;
};
```

Shard shape:

```cpp
struct Shard
{
    alignas(64) SpinMutex writeLock;
    std::atomic<Table*> table;
    Reclaimer<Table, Policy> tableReclaimer;
    Reclaimer<Node, Policy> nodeReclaimer;
};
```

Table shape:

```cpp
struct Table
{
    size_t bucketCount;
    Bucket* buckets;
    size_t size;
};
```

Bucket shape:

```cpp
struct Bucket
{
    std::atomic<Node*> head;
};
```

Node shape:

```cpp
struct Node
{
    size_t hash;
    Node* next;
    Key key;
    Value value;
};
```

`Node::next` should remain a plain pointer while writes are serialized under the shard lock and readers see immutable published structures.

## 5. Concurrency Model

Reads:

- lock-free
- pin or protect the current shard table through the reclamation policy
- traverse bucket chains without taking the writer lock
- copy values out before guard release

Writes:

- take a shard-local writer lock
- load the current table
- mutate through shard-local update logic
- release the lock
- retire removed nodes and old tables through the selected policy

Resize:

- shard-local only
- writer lock held
- allocate a new table
- rehash live nodes into the new table
- atomically publish the new table
- retire the old table

## 6. Implementation Phases

### Phase 1: Single-threaded shard table core

Deliverables:

- `Node`
- `Bucket`
- `Table`
- bucket indexing
- insert, find, remove, and upsert
- resize
- allocator integration
- destroy and clear

Goal:

A correct chained hash table that can later sit behind each shard.

### Phase 2: Sharding and shard-local writer locking

Deliverables:

- shard hashing
- shard table ownership
- shard-local spin mutex
- top-level API delegating to shards
- size accounting
- reserve distribution across shards

Goal:

A correct concurrent baseline before lock-free reads are finalized.

### Phase 3: Policy abstraction

Deliverables:

- `ReclamationPolicy`
- reclaimer traits or classes
- `ReadGuard`
- `Retire`
- `Poll`
- `Drain`

Goal:

Make the core map depend only on the reclaimer contract.

### Phase 4: `ManualQuiesce`

Semantics:

- readers do not register globally
- retired nodes and tables accumulate on shard retire lists
- memory is reclaimed only on `Quiesce()`, explicit collection, `Clear()`, or destruction

Important rule:

The caller must guarantee there are no concurrent readers across `Quiesce()`.

Deliverables:

- per-shard retire list
- retire record type with deleter
- `Quiesce()` implementation
- debug assertions for misuse
- documentation stating memory may grow until quiescence

### Phase 5: `LocalEpoch`

Semantics:

- each map instance owns its own epoch domain
- readers enter and exit that local domain
- retired objects are stamped with retire epochs
- reclamation happens opportunistically during writes, reserve, clear, and destruction

Deliverables:

- thread participant registration
- RAII read guard
- retire queues
- epoch advancement policy
- reclamation scan
- destructor drain

### Phase 6: `HazardPointers`

Semantics:

- readers acquire hazard slots
- readers protect published pointers during traversal
- writers retire old nodes and tables
- reclamation scans active hazards before freeing objects

Important note:

If the reader-visible structure remains immutable after publish, table-level protection may be enough and node-level hazards can be minimized.

Deliverables:

- hazard record pool or thread-local hazard owner
- protected pointer acquisition loops
- retired list scans against active hazards
- opportunistic reclamation thresholds

## 7. Core Algorithm Guidance

The core should be shaped to minimize reclamation complexity.

Preferred reader model:

- readers traverse immutable published table versions

The implementation may start with:

- mutable updates under shard writer lock
- readers protected by a pinned table version
- deferred reclamation of removed nodes and old tables

This favors correctness and keeps all policies working against the same core.

Avoid reintroducing the old slot-state and migration architecture.

## 8. Write Strategy

Two write strategies exist:

- in-place bucket mutation
- versioned bucket or table replacement

Recommended approach:

- shard write lock for writer serialization
- immutable published structure for readers
- copy affected bucket chain on writes where needed
- full table replacement for resize

This is safer and easier to reason about than the previous open-addressed cooperative migration design.

## 9. Policy Rollout Order

Implement in this order:

1. `ManualQuiesce`
2. `LocalEpoch`
3. `HazardPointers`

Rationale:

- `ManualQuiesce` proves the new architecture with minimal reclamation complexity
- `LocalEpoch` is the likely default general-purpose policy
- `HazardPointers` is the hardest and should come after the core is stable

## 10. Testing Requirements

Functional tests:

- insert, find, remove, and upsert
- duplicate insert
- reserve and resize
- clear
- destruction
- allowed move and copy policy behavior

Concurrency tests:

- many-reader one-writer
- many-writer multi-shard
- same-key contention
- erase and insert churn
- reserve while reads are active
- clear under documented conditions
- repeated construct and destroy

Reclamation tests:

For `ManualQuiesce`:

- retired memory not reclaimed before `Quiesce`
- reclaimed after `Quiesce`

For `LocalEpoch`:

- old versions survive while readers hold guards
- reclaimed after reader exit and epoch advancement

For `HazardPointers`:

- protected objects are never reclaimed
- retired objects reclaim after hazard release

Stress and tooling:

- randomized operation streams
- long-running soak
- forced collision scenarios
- throwing key and value types
- TSAN, ASAN, and UBSAN

## 11. Documentation Requirements

Each policy must document:

### `ManualQuiesce`

- fastest path for ECS-style phase-based systems
- memory reclamation deferred until `Quiesce`
- safe when the caller has explicit synchronization points

### `LocalEpoch`

- automatic reclamation
- no global manager or background thread
- lock-free reads
- per-container participant registration

### `HazardPointers`

- automatic and explicit safety
- no dependency on external quiescent phases
- highest read-side overhead

Shared behavior across all policies should also be documented:

- readers receive copied values, not borrowed references
- writers serialize per shard
- iteration semantics
- reserve and clear semantics
- destruction requirements

## 12. Milestones

### Milestone A

- finalize API
- finalize shard, table, and node layout
- finalize policy abstraction
- document invariants

### Milestone B

- implement single-threaded core
- allocator integration
- resize
- unit tests

### Milestone C

- implement sharded concurrent baseline
- shard writer lock
- correct concurrent operations using simple synchronization
- tests

### Milestone D

- implement `ManualQuiesce`
- retire lists
- `Quiesce`
- debug checks
- ECS-focused tests
- benchmark

### Milestone E

- implement `LocalEpoch`
- local epoch domain
- reader guards
- retirement and reclamation
- benchmark

### Milestone F

- implement `HazardPointers`
- hazard records
- protect loops
- retire scans
- benchmark

### Milestone G

- performance pass
- shard alignment
- shard count tuning
- allocator or slab integration
- reserve heuristics
- chain depth tuning

### Milestone H

- hardening
- sanitizer passes
- soak and stress runs
- examples
- documentation cleanup

## 13. Benchmark Plan

Measure all three policies under:

- read-heavy workload
- write-heavy workload
- mixed ECS-style workload
- high-collision workload
- many small maps
- few large maps

Measure:

- throughput
- p99 latency
- memory usage
- retired memory lag
- resize cost
- false sharing

Expected positioning:

- `ManualQuiesce` should be fastest in engine and ECS contexts
- `LocalEpoch` should be the best general default
- `HazardPointers` should be the safest but likely the slowest on read-heavy paths

## 14. Recommended Defaults

Expected final defaults:

- default: `LocalEpoch`
- ECS specialization: `ManualQuiesce`
- safest automatic mode: `HazardPointers`

Possible public aliases:

```cpp
using DefaultConcurrentHashMap = ConcurrentHashMap<..., ReclamationPolicy::LocalEpoch>;
using ECSConcurrentHashMap = ConcurrentHashMap<..., ReclamationPolicy::ManualQuiesce>;
using SafeConcurrentHashMap = ConcurrentHashMap<..., ReclamationPolicy::HazardPointers>;
```

## 15. Recommended Execution Order

Implement in this sequence:

1. single-threaded chained shard table
2. shard writer lock
3. `ManualQuiesce`
4. stabilize and benchmark
5. `LocalEpoch`
6. make it the likely default
7. `HazardPointers`
8. optimize after correctness is proven

The key constraint is to keep the core map architecture stable and isolate reclamation behind a narrow policy contract.
