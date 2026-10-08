# NGIN.Base Agent Guide

NGIN.Base is the C++23 foundation library for NGIN: primitives, containers,
memory, async/execution, I/O, serialization, crypto, and networking. It is
consumed by the NGIN workspace through `Packages/NGIN.Base` and is also a
standalone repository (a git submodule of NGIN).

When working inside the NGIN workspace, the root `AGENTS.md` also applies.

## Source of Truth

- Public headers under `include/NGIN/` are the API contract.
- `docs/README.md` indexes per-area docs; some areas also have a README beside
  their headers (e.g. `include/NGIN/Execution/README.md`). Read the relevant one
  before changing an area.
- `docs/Components.md` defines the eight compiled components (Foundation,
  Execution, IORuntime, IO, Serialization, Crypto, Net, NetTLS) and their
  dependency direction.
- `cmake/NGINBaseComponentHeaders.cmake` assigns every installed header to
  exactly one component.

## Structural Rules

- Every new public header must be added to `cmake/NGINBaseComponentHeaders.cmake`
  under its owning component; unowned or doubly-owned headers fail configuration.
- Respect component dependency direction. Foundation depends on no other
  component; never add an upward dependency (e.g. Foundation → IO).
- Public headers must compile standalone (checked by `NGINBasePublicHeaderChecks`).
- Implementation internals go in a `detail` namespace / lowercase `detail/`
  directory. No anonymous namespaces in headers.
- Non-trivial or platform-specific implementations go in `src/`; templates and
  `constexpr` code stay in headers. Export non-inline symbols with the owning
  component's macro from `NGIN/Defines.hpp` (`NGIN_FOUNDATION_API`,
  `NGIN_IO_API`, …).
- Use `#pragma once`.

## Dependencies

Production code uses the standard library plus the optional crypto/TLS providers
already wired in `cmake/NGINBaseSources.cmake` (OpenSSL, BoringSSL, libsodium).
Do not add new third-party dependencies without approval. Tests use Catch2 via
CPM; benchmarks live in `benchmarks/` and must not leak into public headers.

## Code Style

`.clang-format` and `.clang-tidy` in this directory are authoritative. Notably,
braces for classes and functions go on their own line, and there is no column
limit — don't hand-wrap against one.

- Types, functions, template parameters: `PascalCase`.
- Members `m_camelCase`, statics `s_camelCase`, locals `camelCase`.
- `ALL_CAPS` only for macros.
- Prefer explicit short domain types (`Byte`, `UIntSize`, handles, enums); use
  `auto` for iterators, lambdas, structured bindings, and unwieldy dependent types.
- No implicit narrowing; use `static_cast`.

## API Design

- Report recoverable failures with `NGIN::Utilities::Expected`; use the NGIN
  exception hierarchy (`NGIN/Exceptions/`) where an API throws; assert on
  programmer errors.
- Mark `noexcept` only when the implementation genuinely cannot throw. Don't
  add or remove `noexcept` on existing APIs without justification.
- Avoid heap allocation in hot paths; respect allocator-aware types.
- No hidden global mutable state or singletons; no raw `new`/`delete` outside
  owning abstractions; no RTTI for core logic.
- Don't reorder public struct/class members whose layout may be observed.
- Public headers get `@file`/`@brief`; document ownership, invalidation, error,
  and thread-safety rules for non-trivial public APIs. Comment rationale, not mechanics.

## Verification

```bash
cmake --preset tests
cmake --build --preset tests-debug
ctest --test-dir build/tests -C Debug --output-on-failure
```

Sanitizer and LTO variants: configure `tests-asan` / `tests-tsan` / `tests-lto`,
build with the matching `*-build` preset, and run `ctest -C <config>` in that tree.

- New behavior needs a success-path and a failure/edge-case test (empty,
  moved-from, max size, alignment, error paths) under the matching `tests/<Area>/`.
- Performance-sensitive changes should add or update a benchmark in `benchmarks/`.
- Run ASan/UBSan (`tests-asan`) for new low-level memory or concurrency logic.
- When the change affects NGIN consumers, also verify from the workspace per
  the root `AGENTS.md`.
