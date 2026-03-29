# String Overhaul Plan

Plan for modernizing `NGIN::Text::BasicString` from a solid engine string into a more formally specified, safer, and benchmarked string foundation.

This roadmap assumes breaking changes are allowed where they materially improve the design.

---

## Summary

The overhaul is staged to keep implementation risk controlled:

1. Phase 1 rebuilds the core representation and contracts.
2. Phase 2 broadens the API into a more complete string abstraction.
3. Phase 3 tunes search and hot-path performance based on benchmarks.
4. Phase 4 improves allocator ecosystem support and builder ergonomics.
5. Phase 5 closes the loop with documentation, benchmarking, and migration cleanup.

The first implementation pass should optimize for correctness and structure first, not for maximum API breadth.

---

## Design Targets

### Primary goals

- UB-free storage across SBO and heap states.
- Explicit allocator-correct copy, move, and swap semantics.
- Traits-aware string behavior instead of hardcoding `std::char_traits<CharT>`.
- Fully documented invariants, invalidation, aliasing, and moved-from behavior.
- Benchmarked performance decisions rather than speculative micro-optimizations.

### Non-goals for the first pass

- Matching `std::basic_string` ABI.
- Immediate parity with every STL string overload.
- Aggressive bit-packing or representation tricks before benchmark evidence exists.

---

## Public Header Layout

### Target structure

- Keep `include/NGIN/Text/String.hpp` as the stable facade include.
- Add `include/NGIN/Text/BasicString.hpp` as the primary template definition header.
- Move `BasicString`, `DefaultGrowthPolicy`, and related helper types into `BasicString.hpp`.
- Keep aliases in `String.hpp`:
  - `String`
  - `WString`
  - `AnsiString`
  - `AsciiString`
  - `UTF8String`
  - `UTF16String`
  - `UTF32String`
  - `NativeString`

### Rationale

- Existing users keep including `String.hpp`.
- Advanced users can include `BasicString.hpp` directly.
- The facade split keeps implementation growth out of the alias header and makes future text-layer additions cleaner.

---

## Target Template Shape

Move to a traits-aware template while preserving low migration cost for existing direct instantiations:

```cpp
template<
    class CharT,
    UIntSize SBOBytes,
    NGIN::Memory::AllocatorConcept Alloc = NGIN::Memory::SystemAllocator,
    class Growth = DefaultGrowthPolicy,
    class Traits = std::char_traits<CharT>>
class BasicString;
```

### Contract requirements

- `Alloc` must satisfy `NGIN::Memory::AllocatorConcept`.
- `Growth` must provide `static constexpr UIntSize Grow(UIntSize oldCap, UIntSize required) noexcept`.
- `Traits` must provide the operations required for length, compare, assign, move, and copy semantics used by the implementation.

Add compile-time constraints or `static_assert`s so invalid policy types fail clearly at compile time.

---

## Core Invariants

These invariants should be explicitly documented and defended in helpers:

- `Size()` returns the number of characters excluding the terminator.
- `Capacity()` returns the maximum number of characters excluding the terminator.
- `Data()` always points to live `CharT` storage.
- `Data()[Size()]` is always `CharT(0)`.
- Small mode and heap mode are mutually exclusive and never rely on reading inactive storage.
- Moved-from strings are valid, empty, and null-terminated.
- Any operation that cannot preserve heap ownership under allocator rules must fall back to deep copy rather than stealing.

---

## Phase 1: Core Representation And Contracts

### Objectives

- Eliminate representation UB and implicit lifetime hazards.
- Make allocator behavior deliberate and documented.
- Split `BasicString` into its own header.
- Establish one authoritative contract for aliasing, invalidation, and moved-from state.

### Implementation changes

- Replace the current union + size-byte small representation with an always-live layout:
  - explicit `m_isSmall`
  - full-width `m_size`
  - explicit heap pointer and heap capacity
  - SBO storage as `std::array<CharT, sbo_chars + 1>`
- Remove inactive-union reads entirely.
- Remove the 8-bit SBO size limit.
- Route operations through `Traits` instead of hardcoded `std::char_traits<CharT>`.
- Centralize allocation and deallocation in byte-accurate helpers.
- Guard all zero-length `memcpy` and `memmove` calls consistently.
- Rework `Assign` and `Append` around explicit commit-style helpers so reallocation and aliasing rules are obvious.
- Keep `String.hpp` as the public facade and move the full template implementation into `BasicString.hpp`.

### API decisions

- Add `View() const noexcept`.
- Add non-const `at(size_type)` and remove the inconsistent `At(size_type)`.
- Keep `CStr()`, `GetSize()`, `GetCapacity()`, and `GetAllocator()` for compatibility.
- Keep the current alias names unchanged.

### Allocator behavior to formalize

- Copy assignment deallocates with the current allocator before any propagated allocator replacement.
- Move assignment steals storage only when:
  - `PropagateOnMoveAssignment` is true, or
  - `IsAlwaysEqual` is true, or
  - the implementation has an explicit and valid allocator-equality path
- `Swap` is constant-time only when propagation or allocator equality rules allow it.
- Non-propagating unequal allocator swap uses a deep-content fallback.

### Tests

- SBO to heap transition.
- Heap to SBO `ShrinkToFit`.
- Self-assign and self-append.
- Overlapping-view assign and append in SBO and heap modes.
- Copy and move assignment under non-propagating allocators.
- Swap with propagating and non-propagating allocators.
- Wide-character sanity.
- Zero-length construction, assign, append, reserve, and resize.
- Deallocate byte/alignment verification with tracking allocators.

### Benchmarks

- Short and long construction.
- Short and long append.
- Copy and move assignment.
- Reserve and resize patterns.
- Overlapping append/assign microbenchmarks.
- Object-size and effective SBO-capacity reporting.

---

## Phase 2: API Surface Expansion

### Objectives

- Make `BasicString` a more complete day-to-day string abstraction.
- Close the largest API surface gaps identified in review.

### Additions

- `Append(size_type count, CharT ch)`
- `Insert(size_type pos, view_type sv)`
- `Insert(size_type pos, size_type count, CharT ch)`
- `Erase(size_type pos, size_type count = npos)`
- `Replace(size_type pos, size_type count, view_type replacement)`
- `Substr(size_type pos = 0, size_type count = npos) const`
- `RemovePrefix(size_type count)`
- `RemoveSuffix(size_type count)`
- `StartsWith(CharT ch)`
- `StartsWith(const CharT* cstr)`
- `EndsWith(CharT ch)`
- `EndsWith(const CharT* cstr)`
- richer `operator+` overloads for:
  - string + `view_type`
  - `view_type` + string
  - string + `const CharT*`
  - `const CharT*` + string
  - string + `CharT`
  - `CharT` + string
- reverse iterators
- `Front()` and `Back()` naming review if the library wants PascalCase wrappers consistently

### Behavior requirements

- New mutators must be alias-safe when the source view points into the current string.
- All operations must preserve null termination.
- Complexity and invalidation behavior must be documented for every new mutator.

### Tests

- Insert/erase/replace at front, middle, and back.
- Substring boundaries and out-of-range behavior.
- Prefix/suffix removal crossing SBO/heap boundaries.
- Overlapping replace cases using self-derived views.
- Reverse iteration over empty, SBO, and heap strings.

---

## Phase 3: Search And Hot-Path Performance

### Objectives

- Improve search behavior and common small-string hot paths with benchmark justification.

### Search additions

- `RFind`
- `FindFirstOf`
- `FindLastOf`
- `FindFirstNotOf`
- `FindLastNotOf`

### Performance work

- Add tuned fast paths for:
  - single-character search
  - very short needles
  - `CharT = char` common cases
- Evaluate whether `std::search`, Boyer-Moore-Horspool, or a lightweight custom heuristic helps on larger needles.
- Review short-append and short-assign paths for avoidable branching or temporary allocations.
- Review growth policy thresholds with measured data instead of static intuition.

### Representation review

- Measure object size and padding.
- Measure cost of the explicit `m_isSmall` branch in `Size()`, `Data()`, and `Capacity()`.
- Only after data exists, decide whether tighter metadata packing is worth the added complexity.

### Benchmarks

- Compare against:
  - `std::string`
  - current `BasicString` baseline saved before the overhaul
  - optional third-party comparisons if available in the benchmark environment
- Track:
  - object size
  - small-string construction
  - append throughput
  - find/rfind latency by needle size
  - allocator-sensitive workloads

---

## Phase 4: Allocator Ecosystem And Builder Ergonomics

### Objectives

- Make the string easier to use in allocator-heavy engine code and higher-throughput builder scenarios.

### Additions

- PMR-friendly adapter or documented bridge path if `std::pmr` interop is desired.
- `ResizeAndOverwrite`-style builder API for high-performance generation.
- Optional ownership-transfer API only if a real engine use case exists:
  - `Release`
  - `Detach`
- More formal allocator documentation:
  - allocation/deallocation byte contract
  - propagation expectations
  - stateful allocator equality assumptions

### Constraints

- Builder APIs must preserve the null-termination invariant on completion.
- Ownership-transfer APIs should only land if the allocator and lifetime model are fully specified.

### Tests

- Builder callback writes exact size and shorter-than-capacity results.
- PMR or adapter-backed instances preserve allocator correctness.
- Detached/released ownership, if added, has explicit lifetime tests.

---

## Phase 5: Documentation, Benchmarking, And Migration Closure

### Objectives

- Make the new string behavior easy to understand and defend.
- Close the gap between implementation quality and documented contract quality.

### Documentation updates

- Update `Dependencies/NGIN/NGIN.Base/docs/Memory.md` to reflect the new template shape and semantics.
- Add explicit notes on:
  - invalidation
  - thread safety
  - aliasing/self-reference guarantees
  - moved-from state
  - allocator propagation and swap behavior
  - complexity expectations
- Add a short component note or README if the text module grows beyond one header pair.

### Benchmark deliverables

- Preserve before/after numbers for the phase-1 baseline and the tuned version.
- Record any chosen growth-threshold or search-threshold rationale.
- Capture object-size comparisons in benchmark output or documentation.

### Migration cleanup

- Remove temporary compatibility shims only after internal call sites are migrated.
- Revisit whether `CStr()`, `GetSize()`, and `GetCapacity()` should remain long term.
- Re-evaluate whether `String.hpp` should stay facade-only permanently.

---

## Acceptance Criteria By Phase

### Phase 1 done when

- `BasicString` is split into `BasicString.hpp` with `String.hpp` as facade.
- Inactive-storage UB is removed from the representation.
- Traits-aware implementation is in place.
- Core allocator and aliasing rules are documented and tested.
- Existing benchmarks still build and run.

### Phase 2 done when

- The missing core mutators are implemented and documented.
- New APIs have positive and negative tests.
- New mutators preserve allocator and aliasing guarantees.

### Phase 3 done when

- Search additions and fast paths are benchmarked.
- No performance tuning change lands without measured justification.

### Phase 4 done when

- Builder and allocator-interop features are contract-complete.
- Any ownership-transfer API has clear semantics and tests.

### Phase 5 done when

- Repo documentation matches the implementation.
- Benchmark evidence exists for the major design choices.
- Compatibility debt is consciously either kept or removed.

---

## Recommended Order

1. Land Phase 1 completely before adding broad new APIs.
2. Implement Phase 2 mutators on top of the new core helpers.
3. Use Phase 3 benchmarks to decide whether representation tightening is worth the complexity.
4. Add allocator-interop and builder features only after the core mutator set is stable.
5. Finish with documentation and migration cleanup.

---

## Explicit Defaults Chosen

- `String.hpp` remains the stable public include.
- `BasicString.hpp` becomes the new template implementation header.
- Breaking changes are allowed.
- Compatibility aliases for `String` and related typedefs remain.
- Compatibility methods `CStr()`, `GetSize()`, and `GetCapacity()` remain for now.
- Phase 1 is correctness-first, not API-maximal and not micro-optimization-first.
