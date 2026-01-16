Below is a **pseudo implementation plan / requirements doc** for building `Optional`, `Expected`, `Variant`, `Tuple` on top of your `StorageFor<T>` for **NGIN.Base** (RT/game oriented, ergonomic-for-you, high-perf).

---

# NGIN.Base Core Value Types Implementation Plan

## Global policies and conventions

## Design decisions baked in (no forks)

* Checked access is always **contract-fatal** (assert in debug, abort in release) even when exceptions are enabled.
   * If a throwing accessor is ever desired, it must be explicitly added later (e.g. `ValueOrThrow()`), never by changing `Value()`.
* `Variant` is **never valueless**.
   * In exception-enabled builds, if an operation would otherwise risk leaving the object without a valid active alternative (due to an exception during construction), the library **aborts**.
* Triviality preservation is a hard requirement for `Optional` / `Expected` / `Variant` where the underlying types allow it.
   * This requires `StorageFor<T>` itself to be trivially copyable/movable when `T` is trivially copyable/movable (details below).

### Build / config knobs (recommend)

* `NGIN_NO_EXCEPTIONS` (or `NGIN_EXCEPTIONS=0/1`)

  * Contract policy is consistent across Optional/Expected/Variant:
    * Checked accessors (`Value()`, `Error()`, `Get<I>()`) assert in debug, abort in release.
    * Unsafe accessors (`ValueUnsafe()`, `ErrorUnsafe()`, `GetUnsafe<I>()`) are UB if misused.
  * If exceptions are enabled, keep the same contract by default (no mixed throw behavior).
* Required contract/hint helpers (define these in `include/NGIN/Defines.hpp`):

   * `NGIN_ASSERT(expr)`
      * Debug: `assert(expr)`
      * Release: compiled out
   * `NGIN_ABORT(msg)`
      * Always terminates (e.g. `std::abort()`), `msg` is for diagnostics/logging.
   * `NGIN_UNREACHABLE()`
      * Uses existing `NGIN::Unreachable()` under the hood (and/or compiler builtins) so switches optimize tightly.
   * `NGIN_LIKELY(x)` / `NGIN_UNLIKELY(x)`
   * `NGIN_FORCEINLINE`

### Placement & namespace

* Public headers live under `include/NGIN/Utilities/`.
* Namespace for Optional/Expected/Variant/Tuple: `NGIN::Utilities`.

### Type traits

Prefer `include/NGIN/Meta/TypeTraits.hpp` (and friends). These wrappers may internally use the standard library, but the value types should consistently use NGIN traits for readability and portability.

### Storage rule (core invariant)

* `StorageFor<T>` **never** stores state. Lifetime state is tracked by the wrapper.
* Every wrapper must explicitly define:

  * “alive state” variables (`bool engaged`, `uint32 index`, etc.)
  * when `StorageFor<T>::Construct/Destroy` is called
* `StorageFor<T>` is a raw aligned buffer with helpers (`Construct`, `Destroy`, `Ptr`, `Ref`):

   * It remains **state-free** (no “alive” flag) and its destructor does nothing.
   * It must support **trivial wrappers**:

      * If `T` is trivially copyable/movable, `StorageFor<T>` must be trivially copyable/movable too (defaulted special members).
      * If `T` is not trivially copyable/movable, `StorageFor<T>` must be non-copyable/non-movable to prevent accidental byte-copy of a live non-trivial object.

   This design keeps misuse hard while still enabling `Optional<T>` / `Expected<T,E>` / `Variant<Ts...>` to be trivial when they logically can be.

### Triviality preservation (required)

* Optional/Expected/Variant should remain trivially destructible/copyable/movable when the contained types allow it.
* Use partial specialization or `requires` to default special members where possible.
* Avoid user-defined destructors when `T` is trivially destructible.

Concretely:

* Prefer a `detail::OptionalStorage<T, /*TrivialDtor*/>` base (and same for `Expected` / `Variant`) where the trivially-destructible specialization uses `~Type() = default;`.
* For non-trivial destructors, provide a destructor that calls `Reset()` / `DestroyActive()`.

### Traits checklist (gate before implementation)

Ensure `NGIN/Meta/TypeTraits.hpp` provides:

* `IsTriviallyDestructible`, `IsTriviallyCopyConstructible`, `IsTriviallyMoveConstructible`
* `IsCopyConstructible`, `IsMoveConstructible`
* `IsCopyAssignable`, `IsMoveAssignable`
* `IsNothrow*` where you use `noexcept`
* `EnableIf` or `Requires` helpers used by the implementations

Additionally required for the best-solution design above:

* `IsTriviallyCopyable` (or equivalent)
* `IsTriviallyMoveConstructible` / `IsTriviallyMoveAssignable` where needed to select defaulted special members

---

# 1) Optional<T>

## Purpose

Inline “maybe a T”, no allocations. Minimal overhead. Fast access with checked/unsafe paths.

## Data layout

* `StorageFor<T> m_value;`
* `bool m_hasValue;`

Rationale: placing the flag after the aligned storage typically reduces internal padding when `T` has large alignment.

## Invariants

* `m_hasValue == true` ⇒ `m_value` contains a live `T`
* `m_hasValue == false` ⇒ no `T` alive in `m_value`

## API requirements (v1)

Construction:

* `Optional()` empty
* `Optional(nullopt)`
* `explicit Optional(const T&)`, `explicit Optional(T&&)`
* `InPlace` constructor: `Optional(InPlaceType<T>, args...)`

Observers:

* `bool HasValue() const`
* `explicit operator bool() const`
* `T& Value()` / `const T& Value()` (checked; assert/abort if empty)
* `T& ValueUnsafe()` / `const T& ValueUnsafe()`
* `T* Ptr()` / `const T* Ptr()` (nullable)
* Pointer-style sugar (unsafe/UB if empty, for perf/ergonomics):

   * `T& operator*()` / `const T& operator*() const`
   * `T* operator->()` / `const T* operator->() const`
* `const T& ValueOr(const T& fallback) const &` (zero-copy; fallback lifetime is caller-owned)
* `T ValueOr(T fallback) &&` (move-friendly for rvalues)

Modifiers:

* `void Reset()`
* `template<class...Args> T& Emplace(Args&&...)`
* `void Swap(Optional&)` (later)

## Supporting tag types

* Define `NGIN::Utilities::nullopt_t` and `inline constexpr nullopt` (do not use std).
* Define `NGIN::Utilities::InPlaceType<T>` and `NGIN::Utilities::InPlaceIndex<I>` tags (do not use std).

## Correctness requirements

* Copy/move ctor/assign must:

  * preserve optional-ness
  * destroy old value on assignment if needed
  * handle self-assignment safely
   * when both engaged:

      * if assignable: assign
      * else if constructible: destroy+reconstruct
      * else: disable the operator via `requires`
* Destructor must call `Reset()`

## Performance requirements

* `sizeof(Optional<T>)` should be `sizeof(T) + 1 + padding`
* No dynamic allocations
* `Reset()` should compile to nothing for trivially destructible `T`

## Implementation steps

1. Implement `Reset()`: if `m_hasValue`, `m_value.Destroy(); m_hasValue=false;`
2. Implement `Emplace()`:

   * `Reset()`
   * `m_value.Construct(...)`
   * `m_hasValue=true`
3. Implement copy/move ctor:

   * if `other.m_hasValue`: `m_value.Construct(other.ValueUnsafe())`; `m_hasValue=true`
4. Implement copy/move assignment:

   * cases:

   * both empty → no-op
   * this engaged, other empty → destroy this
   * this empty, other engaged → construct into this
   * both engaged → assign if assignable, else destroy+reconstruct if constructible, else disable
5. Add pointer/ref accessors (checked + unsafe)
6. Add `ValueOr` overloads for lvalue/rvalue qualifiers

---

# 2) Expected<T, E>

## Purpose

Return either a value or an error *inline*, no exceptions required. Great for engine subsystems.

## Data layout

* `StorageFor<T> m_value;`
* `StorageFor<E> m_error;`
* `bool m_hasValue;`

## Invariants

* `m_hasValue == true` ⇒ `m_value` alive, `m_error` not alive
* `m_hasValue == false` ⇒ `m_error` alive, `m_value` not alive

## API requirements (v1)

Types/helpers:

* `Unexpected<E>` helper wrapper for explicit error construction
* `InPlace` support

Construction:

* `Expected(T)` / `Expected(InPlaceType<T>, ...)`
* Error construction must be explicit and unambiguous:

   * `Expected(Unexpected<E>)` / `Expected(InPlaceType<E>, ...)` (error path)
   * Do not provide a raw-`E` constructor to avoid accidental selection of the error path.

Observers:

* `bool HasValue() const`, `explicit operator bool() const`
* `T& Value()` / `const T& Value()` (checked)
* `E& Error()` / `const E& Error()` (checked)
* `T& ValueUnsafe()`, `E& ErrorUnsafe()`
* `const T& ValueOr(const T& fallback) const &` (zero-copy; fallback lifetime is caller-owned)
* `T ValueOr(T fallback) &&` (move-friendly for rvalues)

Modifiers:

* `void EmplaceValue(args...)`
* `void EmplaceError(args...)`
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

   * value ctor sets `m_hasValue=true`, constructs `m_value`
   * error ctor sets `m_hasValue=false`, constructs `m_error`
3. Destructor:

   * if `m_hasValue`: `m_value.Destroy()`
   * else: `m_error.Destroy()`
4. Copy/move constructors:

   * branch on `other.m_hasValue` and construct matching storage
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

* `NGIN::Memory::AlignedBuffer<maxSize, maxAlign> m_storage;`
* `IndexType m_index;` (size chosen by number of alternatives)

Where `MaxSizedAlignedType` is a compile-time computed “largest” buffer type:

* Make `NGIN::Memory::AlignedBuffer<maxSize, maxAlign>`

## Invariants

* `m_index` is always a valid alternative index [0..N-1]
* the object stored in `m_storage` is exactly the type at `m_index`

## API requirements (v1)

Construction:

* default constructs alternative 0 (engine-friendly, never-valueless)
* `Variant(InPlaceIndex<I>, args...)`
* No implicit converting constructors in v1. Construction uses default alt-0 or explicit `InPlaceIndex`/`Emplace<I>`.

Observers:

* `IndexType Index() const`
* `bool HoldsAlternative<I>()`
* `T& Get<I>()` (checked) + `T* GetIf<I>()` (nullable)
* `T& GetUnsafe<I>()` (unsafe/UB if wrong alternative)
* `Visit(visitor)` (v1: switch-based)

Modifiers:

* `template<size_t I, class...Args> Emplace<I>(Args&&...)`
* `Swap` (later)

Semantics policy choices (recommend for engine)

* **Never valueless**
* Keep converting constructors minimal (prefer explicit `Emplace`)
* `Variant` is default-constructible iff `T0` is default-constructible

## Exception-enabled builds (required behavior)

* `Variant` must never become valueless.
* Any exception that occurs during construction of a new active alternative must result in `NGIN_ABORT(...)`.
   * This preserves the RT/engine invariant and avoids `valueless_by_exception`.

## Implementation steps

1. Metaprogramming utilities:

   * `TypeAt<I, Ts...>`
   * `MaxSize`, `MaxAlign`
   * `IndexType` selection:
     * `N <= 0xFF` ⇒ `uint8`
     * `N <= 0xFFFF` ⇒ `uint16`
     * else ⇒ `uint32`
2. Storage:

   * Store `AlignedBuffer` directly and placement-new into it.
3. Core operations:

    * Use an internal `Dispatch(m_index, lambda<I>)` helper that compiles down to a tight switch.
    * `DestroyActive()`:

       * `Dispatch(m_index, lambda<I>)` calls the destructor of `TypeAt<I>`.
       * After the switch, use `NGIN_UNREACHABLE()`.
    * `Construct<I>(args...)`:

       * In exception-enabled builds: wrap placement-new in `try/catch (...) { NGIN_ABORT(...) }`.
       * Placement-new into buffer as `TypeAt<I>`.
       * Set `m_index=I` only after successful construction.
4. Copy/move constructors:

   * `Dispatch(other.m_index, ...)` construct same type from other
5. Assignment:

   * If same `m_index`: assign active value (or destroy+reconstruct if not assignable but constructible)
   * Else: destroy current, construct new, update index
   * if required construction is not possible, disable the assignment operator via `requires`
6. Visit:

   * `Dispatch(m_index, ...)` call `visitor(Get<I>())`
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
* `ForwardAsTuple(...)` (later)
* `Tie(...)` (later)

## Implementation steps

1. Implement `TupleLeaf<I, T, UseEbo>`
2. Implement `TupleImpl<Indices..., Ts...> : TupleLeaf<I, Ts>...`
3. Implement `Tuple<Ts...> : TupleImpl<...>`
4. Implement `Get<I>` as free functions with correct cv/ref qualifiers (`Tuple&`, `const Tuple&`, `Tuple&&`, `const Tuple&&`).
5. Implement `Apply` using `std::index_sequence_for<Ts...>`

## Performance requirements

* Empty elements should not increase size (EBO)
* `Get` should be zero-cost

---

# Validation & testing plan (for all types)

## Test utilities (write first)

* `CountingType` (counts ctor/move/copy/dtor)
* `MoveOnlyType`
* `NonMovableType` (as-needed)
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
