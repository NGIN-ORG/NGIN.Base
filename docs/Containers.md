# Containers

NGIN containers are allocator-aware Foundation types:

- `Vector<T, Allocator>` is contiguous owning storage
- `HashMap<Key, Value, ...>` is the general non-concurrent hash table
- `ConcurrentHashMap<Key, Value, ...>` is sharded and supports lock-free read
  guards with explicit reclamation policies

Reserve capacity when the workload is known and treat iterator/reference
invalidation as part of each container's mutation contract.

`ConcurrentHashMap` offers `ManualQuiesce`, `LocalEpoch`, and `HazardPointers`.
The automatic policies reclaim retired tables after registered readers become
safe. `ManualQuiesce` requires an externally synchronized `Quiesce()` call.
Long-lived read guards intentionally delay reclamation; diagnostics expose
active readers and pending/reclaimed retired objects so this can be observed.

Allocator choice is explicit. See [Memory](Memory.md) for pool, debug, tracking,
fallback, and thread-safe allocator behavior.
