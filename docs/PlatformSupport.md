# Platform support and known limitations

NGIN.Base targets Windows, Linux, and macOS with C++23 compilers. The supported
CI compiler set is MSVC, GCC, Clang, and AppleClang. Native ARM64 Linux CI
validates the NEON-capable SIMD path; x64 CI separately builds SSE2 and AVX2
configurations.

## Files and processes

- Race-free no-replace rename and durable replacement are capability-reported.
  A filesystem/backend that cannot provide a requested guarantee returns a
  typed error rather than silently weakening it.
- Windows symbolic-link creation may require developer mode or elevated
  privileges. Tests skip creation-dependent cases only when the operating
  system denies that privilege; reading existing links and junctions remains
  supported.
- Cross-mount VFS move is copy-then-delete. It is not globally atomic, but the
  source is deleted only after the complete copy succeeds.
- POSIX process isolation uses process groups; Windows uses job objects. Some
  inherited or externally reassigned descendants cannot be controlled by the
  original parent after operating-system ownership changes.
- Direct process execution never invokes a shell. Shell syntax requires an
  explicit shell executable and argument list.

## Network and resolution

- Synchronous `getaddrinfo` cannot be portably interrupted once entered.
  Timeout and cancellation with early return are provided by `ResolveAsync`
  through an explicitly owned `ResolverDriver`; the driver owns its worker
  until the native call exits.
- Resolver ordering is the platform's order. Equivalent duplicate records are
  removed without claiming a portable sort order.
- Numeric IPv6 scope identifiers are supported. Interface-name scopes require
  an OS lookup and are intentionally not accepted by pure endpoint parsing.
- Non-Windows async sockets currently use readiness polling; Windows uses IOCP.
- TLS is not part of the default build. The provider-neutral API is available
  on every platform and returns `ProviderUnavailable` when no implementation
  was compiled. The OpenSSL 3 provider is enabled explicitly with
  `NGIN_BASE_TLS_WITH_OPENSSL`; requiring `openssl` makes absence a configure
  error. Provider-enabled loopback coverage currently runs on Linux, while the
  provider-disabled contract is also verified on Windows.

## Crypto providers

- Platform providers differ by OS and capability. Algorithms must be queried or
  required at configure/context creation time.
- OpenSSL, BoringSSL, and libsodium are optional package providers. Missing
  required providers or algorithms fail configuration; optional absence is
  reported by capability diagnostics.
- The OpenSSL-compatible crypto implementation shares an EC compatibility path
  with BoringSSL. OpenSSL 3 deprecation annotations for that path are suppressed
  until provider-specific implementations are separated; behavior is covered by
  provider conformance vectors.

## Linkage and packaging

- The current aggregate static and shared package forms are usable on Linux;
  an installed shared-library consumer is exercised in CI.
- Independently compiled component archives/shared libraries, build-tree
  package exports, and Windows component visibility are pending the explicit
  component-migration approval gate. Metadata-only component targets are not
  presented as independently linkable binaries.
