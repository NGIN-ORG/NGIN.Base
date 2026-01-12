# StringPlan.md

Plan for refactoring `NGIN::Containers::BasicString` toward UB-free storage, high performance, and allocator-correct usability.

This plan assumes breaking changes are allowed.

---

## Status (Current → Next → Done)

### Current (Start Here)

- [ ] Phase 0: Replace union-based storage to remove inactive-member UB and lifetime issues
- [ ] Phase 0: Establish a single, always-present size field (remove 8-bit small-size limit)
- [ ] Phase 0: Add `GetAllocator()` member; remove allocator-access hack

### Next

- [ ] Phase 1: Implement allocator-aware swap semantics (propagate-on-swap / always-equal rules)
- [ ] Phase 1: Unify reset-to-empty logic and move-assign invariants
- [ ] Phase 2: Standardize on `std::memcpy/std::memmove` for trivial `CharT`

### Done

- [ ] Phase 3: Optional fast-path `Find` for `CharT = char` (heuristic cutoff)
- [ ] Phase 3: Document invalidation/thread-safety guarantees
- [ ] Phase 4: Add tests covering SBO/heap transitions, aliasing append, propagation, swap/move

---

## Goals (Performance-First)

1. **UB-free storage** across all SBO/heap transitions, alignments, and CharT widths.
2. **Always-correct size/cap semantics** without small-size byte limits.
3. **Allocator-correct behavior** (propagation traits, swap semantics, explicit access, consistent deallocate sizes).
4. **Fast hot-paths** with minimal branches and consistent memcpy/memmove use.
5. **Clear contracts** for invalidation, thread safety, and exception behavior.

Non-goals:
- Reproducing the exact `std::string` ABI.
- Micro-optimizing search at the expense of clarity unless `CharT = char`.

---

## Proposed Storage Model (UB-Free)

### Replace union with explicit layout

- Store both small buffer and heap pointer fields in a single struct.
- Maintain an explicit discriminator flag (`m_isSmall`) or a tagged capacity.
- Always store `m_size` and `m_capacity` as full words (no 8-bit size byte).
- Small buffer should be **actual CharT storage**:
  - `std::array<CharT, sbo_chars + 1>` or equivalent aligned storage.
  - This avoids strict aliasing and object-lifetime hazards.

### Suggested invariants

- `m_size` is the number of characters (excluding terminator).
- `m_capacity` is the maximum number of characters (excluding terminator).
- `Data()` always returns a pointer to a live `CharT` buffer with a terminator at `Data()[m_size]`.
- `m_isSmall == true` implies `Data()` points to the small buffer; otherwise heap.

---

## P0: Correctness / UB Removal

### 1) Remove inactive-union UB

- Replace `union Storage` with an explicit `struct` holding both layouts.
- Avoid reading inactive union members entirely.

### 2) Fix SBO size encoding

- Remove the size-byte in the last SBO byte.
- Keep `m_size` as a full `UIntSize` regardless of storage mode.
- Small buffer capacity computed from SBO bytes without metadata overlap.

### 3) Fix CharT lifetime / aliasing

- Use `std::array<CharT, sbo_chars + 1>` (or aligned storage + `std::start_lifetime_as_array` if needed).
- Ensure `CharT` objects are created properly in SBO storage.

---

## P1: API & Allocator Semantics

### 4) Add allocator access member

- Add `const Alloc& GetAllocator() const noexcept`.
- Update `operator+(sv, string)` to use `GetAllocator()`.
- Remove the inheritance-based `get_allocator()` hack.

### 5) Swap semantics

- Follow `AllocatorPropagationTraits<Alloc>::PropagateOnSwap`.
- If allocators differ and propagation is false:
  - if `IsAlwaysEqual`, swap without moving allocators;
  - otherwise swap by moving contents through a temporary or reassign.
- Document swap behavior explicitly.

### 6) Allocator concept compliance

- Require only `Allocate/Deallocate` from `AllocatorConcept`; do not depend on `Owns/MaxSize/Remaining` or cookies.
- Always pass the same `bytes` + `alignment` to `Deallocate` that were used at `Allocate`.
- Ensure allocator handles are treated as non-owning values (compatible with `AllocatorRef`).

### 7) Unified reset / move invariants

- Add `ResetToEmptySmall()` helper to set size, terminator, and discriminant consistently.
- Use it in all move/copy/assign paths to avoid branchy, divergent cleanup.

---

## P2: Performance & Growth Policy

### 8) Growth policy cleanup

- Remove redundant `max(NextPow2(required), required)`.
- Add overflow guards where needed (cap at max capacity).
- Consider a pure 1.5x growth after a small threshold.

### 9) Consistent memcpy/memmove

- With `std::is_trivial_v<CharT>`, prefer `std::memcpy`/`std::memmove` for all data moves.
- Keep `char_traits::compare` for comparisons and `length` only.

---

## P3: Search / Find Improvements (Optional)

- Keep naive `Find` for all `CharT`.
- Add an optimized path for `CharT = char` when needle length >= a small cutoff.
- Avoid pulling in heavy dependencies; consider `std::search` or a lightweight Boyer-Moore-Horspool.

---

## Tests (Required)

1. **SBO ↔ Heap transitions**: resize, reserve, shrink-to-fit.
2. **Aliasing append**: `s.Append(s)`, `s.Append(s.View().substr(...))`.
3. **Allocator propagation**: copy/move/swap rules for stateful allocators.
4. **Move + reset invariants**: moved-from strings are valid and empty.
5. **Wide CharT**: `wchar_t` or `char16_t` basic sanity.
6. **Deallocate size/alignment**: use `Tracking` (or a debug allocator) to verify consistent bytes/alignment.

---

## Documentation Updates

- `include/NGIN/Containers/README.md` (if present): update string semantics.
- In `String.hpp`: document invalidation and thread-safety assumptions.
- Add brief notes on SBO policy and allocator propagation.

---

## Implementation Order (Recommended)

1. Replace storage layout + invariants (UB removal).
2. Add `GetAllocator()` member and update operators.
3. Normalize helpers (`ResetToEmptySmall`, `SetSize`, etc.).
4. Swap semantics + allocator propagation rules.
5. Apply consistent memcpy/memmove paths.
6. Update docs and tests.
