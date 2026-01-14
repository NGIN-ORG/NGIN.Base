Below is a **pseudo implementation plan / requirements doc** for building `Optional`, `Expected`, `Variant`, `Tuple` on top of your `StorageFor<T>` for **NGIN.Base** (RT/game oriented, ergonomic-for-you, high-perf).

---

# NGIN.Base Core Value Types Implementation Plan

## Global policies and conventions

### Build / config knobs (recommend)

* `NGIN_NO_EXCEPTIONS` (or `NGIN_EXCEPTIONS=0/1`)

  * Contract policy is consistent across Optional/Expected/Variant:
    * Checked accessors (`Value()`, `Error()`, `Get<I>()`) assert in debug, abort in release.
    * Unsafe accessors (`ValueUnsafe()`, `ErrorUnsafe()`, `GetUnsafe<I>()`) are UB if misused.
  * If exceptions are enabled, keep the same contract by default (no mixed throw behavior).
* `NGIN_ASSERT(expr)` contract macro (debug) + `NGIN_ABORT(msg)` (release-fatal)
* `NGIN_LIKELY/UNLIKELY` branch hints (optional)
* `NGIN_FORCEINLINE` (optional)

### Placement & namespace

* Public headers live under `include/NGIN/Utilities/`.
* Namespace for Optional/Expected/Variant/Tuple: `NGIN::Utilities`.

### Type traits

Don't use std type_traits header, use NGIN/Meta/TypeTraits.hpp

### Storage rule (core invariant)

* `StorageFor<T>` **never** stores state. Lifetime state is tracked by the wrapper.
* Every wrapper must explicitly define:

  * “alive state” variables (`bool engaged`, `uint32 index`, etc.)
  * when `StorageFor<T>::Construct/Destroy` is called
* `StorageFor<T>` is non-copyable; wrappers implement copy/move by constructing from `Ref()`/`Ptr()` as needed

### Triviality preservation (required)

* Optional/Expected/Variant should remain trivially destructible/copyable/movable when the contained types allow it.
* Use partial specialization or `requires` to default special members where possible.
* Avoid user-defined destructors when `T` is trivially destructible.

### Traits checklist (gate before implementation)

Ensure `NGIN/Meta/TypeTraits.hpp` provides:

* `IsTriviallyDestructible`, `IsTriviallyCopyConstructible`, `IsTriviallyMoveConstructible`
* `IsCopyConstructible`, `IsMoveConstructible`
* `IsCopyAssignable`, `IsMoveAssignable`
* `IsNothrow*` where you use `noexcept`
* `EnableIf` or `Requires` helpers used by the implementations

---

# 1) Optional<T>

## Purpose

Inline “maybe a T”, no allocations. Minimal overhead. Fast access with checked/unsafe paths.

## Data layout

* `bool hasValue;`
* `StorageFor<T> m_value;`

## Invariants

* `hasValue == true` ⇒ `m_value` contains a live `T`
* `hasValue == false` ⇒ no `T` alive in `m_value`

## API requirements (v1)

Construction:

* `Optional()` empty
* `Optional(nullopt)`
* `Optional(const T&)`, `Optional(T&&)` (optional; can be explicit)
* `InPlace` constructor: `Optional(InPlaceType<T>, args...)`

Observers:

* `bool HasValue() const`
* `explicit operator bool() const`
* `T& Value()` / `const T& Value()` (checked; assert/abort if empty)
* `T& ValueUnsafe()` / `const T& ValueUnsafe()`
* `T* Ptr()` / `const T* Ptr()` (nullable)
* `const T& ValueOr(const T& fallback) const &` (zero-copy; fallback lifetime is caller-owned)
* `T ValueOr(T fallback) &&` (move-friendly for rvalues)

Modifiers:

* `void Reset()`
* `template<class...Args> T& Emplace(Args&&...)`
* `void Swap(Optional&)` (nice later)

## Correctness requirements

* Copy/move ctor/assign must:

  * preserve optional-ness
  * destroy old value on assignment if needed
  * handle self-assignment safely
  * when both engaged and `T` is not assignable, destroy+reconstruct if constructible; otherwise disable assignment
* Destructor must call `Reset()`

## Performance requirements

* `sizeof(Optional<T>)` should be `sizeof(T) + 1 + padding`
* No dynamic allocations
* `Reset()` should compile to nothing for trivially destructible `T`

## Implementation steps

1. Implement `Reset()`: if `hasValue`, `m_value.Destroy(); hasValue=false;`
2. Implement `Emplace()`:

   * `Reset()`
   * `m_value.Construct(...)`
   * `hasValue=true`
3. Implement copy/move ctor:

   * if `other.hasValue`: `m_value.Construct(other.ValueUnsafe())`; `hasValue=true`
4. Implement copy/move assignment:

   * cases:

     * both empty → no-op
     * this engaged, other empty → destroy this
     * this empty, other engaged → construct into this
     * both engaged → assign `Ref() = other.Ref()` (or destroy+reconstruct if not assignable)
5. Add pointer/ref accessors (checked + unsafe)
6. Add `ValueOr` overloads for lvalue/rvalue qualifiers

---

# 2) Expected<T, E>

## Purpose

Return either a value or an error *inline*, no exceptions required. Great for engine subsystems.

## Data layout

* `bool hasValue;`
* `StorageFor<T> m_value;`
* `StorageFor<E> m_error;`

## Invariants

* `hasValue == true` ⇒ `m_value` alive, `m_error` not alive
* `hasValue == false` ⇒ `m_error` alive, `m_value` not alive

## API requirements (v1)

Types/helpers:

* `Unexpected<E>` helper wrapper for explicit error construction
* `InPlace` support

Construction:

* `Expected(T)` / `Expected(InPlaceType<T>, ...)`
* `Expected(Unexpected<E>)` / `Expected(InPlaceType<E>, ...)` (error path)

Observers:

* `bool HasValue() const`, `explicit operator bool() const`
* `T& Value()` / `const T& Value()` (checked)
* `E& Error()` / `const E& Error()` (checked)
* `T& ValueUnsafe()`, `E& ErrorUnsafe()`
* `const T& ValueOr(const T& fallback) const &` (zero-copy; fallback lifetime is caller-owned)
* `T ValueOr(T fallback) &&` (move-friendly for rvalues)

Modifiers:

* `void ResetToValue(args...)`
* `void ResetToError(args...)`
* `template<class F> Transform(...)` (later)
* `AndThen/OrElse` (later)

Special case requirements

* `Expected<void, E>` (very common)

  * store only error + bool
  * `Value()` is just a check
  * `Error()` only valid when `!HasValue()`

## Implementation steps

1. Define `Unexpected<E>` (move-only is fine)
2. Implement constructors:

   * value ctor sets `hasValue=true`, constructs `m_value`
   * error ctor sets `hasValue=false`, constructs `m_error`
3. Destructor:

   * if `hasValue`: `m_value.Destroy()`
   * else: `m_error.Destroy()`
4. Copy/move constructors:

   * branch on `other.hasValue` and construct matching storage
5. Assignments:

   * if same state: assign active member (or destroy+reconstruct if not assignable but constructible)
   * if different state: destroy current active, construct other active, flip flag
   * if required construction is not possible, disable the assignment operator via `requires`
6. Add `Expected<void,E>` specialization

## RT-friendly policy

If exceptions are off:

* no need to model “valueless” state
* `Value()` failure ⇒ assert/abort

---

# 3) Variant<Ts...>

## Purpose

Inline tagged union: exactly one alternative alive at a time. Used heavily in engines.

## Data layout (recommended)

* `IndexType index;` (size chosen by number of alternatives)
* `StorageFor<MaxSizedAlignedType>` m_storage;

Where `MaxSizedAlignedType` is a compile-time computed “largest” buffer type:

* Make `NGIN::Memory::AlignedBuffer<maxSize, maxAlign>`

## Invariants

* `index` is always a valid alternative index [0..N-1]
* the object stored in `m_storage` is exactly the type at `index`

## API requirements (v1)

Construction:

* default constructs alternative 0 (engine-friendly, never-valueless)
* `Variant(InPlaceIndex<I>, args...)`
* `Variant(InPlaceType<T>, args...)` (optional)

Observers:

* `IndexType Index() const`
* `bool HoldsAlternative<I>()`
* `T& Get<I>()` (checked) + `T* GetIf<I>()` (nullable)
* `T& GetUnsafe<I>()` (optional)
* `Visit(visitor)` (v1: switch-based)

Modifiers:

* `template<size_t I, class...Args> Emplace<I>(Args&&...)`
* `Swap` (later)

Semantics policy choices (recommend for engine)

* **Never valueless**
* Keep converting constructors minimal (prefer explicit `Emplace`)
* `Variant` is default-constructible iff `T0` is default-constructible

## Implementation steps

1. Metaprogramming utilities:

   * `TypeAt<I, Ts...>`
   * `IndexOf<T, Ts...>` (optional)
   * `MaxSize`, `MaxAlign`
   * `IndexType` selection:
     * `N <= 0xFF` ⇒ `uint8`
     * `N <= 0xFFFF` ⇒ `uint16`
     * else ⇒ `uint32`
2. Storage:

   * `StorageFor<AlignedBuffer<maxSize, maxAlign>>` (or just raw member)
3. Core operations:

   * `DestroyActive()`:

     * `switch(index)` call destructor of active type via `reinterpret_cast`
   * `Construct<I>(args...)`:

     * placement-new into buffer as `TypeAt<I>`
     * set `index=I`
4. Copy/move constructors:

   * `switch(other.index)` construct same type from other
5. Assignment:

   * If same `index`: assign active value (or destroy+reconstruct if not assignable but constructible)
   * Else: destroy current, construct new, update index
   * if required construction is not possible, disable the assignment operator via `requires`
6. Visit:

   * `switch(index)` call `visitor(Get<I>())`
   * For multiple variants later, you can extend, but start single-variant.

## Performance requirements

* `sizeof(Variant)` = buffer + index (+ padding)
* `Visit` should compile to a tight switch (inspect asm)

---

# 4) Tuple<Ts...>

## Purpose

Heterogeneous aggregate with zero overhead access; backbone for template plumbing and async/task systems.

## Data layout (recommended)

Use recursive inheritance + EBO for empty types:

* `TupleLeaf<I, T>` stores `T` (or inherits from `T` if empty & not final)
* `TupleImpl<index_sequence<...>, Ts...>` inherits from all leaves

## Invariants

* All elements are always alive (tuple is not optional)
* Element storage uses EBO where possible

## API requirements (v1)

Construction:

* default ctor if all elements default constructible
* forwarding ctor `Tuple(Args&&...)` matching Ts...
* `MakeTuple(...)` helper

Access:

* `Get<I>(tuple)` returning ref with correct cv/ref qualifiers
* `TupleSize<Tuple<Ts...>>`
* `TupleElement<I, Tuple<...>>`

Utilities:

* `Apply(f, tuple)` (very useful)
* `ForwardAsTuple(...)` (optional)
* `Tie(...)` (optional)

## Implementation steps

1. Implement `TupleLeaf<I, T, UseEbo>`
2. Implement `TupleImpl<Indices..., Ts...> : TupleLeaf<I, Ts>...`
3. Implement `Tuple<Ts...> : TupleImpl<...>`
4. Implement `Get<I>` via `static_cast<TupleLeaf<I, TypeAt<I>> &>(tuple).Get()`
5. Implement `Apply` using `std::index_sequence_for<Ts...>`

## Performance requirements

* Empty elements should not increase size (EBO)
* `Get` should be zero-cost

---

# Validation & testing plan (for all types)

## Test utilities (write first)

* `CountingType` (counts ctor/move/copy/dtor)
* `MoveOnlyType`
* `NonMovableType` (optional)
* `ThrowingType` (test build with exceptions enabled)

## Core test scenarios

* Construct/destroy sequences
* Copy/move correctness
* Assignment across states (esp Expected/Variant)
* Alignment tests (`alignof` correctness)
* `constexpr` tests where applicable
* Fuzz-like random operation sequences for Variant

## Bench goals

* Optional access should be same as pointer + branch
* Expected should be same cost as optional + error storage
* Variant visit should compile to switch; inspect assembly

---
