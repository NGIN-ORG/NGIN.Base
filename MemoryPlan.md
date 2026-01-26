# MemoryPlan.md

Pinpoint problematic areas in `NGIN::Memory` + allocator-aware containers, and propose a performance-first refactor plan.

This plan assumes breaking changes are allowed.

---

## Status (Current → Next → Done)

### Current (Start Here)

- [ ] Phase 3: Make `EpochReclaimer` explicitly-owned + allocator-injected (`include/NGIN/Memory/EpochReclaimer.hpp`)
- [ ] Phase 3: Add bounded/fixed-capacity variants where needed (hash map, queue, etc.)

### Next

- [ ] Phase 2: Update `include/NGIN/Memory/README.md` to match current allocator types (remove `BumpArena`/`OwnedTag` drift)

### Done

- [x] Documented problem inventory and refactor plan (`MemoryPlan.md`)
- [x] Locked in defaults: tri-state ownership in traits; cookie-first routing; allocator handles by value; move-propagation enabled; replace hash map (breaking) (`MemoryPlan.md`)
- [x] Phase 0: Fix `AllocationHelpers` array layout/deallocation base pointer (`include/NGIN/Memory/AllocationHelpers.hpp`)
- [x] Phase 0: Fix `HalfPointer::ToAbsolute` typing + add overflow assertions (`include/NGIN/Memory/HalfPointer.hpp`)
- [x] Phase 0: Make `ThreadSafeAllocator` lock for `MaxSize/Remaining/Owns` and add lock-type parameter (`include/NGIN/Memory/ThreadSafeAllocator.hpp`)
- [x] Phase 0: Add tests for `AllocationHelpers` and `HalfPointer` (`tests/Memory/AllocationHelpersTests.cpp`, `tests/Memory/HalfPointerTests.cpp`)
- [x] Phase 1: Add tri-state ownership to `AllocatorTraits`; remove “unknown == owns” behavior (`include/NGIN/Memory/AllocatorConcept.hpp`)
- [x] Phase 1: Make wrappers/composites traits-correct and explicit about requirements (`include/NGIN/Memory/FallbackAllocator.hpp`, `include/NGIN/Memory/TrackingAllocator.hpp`, `include/NGIN/Memory/AllocatorRef.hpp`)
- [x] Phase 1: Implement header-tagged fallback allocator (`TaggedFallbackAllocator`) (`include/NGIN/Memory/FallbackAllocator.hpp`)
- [x] Phase 1: Replace owning `PolyAllocator` with non-owning `PolyAllocatorRef` (`include/NGIN/Memory/PolyAllocator.hpp`)
- [x] Phase 1: Add allocator propagation traits and apply to core containers (`include/NGIN/Memory/AllocatorConcept.hpp`, `include/NGIN/Containers/Vector.hpp`, `include/NGIN/Text/String.hpp`)
- [x] Phase 2: Replace `FlatHashMap` with allocator-aware explicit-lifetime hash map (`include/NGIN/Containers/HashMap.hpp`)
- [x] Phase 2: Update tests for hash map behavior (`tests/Containers/HashMap.cpp`)
- [x] Phase 2: Remove legacy `FlatHashMap` implementation (replaced in place) (`include/NGIN/Containers/HashMap.hpp`)
- [x] Phase 2: Document hash map invalidation/constraints (`include/NGIN/Containers/HashMap.hpp`)
- [x] Phase 2: Remove empty header `include/NGIN/Containers/Array.hpp`

---

## Goals (Performance-First)

1. **Correctness/UB-free** allocation/deallocation across alignment, size, and composites.
2. **Deterministic behavior** options for real-time (no hidden global state, controllable allocation).
3. **Explicit allocator semantics** (ownership, propagation, routing) that work with stateful allocators.
4. **Allocator-aware containers** that avoid default-constructing N buckets, avoid unnecessary work, and support explicit lifetimes.
5. **Clean layering**: minimal hot-path allocator concept; optional introspection layered on top.

## Chosen Defaults (Decisions Locked In)

- **Ownership query:** tri-state ownership in traits ✅
- **Composite routing:** cookie-first routing (`AllocateEx` / `MemoryBlock::Cookie`), header fallback ✅
- **Container allocator storage:** containers store allocator *handles* by value; `AllocatorRef` for explicit borrowing ✅
- **Propagation:** traits-driven propagation; move-propagation generally enabled ✅
- **Hash map:** replace `FlatHashMap` (breaking changes allowed) ✅

Non-goals:

- Replacing every standard library type everywhere immediately (incremental migration is fine).
- Solving all concurrency reclamation patterns in one pass.

---

## P0: Must-Fix Correctness Issues (UB / data races / compile breaks)

### 1) Array helpers free the wrong pointer (UB for over-aligned `T`)

- Problem: `AllocateArrayUninitialized` allocates `raw = alloc.Allocate(rawSize, AAlign)` but later deallocates `header` which may not equal `raw`.
- Code: `include/NGIN/Memory/AllocationHelpers.hpp:58` to `include/NGIN/Memory/AllocationHelpers.hpp:79`, and `include/NGIN/Memory/AllocationHelpers.hpp:154`.
- Why this is bad:
  - If `alignof(T) > alignof(ArrayHeader)` or if the returned `raw` is not positioned such that `arr` lands immediately after the header, `header != raw`.
  - Freeing `header` (not the original `raw`) is undefined behavior for most allocators (e.g. `SystemAllocator` calling `std::free`).
  - `Tracking`/stats will become nonsense because deallocation sizes don’t match allocation sizes.

**Do instead**

- Change the array header layout to include a **back-pointer to the actual base** returned by the allocator (and ideally the allocated byte size + alignment).
  - Example: `struct ArrayHeader { std::size_t count; std::size_t rawSize; void* rawBase; std::uint32_t magic; };`
  - Deallocate uses `rawBase/rawSize/alignment`.
- Alternatively: require `AllocateEx` and stash the `MemoryBlock` metadata in the header.

### 2) `HalfPointer::ToAbsolute` returns the wrong type (and offset truncation risks)

- Problem: `ToAbsolute(T* base)` returns `reinterpret_cast<char*>(base) + offset` as `T*` without casting.
- Code: `include/NGIN/Memory/HalfPointer.hpp:26` to `include/NGIN/Memory/HalfPointer.hpp:32`.
- Additional risk: offset is a `UInt32` (`include/NGIN/Memory/HalfPointer.hpp:14`), so it silently truncates for heaps > 4GiB.

**Do instead**

- Return `reinterpret_cast<T*>(reinterpret_cast<std::byte*>(base) + offset)` (or equivalent).
- Add debug asserts (or checked constructor) that the distance fits `UInt32`.
- Consider a `HalfPointer64` or a template parameter for offset width if large arenas are expected.

### 3) `ThreadSafeAllocator` is not thread-safe for “const” queries

- Problem: `MaxSize()`, `Remaining()`, and `Owns()` don’t lock.
- Code: `include/NGIN/Memory/ThreadSafeAllocator.hpp:24` to `include/NGIN/Memory/ThreadSafeAllocator.hpp:35`.

**Do instead**

- Lock around all forwarded operations, including the “const” ones.
- Prefer a configurable lock type for RT:
  - `ThreadSafeAllocator<Inner, Lock = NGIN::Sync::SpinLock>` (or similar), defaulting to a low-contention spin lock for short critical sections.

---

## P1: Allocator API / Semantics Problems (inconsistencies, hidden requirements)

### 4) The minimal allocator concept is good, but many allocators assume more than they require

- `AllocatorConcept` only requires `Allocate/Deallocate` (`include/NGIN/Memory/AllocatorConcept.hpp:48`).
- But these types assume `Owns/MaxSize/Remaining` exist:
  - `include/NGIN/Memory/FallbackAllocator.hpp:24` (calls `m_primary.Owns(ptr)` and sums max/remaining)
  - `include/NGIN/Memory/TrackingAllocator.hpp:59`
  - `include/NGIN/Memory/PolyAllocator.hpp:23`

**Do instead (traits-first; keep the core concept minimal)**

- Keep `AllocatorConcept` as-is for hot paths and generic code.
- Make every “extra feature” exclusively accessed via `AllocatorTraits`, and make allocators/composites *not compile* (or take an alternate path) if a required capability isn’t present.
- Fix the ownership-query model:
  - Replace boolean ownership with tri-state in traits:
    - `enum class Ownership { Owns, DoesNotOwn, Unknown };`
    - `static Ownership OwnershipOf(const A&, const void*) noexcept;`
  - For routing decisions, treat `Unknown` as “cannot decide”: require cookie/header tagging instead.

Practical rule:

- `MaxSize/Remaining`: “unknown” may default to `numeric_limits<size_t>::max()` because it’s informational.
- `Owns`: “unknown” must *never* be treated as true in any code path that chooses which allocator to free with.

### 5) Routing `Deallocate` via `Owns()` is fragile for composite allocators

`FallbackAllocator` depends on `Owns()` routing (`include/NGIN/Memory/FallbackAllocator.hpp:24`), which tends to be:

- slow (range checks or tree lookups),
- sometimes impossible (system malloc can’t answer reliably),
- and dangerous if defaulted.

**Do instead**

- Make routing explicit and independent of `Owns()`:
  - Prefer `AllocateEx()` / `MemoryBlock::Cookie` tagging (`include/NGIN/Memory/AllocatorConcept.hpp:25`) when available (**cookie-first**).
  - Otherwise store a small header immediately before the returned pointer with a sub-allocator id (and any required metadata) (**header fallback**).
  - Provide a composite that guarantees correct deallocation without queries:
    - `TaggedFallbackAllocator` (header/cookie-based) for correctness.
    - Keep `FallbackAllocator` only if both allocators have reliable `Owns()` (and enforce that via traits checks / `static_assert`).

---

## P1: Type Erasure and Globals (RT-unfriendly defaults)

### 6) `PolyAllocator` heap-allocates and assumes rich API

- Heap allocate in ctor: `include/NGIN/Memory/PolyAllocator.hpp:14` to `include/NGIN/Memory/PolyAllocator.hpp:16`.
- Assumes `MaxSize/Remaining/Owns` exist (`include/NGIN/Memory/PolyAllocator.hpp:23`).

**Do instead**

- Prefer non-owning “ref” erasure:
  - `PolyAllocatorRef` stores `void* obj` + vtable, no allocation.
- If you need owning erasure, use SBO:
  - `AnyAllocator<InlineBytes>` storing the allocator object in-place.
- For RT, explicitly label owning type-erasure as “slow path”.

### 7) `EpochReclaimer` uses singleton + heap + `std::function`

- Global singleton: `include/NGIN/Memory/EpochReclaimer.hpp:41`.
- Per-thread heap allocation: `include/NGIN/Memory/EpochReclaimer.hpp:100`.
- `std::function` and `std::vector` in hot paths: `include/NGIN/Memory/EpochReclaimer.hpp:15` and `include/NGIN/Memory/EpochReclaimer.hpp:37`.

**Do instead**

- Make reclamation **explicitly owned** and passed to the structure that needs it (no singleton).
- Use allocator-aware containers (e.g., NGIN `Vector`) and deterministic storage (bounded ring buffer or fixed block lists) for retire lists.
- Replace `std::function<void(void*)>` with:
  - function pointer + context pointer, or
  - templated deleter stored with the node (if the API allows).
- Consider moving to `include/NGIN/Memory/Experimental/...` or clearly marking as non-RT by default.

---

## P1: Containers (Allocator awareness + lifetime correctness + hot-path costs)

### 8) `FlatHashMap` is not allocator-aware and has problematic lifetime semantics

- Not allocator aware: uses `Vector<Entry>` with default allocator and also includes unused `<vector>` (`include/NGIN/Containers/HashMap.hpp:14`).
- Forces default-constructible `Value` (`include/NGIN/Containers/HashMap.hpp:24`) and effectively also needs default-constructible `Key` due to the bucket array strategy.
- “Erase” doesn’t destroy `Key/Value`, only clears a flag:
  - `include/NGIN/Containers/HashMap.hpp:220` stores `Key` and `Value` directly in every bucket.
  - `Clear()` only clears flags (`include/NGIN/Containers/HashMap.hpp:285`).
  - `Remove()` clears flag then shifts (`include/NGIN/Containers/HashMap.hpp:388`).

**Do instead**

- Replace with a new implementation that uses explicit storage + explicit lifetime:
  - Store `std::byte keyStorage[...]` + `valueStorage[...]` (like `ConcurrentHashMap`’s `SlotStorage`).
  - Construct/destruct payload on insert/erase.
  - Keep a compact control byte array to support fast probing.
- Make it allocator-aware:
  - `template<class Key, class Value, ..., Memory::AllocatorConcept Alloc = Memory::SystemAllocator>`
  - store allocator with `[[no_unique_address]]` and define allocator propagation semantics.

**Implemented**

- `include/NGIN/Containers/HashMap.hpp` replaced in-place with an allocator-aware, explicit-lifetime flat hash map.
- Backward-shift deletion is now correct for general probe sequences (no “stop at home” bug).
- New constraint: `Key` and `Value` must be `std::is_nothrow_move_constructible_v` (required for backward-shift relocation without risking partial corruption).

### 9) `Vector` docs vs behavior mismatch (and allocator propagation needs a policy)

- Comment claims “externally-owned allocator” and `Allocator::Instance()` (`include/NGIN/Containers/Vector.hpp:4`), but `Vector` stores allocator by value (`include/NGIN/Containers/Vector.hpp:35`).

**Do instead**

- Standardize container allocator storage:
  - Containers store allocator *handles* by value (copyable, cheap, points to a stable resource).
  - Use `AllocatorRef` explicitly when borrowing a non-owning allocator reference is desired.
- Document propagation:
  - Traits-driven propagation flags (std-like semantics) and default policy:
    - move construction/assignment: propagate allocator handle by default
    - copy assignment: do not silently switch allocator unless a trait explicitly opts in

### 10) `include/NGIN/Containers/Array.hpp` is an empty header

- File exists but is 0 bytes. (`include/NGIN/Containers/Array.hpp:1`)

**Do instead**

- Either delete it, or implement `Array<T, N>` with consistent conventions (and add tests).

---

## P2: Documentation / Naming Drift

### 11) `include/NGIN/Memory/README.md` describes non-existent types and incorrect layout

- Mentions `BumpArena`, `OwnedTag`, `BorrowedTag` that do not exist in headers (`include/NGIN/Memory/README.md:10` to `include/NGIN/Memory/README.md:29`).
- Claims “header + back-pointer” (`include/NGIN/Memory/README.md:54`) but current array helper does not store a back-pointer.

**Do instead**

- Update docs to match reality, or add compatibility aliases/types:
  - `using BumpArena = LinearAllocator<>;` (or rename the type).
  - Add `OwnedTag`/`BorrowedTag` only if they’re actively used in public APIs.

---

## Target Architecture (Proposed)

### A) Two-tier allocator API

1) **Hot-path allocator concept** (minimal)

- `Allocate(bytes, alignment) -> void*` (may return null)
- `Deallocate(ptr, bytes, alignment) noexcept` (bytes/alignment may be ignored)

1) **Optional introspection and routing**

- `AllocateEx(bytes, alignment) -> MemoryBlock` (size/alignment/cookie)
- `MaxSize()`, `Remaining()`
- Ownership query should not be a bool default; use tri-state or require it only when needed.

### B) “Allocator handle” rule for containers

For stateful allocators, containers should store a *handle* that is:

- cheap to copy/move,
- refers to a stable backing resource,
- and has well-defined semantics when copied.

Concretely:

- Keep `AllocatorRef<A>` as the non-owning “borrowed” handle.
- Add `SharedAllocator<A>` or `IntrusiveRefAllocator<A>` if shared ownership is desired.
- Only store heavyweight allocators by value when they are explicitly designed to be value-stored (e.g., bump arena with owning slab).

### C) Eliminate “guessing” in composite allocators

Any composite allocator that needs to route `Deallocate` should do so via:

- explicit cookie/tag in allocation result, or
- explicit header attached to returned pointer.

---

## Proposed Refactors (Work Items)

### Phase 0 (Safety baseline)

- Fix `AllocationHelpers` array layout + deallocation base pointer.
- Fix `HalfPointer::ToAbsolute` typing and add overflow assertions.
- Fix `ThreadSafeAllocator` to lock all forwarded operations.

### Phase 1 (Allocator consistency)

- Enforce `AllocatorTraits` usage so types don’t silently depend on optional APIs (avoid proliferating concepts).
- Add tri-state ownership to `AllocatorTraits` and remove “unknown == owns” behavior.
- Redesign `FallbackAllocator` as either:
  - `FallbackAllocator<Primary, Secondary>` requiring `Owns`, or
  - `TaggedFallbackAllocator` that writes a tag/header and never calls `Owns`.
- Replace `PolyAllocator` with `PolyAllocatorRef` and/or `AnyAllocator<InlineBytes>`.
- Define container allocator propagation traits and adopt the default policy (move-propagation generally enabled).

### Phase 2 (Containers)

- Replace `FlatHashMap` with an allocator-aware “explicit lifetime” hash map (breaking change).
  - Consider reusing `ConcurrentHashMap`’s `SlotStorage`/group layout for the single-threaded map to share optimizations.
- Define allocator propagation policy for `Vector` (and all containers).
- Remove or implement `include/NGIN/Containers/Array.hpp`.

### Phase 3 (RT determinism)

- Move `EpochReclaimer` behind explicit ownership + allocator injection; remove the singleton as default.
- Add “bounded / fixed capacity” variants where it matters (hash map, queue, string if needed).

---

## Breaking Changes (Explicit)

Potentially breaking, but recommended:

- Change array helper ABI/layout and deallocation requirements (`AllocationHelpers`).
- Replace boolean ownership with tri-state in allocator traits; remove implicit “unknown means owns”.
- Change `FallbackAllocator`/`Tracking`/`PolyAllocator` constraints or behavior.
- Replace `FlatHashMap` implementation and its requirements (default-constructibility, erase semantics).
- Standardize allocator propagation rules across all containers (copy/move semantics).

---

## Verification Checklist (before merging refactors)

Tests (minimum):

- Over-aligned array allocation/deallocation (`alignof(T) >= 64`) using `SystemAllocator` and `Tracking`.
- `FallbackAllocator` routing correctness (including null returns and mixed frees).
- `ThreadSafeAllocator` multi-thread smoke test.
- Hash map erase semantics ensure destructors run and memory is reclaimed.

Benchmarks (recommended):

- `FlatHashMap` (old vs new) for insert/find/erase at various load factors.
- Allocator overhead microbenchmarks: `Allocate/Deallocate` for System vs Linear vs composites.

---

## Next Step

If you want, I can implement **Phase 0** immediately (the UB and race fixes), then follow with the allocator-layering refactor and a replacement for `FlatHashMap`.
