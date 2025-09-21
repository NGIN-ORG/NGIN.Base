# NGIN.Base – CoPilot / LLM Contribution Instructions

Guidance for automated and human contributors to extend the library safely, consistently, and sustainably.

---

## Code Philosophy

High‑level principles that apply to every component:

1. Header‑only Core: Public functionality lives in headers; only dummy/example/test sources exist in `*.cpp`.
2. Modern Standard: Require C++23 (`cxx_std_23`). Prefer compile‑time (`constexpr`, `consteval`) where reasonable.
3. ABI / ODR Safety: Use inline / templates / `constexpr` in headers. Avoid non‑inline function definitions with static storage.
4. Determinism & Purity: Minimize hidden global state; prefer pure functions and stateless utilities.
5. Allocation Discipline: Avoid heap allocations in hot paths; rely on SBO / static storage / stack when feasible.
6. Exception Policy: Only throw standard exceptions. Favor `noexcept` APIs when invariants are enforceable. Use assertions for programmer errors.
7. Zero Surprise: Follow existing patterns before inventing new abstractions; keep APIs minimal, orthogonal, and composable.
8. Evolvability: Prefer adding over mutating breaking semantics. Mark experimental pieces clearly (comments or separate headers like `Units2.hpp`).

---

## Code Style & Namespacing

Namespaces:

- All code resides under `NGIN` root. Sub‑namespaces reflect domains (e.g. `NGIN::Utilities`, `NGIN::Units`, `NGIN::Async`, `NGIN::Meta`, `NGIN::Containers`).
- No anonymous namespaces in headers. Use `detail` nested namespace for implementation internals: `namespace NGIN::Utilities::detail { ... }`.

Formatting & Layout:

- Enforce repository `.clang-format`; re-run after edits.
- 4 spaces, no tabs. Keep lines ≤ ~120 chars when practical.
- Opening brace on same line: `constexpr auto Foo() {` / `class Bar {`.
- Trailing commas for multiline enums/initializers encouraged when clang-format permits.

Keyword & Qualifier Use:

- Prefer `constexpr` / `consteval` / `constinit` when semantically correct.
- Use `noexcept` when implementation is observably non‑throwing; do not over‑promise.
- Use `final` / `override` explicitly for derived virtuals (rare here).

Naming Conventions:

- Types / templates: `PascalCase` (e.g. `LinearAllocator`, `ConcurrentHashMap`).
- Functions / methods: `PascalCase` (e.g. `ComputeHash`, `UpdateState`).
- Member data prefix: `m_`  (e.g `m_foo`, `m_someData`)
- Static data prefix: `s_camelCase` (e.g `s_foo`, `s_someData`).
- Local / function-scope variables: `camelCase`.
- Template parameters: `PascalCase` short (e.g. `class T`, `typename Alloc`).
- Concepts / traits: suffix with `Concept` / `Traits` where clarity improves (e.g. `FunctionTraits`).
- Constants: `ALL_CAPS` only for macro guards.

Type Usage:

- Use `auto` when the type is obvious from the right-hand side or prevents repetition.
- Avoid implicit narrowing; use explicit `static_cast`.
- Prefer uniform initialization.

---

## Dependency Policy

Production Code:

- Standard library only. No third‑party runtime dependencies added without prior approval.

Tests / Tooling:

- May use Catch2 and other explicitly approved test-only or benchmark libraries.
- Benchmarks should remain isolated under `benchmarks/` and not leak dependencies into public headers.

---

## Component‑Specific Guidelines (Extensible)

This document intentionally avoids deep per‑component details to reduce staleness. Each module can supply a focused `README.md` placed alongside its headers, e.g.:

```
include/NGIN/Units/README.md
include/NGIN/Async/README.md
```

Template for a component README:

1. Purpose & Scope
2. Key Types / Concepts
3. Usage Examples (concise)
4. Performance Notes
5. Extension Points & Invariants
6. Testing Guidance (specific edge cases)

If a component lacks a README, follow general patterns outlined here and inspect similar modules for precedent.

---

## Verification

Goals: correctness, safety, performance regressions caught early.

Tests:

1. Every new feature: at least one positive (success) and one negative (failure / defensive) test.
2. Use existing Catch2 style:

   ```cpp
   TEST_CASE("scenario") {
       // REQUIRE/SECTION assertions
   }
   ```

3. Cover: invariants, boundary conditions (empty, moved-from, max size, alignment), exception or error pathways.
4. Avoid relying on unspecified order or undefined behavior.

Benchmarks:

- For measurable performance-sensitive additions add/extend benchmarks in `benchmarks/` naming them descriptively (`SchedulerBenchmarks.cpp`, etc.).
- Keep benchmarks isolated from test-only headers.

Static & Dynamic Analysis (recommended before PR merge):

- `clang-tidy` on changed files (respect local suppression comments only when justified).
- Sanitizers (ASan, UBSan) in a debug build for new low-level logic.
- Optional: compile with `-Wall -Wextra -Wpedantic` (or MSVC equivalents) and zero new warnings.

Checklist Summary:

- [ ] Positive test
- [ ] Negative test
- [ ] Benchmark (if perf critical)
- [ ] Static analysis clean
- [ ] Sanitizers pass (where applicable)

---

## Performance & Quality

- Prefer compile-time evaluation when it doesn't harm clarity.
- Avoid dynamic allocation in tight loops; exploit SBO or stack arrays.
- Minimize branching depth; use constexpr dispatch / type traits for compile-time selection.
- Keep trivial functions defined in-class (implicit inline).
- Guard `noexcept` correctness—removal requires justification in PR notes.

---

## Documentation / Comments

- Provide concise doxygen-style summaries on public classes / templates / free functions.
- Use `detail` namespaces instead of comments for scoping internals.
- Avoid redundant comments (e.g., `// increment i`). Focus on rationale and invariants.

---

## Patterns to Follow / Avoid (Cheat Sheet)

Follow:

- Consistent value construction: `constexpr explicit Type(T v) noexcept : value_(v) {}`.
- Policy / traits indirection for customization instead of conditionals.
- Conversion pipeline: source policy → base → target policy → target cast.
- Reuse helper functions (`copyFrom`, `moveFrom`) to unify special member logic.
- Use `detail` namespace + `[[nodiscard]]` and strong types to prevent misuse.
- #pragma once

Avoid:

- Raw `new` / `delete` outside controlled abstractions.
- RTTI for core logic; prefer traits / concepts.
- Global singletons / hidden mutable state.
- Logic macros (macros only for include guards / platform detection).
- Silent narrowing or unchecked casts.

---

## Adding New Features Checklist (Generic)

1. Correct Namespace & Header Placement (public headers under `include/NGIN/...`).
2. Minimize Includes (forward declare where possible; keep public interface lean).
3. Tests: positive + negative; property / edge coverage where relevant.
4. Benchmarks (if performance-sensitive or replacing tuned code).
5. Formatting & Static Analysis (clang-format, clang-tidy, zero new warnings).
6. Public API Documentation & inline rationale for non-obvious decisions.
7. Examples or README update for discoverability (if user-facing abstraction).
8. Confirm `noexcept` correctness and exception safety guarantees.
9. Ensure no unintended dependency creep.

---

## Commit / PR Guidance

Title: Short imperative (e.g., "Add units conversion ratio helper").
Body Outline:

- Motivation / Problem
- Solution Summary (key types & algorithms)
- Tests & Verification Steps (mention benchmarks if added)
- Potential Follow-ups / Limitations

---

## AI & Automated Suggestions

- Do not remove existing `noexcept` or strengthen exception specs without proof.
- Do not reorder public struct/class members if layout might be observed.
- Preserve semantic behavior unless PR explicitly states an intentional change.
- Keep diffs minimal; avoid opportunistic refactors mixed with feature changes.
- Prefer introducing helpers over duplicating complex logic.

---

## License

Apache 2.0 (see `LICENSE`). Preserve license headers and attribution where present.

---

Adhere to these guidelines for consistent, safe, and maintainable contributions.
