# Migration Plan: Reflection Utilities → NGIN.Base

## Objectives

- Graduate reusable reflection runtime primitives (type-erased storage, string interning, name helpers, adapters) into NGIN.Base while honoring base library guidelines.
- Provide a clear path for module placement, API surface adjustments, and verification so downstream repos can adopt the new facilities without breaking changes.
- Identify quality-of-life improvements that make the migrated utilities broadly useful beyond the reflection registry.

## Guardrails & Prereqs

- Follow header-only/public-header rule; any `.cpp` remains for tests/examples only.
- Require C++23 features already enabled in NGIN.Base (constexpr-if, `std::expected`, `std::pmr` interop where applicable).
- Align with existing allocator concepts in `NGIN::Memory` and container patterns in `NGIN::Containers`.
- Ensure no hidden coupling to the reflection registry (no global registries, RTTI, or static non-inline state).

## Migration Targets & High-Level Notes

1. **Type-Erased Box (`Reflection::Any`)** – move to `NGIN::Utilities::Any`, keep SBO, allocator override, hash support via `Meta::TypeName`.
2. **String Interner (`detail::StringInterner`)** – promote to public header (likely `NGIN::Utilities::StringInterner`) with allocator configurability and thread-safety consideration.
3. **Name Extraction Helpers (`MemberNameFromPretty`)** – fold into `NGIN::Meta` alongside `TypeName`, ensure constexpr coverage and compilers support.
4. **Container Adapters (`Reflection::Adapters`)** – relocate to `NGIN::Utilities::AnyAdapters` once `Any` lands; broaden adapter set to cover core NGIN containers and std types.

## Detailed Workstreams

### 1. Establish `NGIN::Utilities::Any`

- Audit the Reflection implementation: document layout (SBO size, alignment guarantees, allocator fallbacks) and identify references to registry internals.
- Port into `include/NGIN/Utilities/Any.hpp`; expose doxygen docs, inline implementation, and detail namespace for helpers (vtable, storage traits).
- Replace `Meta::TypeName` usage with existing `NGIN::Meta::TypeName` utilities (confirm hashing strategy; consider providing a customizable hash functor instead of hard-wiring type names).
- Introduce concepts/traits for detecting trivially relocatable types to maximize constexpr/noexcept coverage.
- Provide conversion helpers (e.g., `TryCast`, `Visit`) consistent with Base naming and exception policy; ensure defensive checks throw `std::bad_any_cast` or `std::logic_error` equivalents already used in Base.
- Tests: create `tests/Utilities/Any.cpp` with Catch2 test cases covering SBO hit/miss, allocator override, const/volatile qualification, move-only types, visiting, and error cases.

### 2. Promote `StringInterner`

- Extract logic into `include/NGIN/Utilities/StringInterner.hpp`, wrapping mutable interned storage with allocator template parameter (default `Memory::SystemAllocator`).
- Align stored string type with `NGIN::Containers::String` (or `BasicString`) to avoid redundant string implementations; explore accepting `std::string_view` inputs without allocation when already interned.
- Evaluate thread-safety: document default (likely single-threaded). If multi-threading is desired, add policy-based locking or provide separate `ThreadSafeStringInterner` wrapper.
- Add instrumentation hooks (debug assertions, optional capacity stats) under `detail` utilities without runtime overhead when disabled.
- Tests: `tests/Utilities/StringInterner.cpp` verifying deduplication, allocator substitution, move semantics, and negative cases (e.g., exhausting capacity, invalid deintern attempts).

### 3. Relocate Name Utilities

- Create `include/NGIN/Meta/NameUtils.hpp` or extend existing `TypeName.hpp` with a documented `ExtractMemberName` API; ensure implementation relies solely on compiler pretty function strings handled via constexpr parsing.
- Expand compiler coverage (GCC/Clang/MSVC) with dedicated tests using `static_assert` and Catch2 checks.
- Provide fallback for unsupported compilers (returning original pretty string) with clear documentation.
- Verify interaction with existing `Meta::TypeId` and `TypeName` to avoid duplicated code paths.
- Tests: augment existing meta tests or add `tests/Meta/NameUtils.cpp` verifying member-function name extraction, lambdas, and edge cases (templates, overloaded operators).

### 4. Rehome Any Adapters

- After `Any` is in Base, port adapter templates into `include/NGIN/Utilities/AnyAdapters.hpp` (may live in `detail` nested namespace with public entry points).
- Normalize adapter naming to Base conventions (e.g., `AdaptIterable`, `AdaptOptional`, `AdaptVariant`). Consider concepts (`IterableConcept`) to guard template participation.
- Support `std::optional`, `NGIN::Containers::Optional` (if present), `std::variant`, and tuple-like types; ensure adapters do not bring in heavy headers unnecessarily (use forward declarations and lazy includes where possible).
- Provide bridging utilities for visiting containers (e.g., `ForEachElement`) that work directly with `Any` storage.
- Tests: `tests/Utilities/AnyAdapters.cpp` covering conversion of std/NGIN containers, error reporting when types unsupported, and verifying adapter composition.

## Cross-Cutting Tasks

- Update `include/NGIN/NGIN.hpp` aggregator to expose new headers when stable.
- Add component README stubs (`include/NGIN/Utilities/README.md` section update) describing new utilities and usage snippets.
- Ensure `CMakeLists.txt` install/export sets include new headers (header-only, but ensure packaging).
- Run `.clang-format` across new headers/tests; add clang-tidy configuration notes if specialized suppressions are required.
- Review allocator + exception guarantees; annotate `noexcept` accurately and add assertions for programmer errors (e.g., invalid casts).

## Implementation Phasing

1. **Analysis & Design Notes** – capture current Reflection behavior, confirm allocator + type trait expectations (no code changes yet).
2. **Introduce `Any` + Tests** – land core type, include documentation, adjust CMake, add unit coverage.
3. **Add String Interner** – ensure independence from reflection data; include tests and optional locking decisions.
4. **Move Name Utilities** – update meta headers/tests; ensure reflection repo can include new location without breakage.
5. **Publish Adapters** – rely on the new `Any`; adjust includes in reflection repo to consume from Base.
6. **Integration Cleanup** – update aggregator headers, README, and ensure reflection module compiles against revised includes.

## Analysis Notes (2025-02-14)

- **Reflection::Any** currently defines a 32-byte SBO with `SystemAllocator` fallback, hashes types using `Meta::TypeName` + FNV1a64, and exposes `As<T>` without runtime checks. Copy/move rely on stored function pointers; heap stores alignment alongside pointer. Destruction path always consults optional dtor pointer before optionally freeing heap memory.
- **detail::StringInterner** manages geometrically growing `Page` buffers (min 4 KiB), tracks entries with FNV hash buckets implemented via `FlatHashMap` of vectors, and returns stable `std::string_view` lifetimes; allocator usage is limited to `SystemAllocator`. No synchronization or allocator customization hooks exist.
- **Utilities::StringInterner** now lives in Base with allocator + thread-policy template parameters, page-backed storage, `InsertOrGet`/`TryGetId`/`Intern` helpers, and built-in statistics counters so subsystems can opt into locking and instrumentation.
- **detail::MemberNameFromPretty** extracts identifiers using compiler-specific pretty-function strings, trimming the class qualifier. Output is a `consteval std::string_view` suitable for compile-time member metadata.
- **Adapters namespace** offers detection traits and thin wrappers for sequence, tuple, variant, optional, and map-like types, building on `Reflection::Any` and `ConvertAny`. Traits are ad-hoc; adapter APIs return `Any` boxes and rely on reflection detail conversions to round-trip keys/values.
- Cross-cutting dependencies include `Hashing/FNV`, `Containers::Vector`/`FlatHashMap`, and reflection-specific `ConvertAny` logic; allocator interactions assume `Memory::SystemAllocator` and there are no `noexcept` guarantees documented today.

## Verification Strategy

- Unit tests per feature (positive + negative) under `tests/Utilities/` and `tests/Meta/` with Catch2.
- Build matrix: standard Debug/Release on Clang & GCC; ensure MSVC builds if in CI.
- Optional benchmarks (place under `benchmarks/Utilities/`) for `Any` SBO performance vs. std::any and for interner lookup.
- Consider ASan/UBSan runs for `Any` storage correctness and interner memory handling.

## Follow-Up Items for NGIN.Reflection

- Replace internal includes with new Base headers; add migration notes in that repo to avoid duplicate definitions.
- Remove deprecated code after a compatibility window; optionally provide shim headers forwarding to Base versions.
- Evaluate additional reflection utilities for future migration once these land successfully.
