# Stage 2 — Cancellation and terminal results

## T10 cancellation boundaries — 2026-09-13

Ordinary Task awaits inherit AsyncEnvironment and create no source, link, or registration. The TaskContext
suspension boundary registers only when its token has state; empty-token checks short-circuit before touching
cancellation locks/atomics or allocating cancellation state. Existing token-free wait machinery is execution
and delivery bookkeeping, not cancellation state. Callback Reset still joins concurrent invocation and
supports callback-initiated unregister using stable nodes; that protocol was preserved.

The boundary audit found and removed two redundant costs:

- Token equality compares state identity. TaskContext linking to its current token now preserves the existing
  boundary, as linking an empty token already did. Explicit linked sources skip empty and duplicate parents.
- WhenAny now owns one linked source per competitor and passes its token through the child context. It no
  longer creates a separate loser source plus a second source linking that source to the parent.

TaskScope already owns one linked source shared by its descendants. Socket/TLS explicit-token composition
is an operation boundary, not a sequential helper cost. Source ownership and context/link lifetimes remain
explicit. Normal spawn ownership, structured closure, and deadline policy are still later plan work; this
checkpoint does not claim those requirements are complete.

Verification:

- Rebuilt Cancellation, TaskContext, WhenAny, TaskScope, and Environment in the existing Debug tree:
  **52 tests passed** with `ctest --test-dir build/tests -C Debug --output-on-failure --timeout 30
  -R '^Base.Async.(Cancellation|TaskContext|WhenAny|TaskScope|Environment).'`.
  Log: `build/async-runtime-evidence/cancellation-boundaries-tests-final.txt`.
- The same **52 passed under ASan/UBSan** in `build/tests-asan`, RelWithDebInfo.
  Log: `build/async-runtime-evidence/cancellation-boundaries-asan-tests-final.txt`.
- New depth-1/8/64 fixtures observe zero registration-resource allocations without suspension and exactly
  one node allocation at the pending leaf; the source's table was preallocated before the measured interval.
  Cancellation drains the leaf, preserves already-completed success, and removes its timer at every depth.
- A failure-armed registration resource verifies 64 identical-token context links introduce no callback or
  new cancellation boundary. Duplicate explicit parents produce one node. Parent cancellation reaches both
  pending WhenAny competitors and drains their timers. Existing unregister, rejection, self-reset, and
  concurrent scope/race tests remain passing.
- Initial verification exposed a test expectation error (the source preallocates its registration table,
  so the pending node adds one allocation, not two) and an unsupported default shared-pointer equality.
  Both were corrected; initial failed logs remain separate. Final diff review/check passed.

T10 is complete locally. T11 must replace the four terminal categories with Value/Error/Stopped and migrate
all consumers together; T12 supplies the resulting publication/retirement race gate. No broad suite or
benchmark was repeated for this batch.

## T11 terminal-channel migration — 2026-09-13

CompletionKind now has exactly Value/Error/Stopped; TaskStatus adds only Pending. Completion exposes
Error() and StopReason(), while Task/Operation use HasError()/IsStopped(). The old IsFaulted/IsCanceled
predicates and four-channel enum values are removed from authored consumers. DomainFailure/Faulted remain
payload construction helpers for the single Error channel, not separate terminal categories.

AsyncError<E> owns a domain or AsyncFault payload inline, or explicitly retains immutable error storage.
Task propagation, ready consumption, structured joins, and generators forward the complete error rather
than extracting move-only payloads. StopReason represents Requested/Deadline/OwnerClosed/RuntimeShutdown
and survives task, WhenAll, WhenAny, and generator propagation. Wiring owner/deadline/shutdown request
origins into these reasons remains with those later ownership/deadline tasks; this migration does not claim
those policies are implemented. Existing first-publication guards still prevent a later stop request from
replacing an existing result.

Updated public Async/Network documentation, Base tests/benchmarks/package consumer, native/fallback I/O
and network call sites, Hello.IO, and NGIN.UI's async call sites. Private driver completion/admission types
retain their existing backend contracts; they were distinguished from public Completion during integration.

Verification:

- Rebuilt all Async/Execution/IO/Net targets in Debug: **390 passed, six existing Windows/TLS skips**, zero
  failures (`build/async-runtime-evidence/terminal-migration-tests3.txt`).
- Rebuilt Completion, Ready, Task, When, WhenAny, AsyncGenerator, ExecutorTransitions, ChildLifetime,
  TaskScope under ASan/UBSan: **93 passed** (`terminal-migration-asan-tests.txt` in that directory).
- New fixtures assert exactly three channels with both error payload alternatives, all four stop reasons
  through ordinary tasks and combinators/generators, and retained move-only error propagation/lifetime.
- Static installed Execution consumer passed (`terminal-installed-static.txt`); rebuilt shared installation
  and its consumer, which passed (`terminal-installed-shared.txt`).
- Hello.IO validated, built, and passed its canonical ngin smoke test in `build/manual/Hello.IO`. All three
  execution modes completed, using io_uring on this host (`terminal-hello-tests.txt`).
- NGIN.UI configured with its canonical CMake command and GCC 15.2, but NGINUITests could not build:
  unchanged Platform.hpp, State.hpp, and Application.cpp return UIError directly where std::expected is
  required. Those files have no working diff. The async call-site migration remains unverified by that
  package build; log `terminal-ui-build.txt`. No unrelated UI return conversions were changed.
- Source review and diff checks passed. Initial integration logs retain private-driver call-site rewrite
  corrections and a test field-access rewrite correction; no checks were disabled to pass verification.

T11's Base implementation and consumer smoke tests are verified; NGIN.UI package compilation and native
Windows/macOS validation remain limitations. T12's coordinated publication/retirement race gate is next.

## T12 coordinated race verification — 2026-09-13

Completed the local Stage 2 race gate against the three-channel implementation. This pass added two
controlled tests and reused the existing race/retirement suite instead of creating per-path checkpoints.

| Required ordering | Evidence exercised |
| --- | --- |
| Cancellation before registration | Cancellation registration on an already-requested source, self-reset/destruction while Register returns, pre-canceled Task waits, and empty-token admission tests. |
| Completion/cancellation during submission | TaskContext early-submission handshake and rejection/discard tests; 100 two-worker resolver setup/cancellation races; runtime timer installation/cancellation stress. |
| Backend completion before cancellation delivery | New NativeFileAwaiter fixture publishes a controlled backend's last buffer write and completion before requesting cancellation, then drives common/selected delivery. It verifies one resume, retirement of the backend request, and no completion before delivery. This tests the adapter ordering, not a simulated claim of OS execution. Actual native/fallback runtime tests run alongside it. |
| Cancellation while a backend borrows memory | WorkerGate tests hold the worker at its final access, request cancellation, and verify the parent cannot finish until release. File-open cancellation verifies unclaimed descriptor cleanup; socket ready/close races verify retirement before descriptor reuse. |
| Concurrent unregister | Latch-controlled Reset waits for the callback; callback self-reset remains supported. 512 cancellation/reset iterations verify zero or one reserved delivery and return outstanding reservation storage to zero. |
| Cancellation after outcome storage, before final delivery | New value/void coroutine-cleanup fixture requests cancellation from a local destructor after the outcome is stored. Value, domain-error payload, fault payload, and an existing Deadline stopped reason remain unchanged. The registered callback fires once despite repeated requests. |
| Frame destruction and nested cleanup | ChildLifetime stress uses 256 suspended or 2,000 immediate iterations, checks child-before-parent retirement, and joins leases. Generator/scope/race tests verify owned captures retire under concurrent delivery. |
| Saturated/rejected/discarded delivery | Existing reserved-capacity, unsupported-executor, closed-admission, discarded submission/timer/resolver, and runtime shutdown tests remain passing. |

Verification in the existing configured trees:

- Rebuilt Async/IO/Net in Debug: **349 passed, six existing skips** (355 selected), log
  `build/async-runtime-evidence/stage2-races-debug-tests.txt`. The subsequently added controlled native case
  was checked with the NativeRuntime target: **eight passed**, `stage2-native-race-tests.txt`.
- ASan/UBSan, RelWithDebInfo: **227 passed** across all Async and selected file/runtime/resolver suites,
  `stage2-races-asan-tests.txt`. The socket selection was explicitly checked separately: **13 passed** in
  `stage2-races-native-socket-asan-tests.txt` (that invocation also repeats seven native cases).
- Refreshed NativeRuntime sanitizer target: **eight passed**, including the new adapter test,
  `stage2-native-asan-tests-fresh.txt`. Test-list inspection caught an object compiled before the late fixture
  edit; source timestamp refresh and the canonical target rebuild ensured the new case actually ran.
- Checked final test diffs and `git diff --check`. No production change, benchmark, source archive, or relaxed
  assertion was needed for this pass. Log names above are relative to the existing build evidence directory.

T12 and Stage 2 are complete locally. Windows/macOS native gates, TLS-enabled verification, the previously
reported unrelated UI package build errors, and final performance acceptance remain open. T13 now implements
scope-owned running operations, asynchronous closure, and deterministic reports; T14 adds wait-only observers.
