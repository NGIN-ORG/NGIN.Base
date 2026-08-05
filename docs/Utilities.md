# Utilities and typed errors

`NGIN::Utilities` provides small, non-domain-specific value tools:

- `Expected<T, E>` for explicit success or failure
- `Optional<T>` for an optional value without a domain error
- `Any` and `Callable` for type-erased ownership and invocation
- `StringInterner` and `SymbolTable` for stable symbolic lookup
- `ErrorInfo` for lightweight cross-boundary domain, code, and native-code
  metadata

Prefer a subsystem's typed error (`IOError`, `NetError`, or
`ParseDiagnostic`) at public domain boundaries. `ErrorInfo` is useful for
generic reporting and transport, but should not erase structured information
while the caller can still act on it.

`Expected` does not imply exception suppression: user callbacks and allocation
may still throw where the called API documents that behavior. Inspect the
function's exception specification rather than assuming every expected-returning
function is `noexcept`.
