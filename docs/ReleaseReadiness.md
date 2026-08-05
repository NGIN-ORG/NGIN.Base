# Release-readiness audit

Last updated: 2026-08-05.

This page records evidence for the completion plan and distinguishes local
verification from hosted workflow execution.

## Error and cancellation consistency

| Area | Domain failure | Cancellation/timeout contract |
| --- | --- | --- |
| Async | `Completion` domain error or fault | cancellation is a distinct terminal state |
| IO/filesystem | `IOError` | async filesystem cancellation maps to `IOErrorCode::Canceled` at the operation boundary |
| Process | `ProcessError` for start/wait/stream failures | timeout and requested cancellation are child outcomes on `ProcessResult`, distinct from exit code/signal |
| Net sockets | `NetError` | coroutine cancellation remains an Async terminal state |
| Resolver | `ResolveError` preserves native status | async timeout/cancellation can return before the owned resolver worker exits |
| Serialization | `ParseDiagnostic` / `WriteDiagnostic` | no asynchronous operation; handler rejection is a typed diagnostic |
| Crypto | `CryptoError` and capability diagnostics | synchronous provider operations; no cancellation contract |
| TLS | `TlsError` preserves provider, protocol, certificate, hostname, ALPN, transport, state, timeout, and cancellation categories | caller cancellation and handshake timeout remain distinct typed outcomes |

The TLS API retains caller cancellation as an operation outcome while using a
distinct timeout/category diagnostic, so unrelated failure classes do not
collapse into one code.

## Verification evidence

- Configure-time ownership and dependency-direction validation: 263 installed
  headers, 261 standalone public contracts, zero forbidden public-header edges.
- Windows independent public-header matrix and six focused umbrella tests pass.
- Windows incremental JSON/XML suite: 1,872 assertions in five test cases.
- Windows allocator suite: 56 assertions in eight test cases.
- Linux incremental and allocator suites pass with the same assertion counts.
- Filesystem behavior suites pass on Windows (local: 183 assertions/11 cases;
  VFS: 105/6) and Linux (local: 282/14; VFS: 105/6).
- Windows static-only and shared-only builds produce all six component
  libraries. Build-tree and installed external-consumer matrices link and run
  the aggregate plus all six preferred component targets for both linkage
  forms.
- Linux combined static/shared builds produce all twelve component libraries;
  the installed seven-consumer matrix configures, links, and runs against an
  empty-prefix installation.
- OpenSSL 3 crypto configuration satisfies all required provider capabilities;
  62 focused provider/include tests pass.
- The OpenSSL 3 TLS provider passes 190 assertions across trusted fragmented
  transport, SNI, hostname verification, ALPN, mutual authentication,
  cancellation, timeout, clean shutdown, and truncated-EOF scenarios. The
  provider-disabled Windows contract passes independently.
- The Windows CLI target rebuilds after migration to the shared Process API;
  the direct, shell-free process facade test passes with two assertions.
- The Windows Release components and all 261 standalone public-header contract
  translation units rebuild after the final warning cleanup.
- GitHub workflow files pass YAML lint and define Windows/MSVC, Linux/GCC,
  Linux/Clang, macOS/AppleClang, ASan/UBSan, TSAN, SSE2, AVX2, native ARM64,
  provider, fuzz, shared-install, and external-consumer jobs.
- Allocator and concurrent-reclamation Release baselines are recorded in
  [Baseline](Baseline.md).

## Documentation-example review

Public examples were checked against the current narrow headers and type names.
The aggregate external consumer compiles the focused Net and Serialization
umbrellas, while six focused consumers compile and execute one public API from
each component. Focused tests compile the Foundation, Execution, IO, Net,
Serialization, and Crypto umbrellas.
Behavioral documentation examples remain backed by their matching focused test
areas (`Async`, `Sync`, `IO`, `Process`, `Net`, `Serialization`, and `Crypto`).

## Pending release gate

- Execution of the newly expanded hosted CI matrix on the published branch.

The target-name, dependency-propagation, and component-visibility review is
complete. Windows shared builds currently retain automatic symbol export as a
compatibility bridge; removing it is a later ABI-hardening task, not a blocker
for the canonical component package contract.
