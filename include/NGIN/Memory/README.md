# NGIN Memory Module

Modern, composable, header-only allocators for high-performance components.

## Components

| Component | Purpose |
|-----------|---------|
| `SystemAllocator` | Aligned system allocations (stateless) |
| `BumpArena` | Fast linear arena with markers & rollback |
| `Tracking<Inner>` | Decorator collecting allocation statistics |
| `FallbackAllocator<Primary,Secondary>` | Try primary then fallback |
| `ThreadSafe<Inner>` | Mutex-protected decorator |
| `AllocationHelpers` | Safe object/array construction helpers |

## Design Principles

1. Concepts over virtual dispatch (`AllocatorConcept`).
2. Explicit ownership (`OwnedTag` / `BorrowedTag`).
3. Zero-cost abstractions via `[[no_unique_address]]` for decorators.
4. Deterministic array layout (header + back-pointer) for O(1) deallocation.
5. Optional instrumentation / thread safety through composition.

## Quick Start

```cpp
using Arena = NGIN::Memory::BumpArena<>; // SystemAllocator upstream
Arena arena{NGIN::Memory::OwnedTag{}, 64 * 1024};
auto mark = arena.Mark();
int* values = static_cast<int*>(arena.Allocate(100 * sizeof(int), alignof(int)));
// ... use values ...
arena.Rollback(mark); // free all since mark
```

Tracking:

```cpp
using TrackedArena = NGIN::Memory::Tracking<Arena>;
TrackedArena tracked{ Arena{NGIN::Memory::OwnedTag{}, 4096} };
void* p = tracked.Allocate(128, 16);
auto stats = tracked.GetStats(); // peak/current
```

Fallback:

```cpp
using FBA = NGIN::Memory::FallbackAllocator<TrackedArena, Arena>;
FBA allocator{ TrackedArena{ Arena{NGIN::Memory::OwnedTag{}, 1024} }, Arena{NGIN::Memory::OwnedTag{}, 8192} };
void* big = allocator.Allocate(4000, 16); // likely taken from secondary
```

## Array Helper Layout

AllocationHelpers store an `ArrayHeader` at base and a back-pointer just before aligned element block for constant-time retrieval; no linear scan.

## Migration Notes

- Legacy `IAllocator` remains temporarily for existing containers.
- New code should prefer concept-based APIs.
- Gradually template containers on a generic `Alloc` meeting `AllocatorConcept`.

## Testing Guidance

- Positive + negative tests (allocation success, exhaustion, rollback).
- Marker rollback correctness (usage decreases to expected value).
- Tracking stats invariants (peak >= current, counters match operations).

## Future Extensions

- Freelist / segregated pool allocators.
- Debug canary / poisoning decorators.
- Reallocation convenience helper.

---
Apache 2.0 Licensed.
