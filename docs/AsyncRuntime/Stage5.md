# Stage 5 — final evidence and consumer completion

## T22 Linux correctness and installed consumers — 2026-09-13

Built the 59 repository-defined Async/Execution/IO/Net test targets together with
NGINBasePublicHeaderChecks and the release fail-fast fixture in the existing
Ninja Multi-Config Debug tree. All 413 scheduled build actions succeeded without
compiler warnings/errors. Public headers were compiled as independent translation
units. Source was held fixed across builds and runs, and the manifest was checked
again afterward.

Configuration: GCC 15.2.0, x86_64 Linux 6.18.33.2-microsoft-standard-WSL2,
static components, Debug. Base HEAD b44612eb896ab013463876e22a40e221a4fd8c5f plus
the working-tree implementation. The 711-file source/configuration/test manifest
has SHA-256 06b056b63a72e7df5a971ddfb92fe31a04f94423a2fd08e8029c09eadab13a3f.
Exact manifest, selected build options, and compiler/platform identity are saved
alongside logs. This fingerprint identifies authored implementation/tests/config,
not generated build output or subsequent documentation edits.

Executed the canonical CTest selection:

```sh
ctest --test-dir Dependencies/NGIN/NGIN.Base/build/tests -C Debug \
  -R '^Base\.(Async|Execution|IO|Net)\.' --output-on-failure --timeout 60
```

472 registered checks: 466 passed, six skipped, zero failed. Skips were the explicit
Windows IOCP placeholder and five OpenSSL-disabled TLS cases. The selection includes
the portable loop, controlled concurrency/lifetime cases, bounded admission, and
the NDEBUG fail-fast subprocess checks. Windows-only code compiled out by platform
guards is not native verification.

Rebuilt Net_TLSTests separately in the existing tests-tls Debug tree with
NGIN_BASE_TLS_WITH_OPENSSL=ON, then executed it: six cases / 190 assertions passed,
no skips. This supplies TLS-enabled Linux coverage missing from the main configuration.

The canonical installed IO, Net, and IORuntime consumer checks all passed, using
CXX=/opt/gcc-15.2/bin/c++ and CMAKE_BUILD_PARALLEL_LEVEL=2. These checks rebuilt and
installed each selected component and compiled/executed consumers outside the
source package. They do not establish an unrun shared-library matrix.

Evidence: build/async-runtime-evidence/t22-linux-{build,tests}.log,
t22-installed-consumers.log, t22-tls-{build,tests}.log, t22-source-manifest.sha256,
t22-configuration.json, t22-build-options.txt, and t22-targets.txt.

T22 remains open for the final ASan/UBSan batch against the latest stop-origin and
observer-saturation additions and the canonical Hello.IO execution modes. T23–T27
remain open; these correctness results do not establish performance or native
Windows/macOS acceptance. No active build/test process remains from this checkpoint.

## T22 completion — 2026-09-29

T22 is complete locally. The remaining sanitizer and Hello.IO checks passed without
production or test changes. Before and after execution, `sha256sum --check --quiet
build/async-runtime-evidence/t22-source-manifest.sha256` verified every entry in the
711-file source manifest from the checkpoint above. Its SHA-256 remains
`06b056b63a72e7df5a971ddfb92fe31a04f94423a2fd08e8029c09eadab13a3f`; Base HEAD remains
`b44612eb896ab013463876e22a40e221a4fd8c5f` plus the recorded working-tree implementation.
The existing Debug, TLS-enabled, independent-header, and installed-consumer evidence
therefore still applies and was not rerun.

The existing `build/tests-asan` tree uses GCC 15.2.0 on x86_64 Linux
6.18.33.2-microsoft-standard-WSL2, RelWithDebInfo, static components, and
`-fsanitize=address,undefined -fno-omit-frame-pointer` for library and test code.
Both `NGIN_BASE_ENABLE_ASAN` and `NGIN_BASE_TEST_ENABLE_ASAN` are ON; OpenSSL is OFF.
The build of all 59 targets listed in `t22-targets.txt` plus
`NGINBaseScopeLifetimeFixture` succeeded and reported no work needed. No
reconfiguration was necessary. Exact expanded build command: `t22-final-asan-command.txt`.

From the Base directory:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/tests-asan -C RelWithDebInfo \
  -R '^Base\.(Async|Execution|IO|Net)\.' --output-on-failure --timeout 60
```

Result: **466 passed, six skipped, zero failed** in 12.33 seconds, with no reported
sanitizer errors. The six skips are the Windows IOCP placeholder and five
OpenSSL-disabled TLS integration cases, as in the Debug checkpoint. TLS-enabled
Linux coverage remains the six previously recorded Debug passes; this run does
not add TLS-enabled sanitizer coverage. The executed selection includes the final
stop-reason propagation and observer-capacity exhaustion tests, controlled
publication/cancellation races, bounded child-lifetime stress, portable-loop checks,
and release fail-fast fixture. Sanitizers do not establish absence of data races.

From the repository root, all four canonical commands returned zero:

```sh
./build/dev/Tools/NGIN.CLI/ngin validate --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug
./build/dev/Tools/NGIN.CLI/ngin build --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
./build/dev/Tools/NGIN.CLI/ngin run --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
./build/dev/Tools/NGIN.CLI/ngin test --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
```

Both run and smoke-test output confirm manual RunTask, background RuntimeRunner,
and external executor modes completed with all tasks joined. Each mode verified
file write/copy/readback through io_uring, loopback TCP echo, and cancellation/join
of a silent listener. Background shutdown joined its pending accept; external
analysis verified worker placement. The existing example build was up to date.

Evidence under Base's `build/async-runtime-evidence/`:

- `t22-final-asan-{build,tests}.log` and `t22-final-asan-command.txt`.
- `t22-hello-{validate,build,run,test}.log`.
- `t22-final-configuration.json` and `t22-final-inputs.sha256` (example inputs,
  CLI binary, and sanitizer cache; paths relative to the repository root).

No failures required implementation changes. T23 performance acceptance, T24 final
documentation/consumer cleanup, T25/T26 native Windows/macOS verification, and T27
requirements closure remain open. No shared-library matrix or native-platform
acceptance is inferred from these Linux results.

## T23 measurement result — 2026-09-29

[Performance.md](Performance.md) records the matched original/checkpoint/redesign
Release runs, structural and overload measurements, and exact evidence. All 63
processes completed successfully, but file, socket, and some root latency/cold-setup
gates remain unmet. T23 stays open; correctness success and deep-chain improvements
do not close performance acceptance. Production implementation and thresholds were
unchanged; the three I/O timing harnesses gained identical per-case warmup on all
versions. T22's production-source verification remains valid.
