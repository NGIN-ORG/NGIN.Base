# I/O runtime implementation progress

The full `IORuntimePlan.md` is **not complete**. This records the verified
foundation and remaining work; `IORuntime.md` describes the current API.
The latest checkpoint is [mixed-load fairness and file admission](#mixed-load-fairness-and-file-admission-checkpoint-2026-09-11).

## Implemented foundation

- Recorded admission, resource concurrency, thread ownership, shutdown, and
  task-scope contracts in `IORuntimeContracts.md`.
- Captured five Linux Release baseline runs and fixed initial regression
  thresholds before changing execution code (`IORuntimeMeasurements.md`).
- Fixed fallback worker cancellation publishing completion while a blocking
  operation still accessed borrowed memory. Cancellation now requests a result
  and the worker publishes it after its final buffer access.
- Fixed parent continuation affinity for value/void Task and Operation awaits.
  Status queries now acquire terminal publication before reading payloads.
- Made cancellation-aware child waits join the child before publishing parent
  cancellation. Published cancellation-registration ownership before making its
  callback visible to concurrent cancellation.
- Made cancellation-aware Yield/Delay completion run on its selected executor,
  arbitrate ready/cancel races once, and release coroutine leases promptly.
  Discarding a pending cancellation-aware timer delivers a terminal fault.
  Nonfinite delays are rejected; large finite deadlines saturate safely.
- Added bounded, allocated-at-admission `CompletionReservation` delivery to
  ExecutorRef, ThreadPoolScheduler, and CooperativeScheduler. File worker and
  network readiness waits reserve terminal delivery before admission.
- Added the independently compiled IORuntime component, exports, package
  integration, private service attachment, and isolated installed consumers.
  Net no longer links filesystem implementation or installs its headers.
- Made filesystem workers lazy, with pending/running worker jobs bounded by
  `FileOptions::queueDepthHint`. Worker-pool partial construction cleans up
  threads already started if later initialization fails.
- Added the private `RuntimeLoop` and `RuntimePoller` implementation in the
  IORuntime component. The loop has bounded submissions, completions, timers,
  and registrations; queued execution and dispatch affinity; fixed first-drive
  ownership; nonreentrant Run/PollOnce; and stop/drain/manual shutdown behavior.
  It rotates event sources across bounded batches and retains OS event backlog.
- Connected the core to the public Runtime API. Runtime now exposes its executor,
  fixed-owner Run/PollOnce, RequestStop/Shutdown, state and host-wait snapshots.
  Removed NetworkMode and Stop. RuntimeRunner synchronously establishes ownership
  and joins its loop thread on shutdown.
- Replaced POSIX network waiter scans and private threads with direct, stable
  runtime registrations. Pending waits reserve external delivery and common
  operation capacity, and route cancellation through the runtime.
- Routed fallback worker results and native file results through the runtime's
  bounded operation queue. Workers remain lazy and retain runtime retirement
  ownership until their final access to captured resources has ended.
- Integrated Linux io_uring eventfd notifications with the shared loop. The native
  backend uses bounded request slots, no private completion thread, and bounded CQ
  draining that reasserts readiness when coalesced notifications leave a backlog.
- Added removable monotonic timers and ExecutorRef::ScheduleTimer with
  TimerRegistration ownership. TaskContext removes canceled runtime timers and
  reports a terminal scheduling fault for timers discarded during shutdown.
- Added reusable CompletionReservation delivery. Started tasks on tracked executors
  reserve a lifetime and continuation slot; value/void Task and Operation child
  completions use that slot even after admission closes. Task continuation
  rejection no longer resumes the parent inline. Child payload propagation runs
  on the parent's executor, with the child frame retained through delivery.
- Reworked WhenAll to join its existing children without starting another helper
  task between waits; joining survives stopping and tight continuation budgets.
- Added TaskScope with bounded child admission, retained factories and contexts,
  reserved terminal observation, sibling cancellation, and explicit joining that
  releases child frames before return. Added RunTask's root supervisor, failure
  precedence, reserved shutdown notification, and joining across external executors.

This is an implementation checkpoint, not a completed cross-platform runtime.
Windows networking and native files use per-operation registrations in the
shared IOCP loop. Affected Windows test executables have cross-compiled and
linked; Windows runtime behavior remains unverified here and will be tested
by the user in a Windows environment. File admission is
implemented and tested on Linux. Remaining backend and application-cleanup work
is listed below.

## Verification of this foundation

Linux/WSL2, GCC 15.2, 2026-09-08:

- Affected Async, Execution, IO, and Net suites: 177 passed, five TLS-provider
  tests skipped, in both Debug and ASan/UBSan RelWithDebInfo.
- Five lifetime/cancellation race tests repeated 20 times under ASan/UBSan:
  all passed, including 512 registration/destruction races per applicable run.
- IORuntime-only and Net-only installed-package consumers: both passed.
- Rebuilt `ngin_cli` and `NGINCliTests`. CLI tests: 53 passed, one existing
  manifest-inventory assertion failed because it expects 53 manifests while
  `git ls-files` contains 54 in the tested inventory. This work added no manifest
  file and does not change that count; the unrelated assertion is unchanged.
- Hello.Native validation/build/run passed.
- Existing Hello.IO validation/build/smoke test passed with io_uring selected.
- Diff whitespace checks passed. No commits, branches, or pushes were made.

Additional loop/timer verification on the same host:

- All affected suites rebuilt without compiler warnings: 210 tests passed and
  six skipped in both Debug and ASan/UBSan (216 selected). Five skips require an
  unavailable TLS provider; the sixth is the portable backend's native
  host-descriptor test, whose adapter remains outstanding.
- The new loop suite runs against both epoll/eventfd and portable poll/pipes on
  Linux: 33 passed and one skipped, including ownership, saturation, fairness,
  backlog, stale generations, host notification, shutdown, and removable timers.
  The suite also passed 20 ASan/UBSan repetitions.
- A wake/shutdown stress test exposed a missed terminal-state transition after
  the control signal was consumed. Readiness now includes the pending transition
  to Stopped. The reproducer passed 200 consecutive processes after the fix,
  each with 2,000 submissions racing the driver's entry into platform wait.
- Independent public-header compilation passed, including TimerRegistration.
  IORuntime-only and Net-only installed consumers passed again.
- Windows IOCP and macOS/BSD kqueue implementations have not been built or run.
  These earlier results do not claim final backend integration or performance.

## Remaining implementation

1. Finish native platform validation of file admission. Shared-position,
   independent-offset, flush, and close ordering use a per-file gate, with a shared
   queue budget across handles and executors. Cold tasks retain state, and native
   handles retire with their last owner. Earlier Windows changes passed
   cross-compilation and test-executable linkage; the latest gate-budget changes
   have Linux verification only. Windows execution remains assigned to the user.
2. Validate Windows native files/networking and macOS backends. The Windows
   socket driver uses the shared IOCP registration boundary; POSIX host waits
   now have a bounded descriptor snapshot, including the portable fallback.
   Windows execution will be performed by the user.
3. Finish task cleanup and cancellation auditing as the remaining backends are
   integrated. Tracked task and generator submissions now deliver discarded work
   through their existing reservations, including token-free TaskContext waits.
   Generator advances retain their producer; generic cancellation registration,
   WhenAny admission, scoped retirement, and nested Task delivery use the tested
   ownership protocol. Remaining backend call chains still need auditing.
   Broaden TaskScope/RunTask tests for concurrent submission, allocation failure,
   and resource integration as the remaining backends are completed.
4. Complete native platform validation of host integration and examples. POSIX
   hosts can copy bounded wait-source snapshots; Windows hosts use RuntimeRunner
   with their selected host executor. Hello.IO demonstrates all three entry/executor
   patterns, scoped TCP peers, external CPU work, and in-flight shutdown on Linux.
5. Complete concurrent TCP load, comparable runtime/external root entry, original-
   baseline allocation comparisons, and concurrent-producer memory measurements.
   Mixed UDP/file/task timer load and repeated file-gate overload on four external
   executors now have measurements. File read/fallback latency still exceeds
   original thresholds; final performance acceptance has not passed.
6. Run final component-consumer, example, sanitizer, race and platform validation
   against the completed implementation, and update current API documentation.

The original temporary baseline source/executable directory was no longer present
when host integration finished. Tracked source was restored from revision
`203f704e97a704259ef2db1ee8c93bf92763a7e1` using a read-only Git archive; its
local reference was recorded in `/tmp/ngin-io-baseline-reference-path`. The sequential, file, and network-scaling measurement harnesses and static
Release binaries were rebuilt there. Temporary copies may not persist; use the
recorded revision and artifact hashes to restore them. Original production source was unchanged;
benchmark-only entry code uses the original lifecycle. Recorded initial measurements
and thresholds remain unchanged. This is
measurement infrastructure outside the repository, not a production compatibility path.

Current shared-loop integration verification (Linux/WSL2, GCC 15.2):

- After public-runtime and Linux native-file integration, all 227 selected
  Async/Execution/IO/Net checks passed under ASan/UBSan, with six provider/platform
  skips. The native test includes a 96-request io_uring backlog and batch size 1.
- The subsequent reusable task-continuation change passed 38 focused Debug tests,
  including stopping with an external child and joining another pending child
  after stop with only one runtime continuation slot.
- With TaskScope and RunTask added, all 244 selected checks passed in Debug and
  Debug ASan/UBSan: 238 passed, six provider/platform skips. Both include root
  success, cancellation, failures, external children, closed-admission joining,
  and child factory lifetime.
- Thirty-one continuation, scope, and root tests passed 20 ASan/UBSan repetitions.
  Independent public-header compilation and the IORuntime-only and Net-only
  installed consumers passed. The IORuntime consumer exercises RunTask and
  RuntimeRunner without filesystem headers or linkage.
- Hello.IO validation, build, and canonical smoke test passed with io_uring.
  All three execution modes completed, including an accept canceled and joined
  during stopping. The example creates and cleans its own local scratch data.
- The sanitizer build exposed existing sign-conversion warnings in unchanged
  Hashing headers; no warning from the new runtime/task files was reported.

Final checks for this application-ownership checkpoint:

- After fixing owning-awaiter moves and exception publication, all 245 selected
  checks passed in both Debug and Debug ASan/UBSan: 239 passed and six skipped.
- Hello.IO's canonical smoke test rebuilt against those headers and passed again
  in all three modes. Diff whitespace checks passed in Base and the workspace.
- The active goal remains unfinished. Next work must address per-resource close
  and overlap admission, Windows networking/shared IOCP, WhenAny/generator
  shutdown and overload paths, remaining platform validation, and performance.

## POSIX socket admission checkpoint (2026-09-09)

- SocketHandle now owns stable state. Close prevents further admission, requests
  cancellation, and defers native release until all backend leases retire.
- TCP/UDP/listener async methods capture that state before returning a cold task.
  Moving or reopening the wrapper cannot redirect existing tasks to another socket;
  destruction cancels pending operations without leaving a dangling wrapper access.
- POSIX send, receive, datagram, connect, and accept use one backend operation from
  admission through native completion. Read and write progress independently;
  same-direction overlap and duplicate accepts report OperationInProgress. Connect
  reserves both directions and cancellation closes an in-flight connection.
- Completion and common-operation tickets, cancellation callbacks, accepted-socket
  state, and readiness registrations are secured before native I/O. Blocking sockets
  are rejected before they can stall the loop. Native handles retire before terminal
  delivery; operation ownership remains tracked until delivery has been transferred.
- Accepted native sockets use preallocated ownership state; nonblocking setup failures
  close the accepted handle and report an error. Connect verifies the peer after a
  writable hint, including stale readiness observed before connect began.
- All 60 Net checks passed in Debug and Debug ASan/UBSan: 55 passed, five TLS-provider
  skips. These cover moves, close/readiness/cancellation races, reopening, overlapping
  operations, full read/write progress, admission rejection before a ready send, and
  capacity recovery. Public-header compilation and the Net-only installed consumer
  passed. Hello.IO's canonical smoke test passed in all three modes with io_uring.
- Fourteen socket operation/state checks passed 20 ASan/UBSan repetitions, including
  64 close/readiness/cancellation/reopen races and 128 close/admission races per run.
  No new compiler warnings were reported for this checkpoint. Diff whitespace
  checks passed; unrelated concurrent workspace documentation edits were preserved.

This checkpoint does not validate Windows networking or macOS. Registering before
native I/O guarantees admission but adds registration work to immediate operations;
its latency and allocation costs still need the final baseline comparison and tuning.
The full goal remains active, including file resource rules, shared Windows IOCP,
WhenAny/generator shutdown paths, host integration, platform validation, and measured
performance acceptance.

## Structured notification and frame-retirement checkpoint (2026-09-09)

- Replaced WhenAny's detached watcher tasks and inline rejection fallback with
  terminal notifications reserved before factory invocation. Exhaustion or allocation
  failure starts no child; factory exceptions, empty tasks, and child admission failures
  all contribute a terminal outcome and join the other children.
- The first observed terminal outcome selects the winner and requests loser
  cancellation. The parent joins with its existing continuation reservation, including
  during stopping and with all other runtime completion slots occupied.
- A concurrent test exposed terminal publication preceding release of a cancellation
  callback's coroutine-frame lease. Added reusable internal observation and final-frame
  retirement notification; WhenAny and TaskScope reuse the same child slot for both
  phases. Child frames retire before joining, and factory captures are released on
  the parent executor. RunTask explicitly awaits root-frame retirement after its scope.
- Added focused allocation/capacity, factory-failure, mixed-result, untracked-executor,
  cancellation-lifetime, concurrent-completion, and stopping-with-external-children
  checks. Nested Task/Operation propagation and generator continuation paths remain
  part of the unfinished cleanup audit; this is not a claim of full plan completion.

Verification for the structured notification checkpoint (Linux/WSL2, GCC 15.2):

- All 271 selected Async/Execution/IO/Net checks passed in Debug and Debug
  ASan/UBSan: 265 passed, six provider/platform skips. Both builds reported no
  compiler warnings. Independent public-header compilation passed.
- Twenty-seven WhenAny, TaskScope, RunTask, and RuntimeRunner checks passed
  20 ASan/UBSan repetitions, including 128 concurrent WhenAny runs per process
  and a deliberately retained cancellation frame that must delay scope joining.
- IORuntime-only and Net-only installed consumers passed. Hello.IO rebuilt and
  passed its canonical smoke test in all three modes with io_uring selected.
- Diff whitespace checks passed. No commit, branch, or push was made; concurrent
  workspace documentation changes remain outside this task's edits.

## Nested task delivery checkpoint (2026-09-09)

A concurrent regression reproduced child locals being destroyed after the parent
locals they borrowed. Scope retirement alone did not prevent a lower-level delivery
callback from retaining the child while a different executor thread unwound its parent.

- TaskContext now ends setup and delivery frame access before publishing failure or
  resuming execution. Cancellation registration retirement still precedes delivery.
- Child delivery copies dispatch metadata and releases its extra child hold before
  the owning awaiter consumes the result. Task parent continuations release waiting
  references before resuming or propagating terminal failure. TaskScope, WhenAny,
  and root retirement use the same resume rule and existing reserved storage.
- The regression covers three nested coroutine frames: value and void children,
  cancellation-aware awaits, cancellation before yielding, and cancellation racing
  with a suspended delay. Each complete run exercises 8,512 nested cancellations.
- This preserves Task/Operation ownership; it does not consume a borrowed owner or
  introduce a new cancellation-unwinding or exception protocol. Generator and other
  generic continuation paths remain part of the unfinished cleanup audit.

Verification for the nested delivery checkpoint (Linux/WSL2, GCC 15.2):

- All 273 selected Async/Execution/IO/Net checks completed successfully in Debug
  and Debug ASan/UBSan: 267 passed and six provider/platform checks skipped.
  This includes owned and borrowed value/void Operation retirement checks.
- Thirty-one child-lifetime, WhenAny, TaskScope, RunTask, and RuntimeRunner
  checks passed 20 ASan/UBSan repetitions, including 170,240 nested cancellations.
- Independent public-header compilation, IORuntime-only and Net-only installed
  consumers, and the canonical Hello.IO smoke test passed. Hello.IO completed
  all three execution modes with io_uring selected.

## Cancellation registration delivery checkpoint (2026-09-09)

- Removed the generic cancellation node's ordinary submission and inline-resume
  fallback. A raw coroutine registration reserves completion storage before its
  callback becomes visible, including when the token is already canceled.
- Capacity/allocation failures report `ResourceExhausted`; unsupported, stopped,
  or rejecting executors report `CompletionUnavailable`. A raw coroutine without
  a valid executor reports `InvalidTarget`. Callback-only registrations and empty
  tokens retain their existing behavior.
- Cancellation transfers the reserved continuation before finishing its invocation.
  Reset on another thread joins that handoff; self-removal remains supported.
  Unregistering before invocation or declining the continuation releases the slot.
  Raw coroutine ownership remains with the caller through queued delivery.
- Added shutdown-affinity, unavailable-storage, allocation-failure, self-removal,
  empty-token, and cancellation/reset race coverage. Fifteen cancellation checks
  passed Debug, then 20 ASan/UBSan repetitions. Delivery uses no fresh submission
  or allocation even after admission closes and allocators are set to fail.

AsyncGenerator still has separate inline fallbacks and a producer-frame lifetime
problem when a canceled consumer leaves while the producer has queued work. That
path needs its own ownership and cancellation fix; the full goal remains active.

After the cancellation registration change, all 279 selected Async/Execution/IO/Net
checks completed successfully in Debug and Debug ASan/UBSan: 273 passed and six
provider/platform checks skipped. Both affected-suite builds and independent
public-header compilation passed without compiler warnings. Diff whitespace
checks passed. This does not replace the remaining platform and performance gates.

## Generator ownership and delivery checkpoint (2026-09-09)

The queued-producer/canceled-consumer regression crashed the previous generator
implementation. Its cancellation callback could resume the consumer on the producer
executor and leave the producer with no valid consumer continuation.

- Next snapshots producer-frame ownership before returning its cold task. Moving
  or destroying the generator no longer invalidates a queued or cold advance.
- An active advance reserves the producer's execution/continuation storage and
  retains its consumer. Initial production uses the producer executor; yield and
  terminal delivery reuse the consumer task's reserved slot. Removed the generator's
  ordinary consumer submissions and inline rejection fallbacks.
- Producer execution and temporary delivery holds end before the consumer resumes.
  Consumer cancellation waits for a yield or terminal outcome; an uncancelable
  producer suspension must finish naturally. A competing Next fails independently
  rather than terminating the active producer's ownership period. The public async
  documentation now describes these ownership and cancellation boundaries.
- Focused coverage includes separate producer/consumer/Operation executors, cold
  and pending ownership moves/destruction, closed or exhausted completion storage,
  allocation and submission failures, cancellation-aware and uncancelable producer
  waits, and repeated concurrent value delivery and canceled-frame retirement.

This change does not resolve the separate no-token TaskContext ordinary-submission
discard path identified during the audit. That path still needs terminal delivery
when a scheduler discards accepted work during shutdown. File resource rules,
Windows IOCP, host/platform integration, and final performance acceptance remain
part of the full goal.

Verification for the generator checkpoint (Linux/WSL2, GCC 15.2):

- All 289 selected Async/Execution/IO/Net checks completed successfully in Debug
  and Debug ASan/UBSan: 283 passed and six provider/platform checks skipped.
- Fourteen generator checks passed 20 ASan/UBSan repetitions, including 40,000
  value sequences and 40,000 canceled owned-producer retirements on four workers.
- Affected-suite builds and independent public-header compilation passed without
  compiler warnings. Diff whitespace checks passed. Platform and performance
  acceptance remains outstanding; this is a verified implementation checkpoint.

## Queued submission retirement checkpoint (2026-09-09)

- Reproduced an admitted task remaining incomplete after its ordinary submission
  was discarded. Initial root/child tasks, generator production, and token-free
  yield/delay steps now use a shared submission handshake and the task's existing
  completion reservation. A discarded accepted step reports `Stopped` on that
  executor; immediate rejection preserves the actual scheduling error.
- Dispatch before admission returns queues a reserved resume after setup finishes.
  Later ordinary dispatch resumes directly on the selected executor. Active
  execution protects the frame until both sides end their access; temporary
  submission objects do not retain child locals while their parent unwinds.
- Added ExecutorRef binding equality to choose whether an existing reservation
  belongs to the requested context. A different executor reserves its own delivery.
  Unsupported token-free suspension fails before admission instead of relying
  on ordinary queue retention.
- Focused tests cover early dispatch/discard, rejection, separate executors,
  initial root/child/generator discard, occupied-worker shutdown, token-free
  timers, and 10,000 yields using one completion slot with no further allocations
  from its allocator and no recursive dispatch.

Verification for queued submission retirement (Linux/WSL2, GCC 15.2):

- All 298 selected Async/Execution/IO/Net checks completed successfully in Debug
  and Debug ASan/UBSan: 292 passed and six provider/platform checks skipped.
- Fifty-eight TaskContext, generator, child-lifetime, WhenAny, TaskScope, RunTask,
  and RuntimeRunner checks passed 20 ASan/UBSan repetitions.
- Independent public-header compilation and IORuntime-only/Net-only installed
  consumers passed. Hello.IO rebuilt and passed its canonical smoke test in all
  three modes with io_uring selected. Both affected-suite builds reported no
  compiler warnings; diff whitespace checks passed.
- These checks close the identified tracked-submission discard gap. File-handle
  concurrency/lifetime, Windows IOCP, platform/host validation, and performance
  acceptance remain required before the full plan can be considered complete.

## Async file ownership checkpoint (2026-09-09)

- Moved AsyncFileHandle dispatch into its compiled IO component. Each public
  operation snapshots its backend state and function binding before returning
  the cold task; the existing backend operation table signatures are preserved.
  Calls cannot switch to a replacement handle or borrow a moved-from handle's
  shared_ptr object while suspended.
- Added native-handle ownership to opened results and local async file state.
  Ownership transfers invalidate the source descriptor/handle. Destruction,
  canceled open delivery, and failure while constructing shared backend state
  can now retire the native handle instead of leaking it.
- Tests cover all six operation bindings around cold/pending moves and destruction,
  invalid-binding snapshots, abandoned cold calls, and Linux descriptor lifetime
  through native/fallback reads and canceled delivery of an already-opened file.
  Windows ownership code was updated but has not been built or run here.

Shared-position serialization, explicit-offset concurrency, flush barriers, and
explicit close admission still require implementation. This ownership checkpoint
provides the stable state needed by those rules; it is not completion of the full
file-resource contract or runtime plan.

Verification for async file ownership (Linux/WSL2, GCC 15.2):

- All 110 selected I/O/header checks completed successfully in Debug and Debug
  ASan/UBSan: 109 passed and the portable host-descriptor check skipped.
- Seven async-handle/native-runtime checks passed 20 ASan/UBSan repetitions.
  Linux native and fallback reads both retained the descriptor through a cold
  operation after handle release, then retired it with the last owner. Canceled
  delivery of an already-opened file left no descriptor behind.
- Added and passed an IO-only installed-consumer matrix entry exercising all six
  compiled file-operation methods. Independent public-header compilation and
  the canonical Hello.IO smoke test passed; Hello.IO completed all three modes
  with io_uring selected. Diff whitespace checks passed; builds reported no
  compiler warnings. Windows ownership changes remain unverified on Windows.

## Async file admission checkpoint (2026-09-09)

- Added a compiled per-file admission gate. Independent offsets may overlap;
  shared-position operations enter in order. Flush waits for earlier work and
  blocks later work. Close closes admission, drains earlier backend work, then
  releases the native handle. Overlapping close/new operations report Busy;
  repeated close after closure succeeds. Pending gate nodes live in retained
  coroutine frames and wake through their existing task continuation reservation.
- Queued cancellation removes its waiter without occupying a worker or starting
  backend work. Canceling a queued close reopens admission. Runtime stop cancels
  queued file work before it starts another backend operation. Backend outcomes
  are observed and their frames released before the lease retires, including
  rollback guards for native close admission failure.
- Removed long POSIX worker locks around data/flush calls now protected by the
  gate. Added consistent access-mode, transfer-length and explicit-offset
  validation. Explicit-offset writes on append handles fail with NotSupported
  rather than silently appending at a different offset.
- Updated Windows shared-position accounting to advance by actual transferred
  bytes, including successful transfers whose delivery was canceled. Worker
  fallback OVERLAPPED requests now use individual events and wait for pending
  OS access before releasing their stack state or borrowed buffers, following
  [GetOverlappedResult's completion and event contract](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult).
  These Windows changes remain uncompiled and unexecuted on Windows here.

Verification on Linux/WSL2 with GCC 15.2:

- All 123 selected I/O/header checks completed in Debug and Debug ASan/UBSan:
  122 passed and the portable host-descriptor check skipped.
- Sixteen gate/native-runtime checks passed 20 ASan/UBSan repetitions. Coverage
  includes ordering, overlap, close/cancel races, frame retirement on backend
  failure, native/fallback descriptor lifetime, access/offset validation, canceled
  close, and shutdown while file work waits at the gate. A final test refinement
  checks ResourceExhausted explicitly using a nonempty reservation request;
  that capacity test passed again in both configurations.
- Independent public-header compilation, the IO-only installed consumer, and the
  canonical Hello.IO smoke test passed. Hello.IO completed all three modes with
  io_uring selected. Builds reported no warnings; diff whitespace checks passed.

This does not complete the runtime plan. Shared Windows IOCP/network integration,
platform and host-loop validation, remaining resource/capacity audits, and final
performance acceptance remain open. The file wrapper currently adds a backend
Task frame to observe failure before releasing admission; its allocation and
latency costs must be included in the final measurements and tuning. File-gate
waiters consume their selected executor's task admission capacity; the runtime
operation slot is acquired only when backend work is admitted. Audit that boundary
across multiple external executors in the remaining overload measurements.

## Shared Windows file IOCP checkpoint (2026-09-09)

- Added per-operation IOCP registration to the private RuntimeLoop/RuntimePoller
  boundary. Each OVERLAPPED maps directly to a bounded generation registration;
  cached events retain the captured generation. Idle handles require no retained
  registration or scan. Readiness registration remains separate on POSIX.
  This avoids treating a per-handle completion key as a reusable operation ID:
  [Windows retains the port association for the handle's lifetime](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-createiocompletionport).
- Replaced the Windows file backend's private IOCP and worker thread with these
  registrations in the shared loop. Native reads/writes use the common reserved
  completion path. Blocking flush/close use the existing lazy, bounded filesystem
  workers. Native capacity and cancellation registration are checked before OS
  submission; rejected control submission preserves native-close rollback.
- User cancellation and runtime stop request CancelIoEx for the specific pending
  OVERLAPPED. Registration/state remain alive until the terminal packet is consumed;
  callbacks are joined before terminal delivery can retire backend ownership.
  Cancellation does not imply that OS access has ended, as required by the
  [CancelIoEx contract](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex).
- Added Windows tests for one handle with 96 independently registered requests,
  handle/OVERLAPPED reuse, registration limits, and shutdown. Named-pipe tests
  exercise genuinely pending native reads, user cancellation without stopping the
  runtime, shutdown cancellation, loop-thread affinity, and lazy worker creation.
  The POSIX test checks that unsupported IOCP registration releases capacity.

Verification and limits:

- Linux/WSL2 GCC 15.2: all 320 selected Async/Execution/IO/Net checks completed in
  Debug and Debug ASan/UBSan: 313 passed, seven platform/provider checks skipped.
  Forty-two loop/native-runtime checks passed 20 ASan/UBSan repetitions; the
  portable host-descriptor check remained skipped. Independent public-header
  compilation, IORuntime-only/IO-only installed consumers, and Hello.IO's canonical
  smoke test passed. Hello.IO completed all three modes with io_uring selected.
- Cross-compiled seven affected production and four test translation units for
  Windows x64 using [LLVM-MinGW 20260908 / Clang 23.1.1](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260908),
  unpacked outside the repository under /tmp. The cross-check found and fixed a
  missing direct exception include in RuntimePoller and an invalid char8_t-to-Path
  construction in the Windows filesystem, using the existing Text::AsBytes helper.
  Production source checks had no warnings; test files reported Catch2's
  __COUNTER__ extension warnings with this compiler.
- Full cross-linking was not achieved: existing Foundation files include Windows.h
  with uppercase spelling on a case-sensitive host, and its Exception API requires
  std::stacktrace, which this libc++ toolchain lacks. No substitute stacktrace or
  header shim was added. The Windows source checks do not prove Windows execution;
  the Windows-specific tests have been compiled but not run.

Windows networking still needs implementation on this shared registration boundary.
Native platform execution/linkage, host adapters, the remaining lifetime/capacity
and resource audits, and final benchmark acceptance remain required by the goal.


## Shared Windows socket IOCP checkpoint (2026-09-09)

- Implemented TCP connect/accept and TCP/UDP send/receive through the runtime's
  per-OVERLAPPED IOCP registrations. The network backend has no private port,
  polling scan, or thread. Operation storage, cancellation registrations, native
  registration capacity, and both executor delivery paths precede native work.
- Socket leases allow one read and one write concurrently, reject same-direction
  overlap, and retain native ownership through cancellation and terminal packet
  consumption. Connect holds both directions; a canceled issued connect closes
  the socket. Successful connects update their socket context before delivery.
- Resolve ConnectEx/AcceptEx against the actual socket provider. Accept creates
  its socket from the listener's provider after admission, requests zero initial
  payload bytes, and establishes the accept context and nonblocking mode before
  returning it. Failed/canceled accepts release the unreturned socket.
- Added duplex TCP progress and Windows datagram-truncation/reuse tests. Existing
  socket suites cover cancellation, close, moves, admission saturation, rejected
  delivery, and shutdown. WinSock mode changes now update stable socket metadata
  so blocking handles are rejected consistently before asynchronous admission.
- Fixed QPC tick conversion overflowing its intermediate product after about
  31 minutes at 10 MHz. The private constexpr conversion divides whole seconds
  first, computes fractional nanoseconds exactly without intermediate overflow,
  and saturates at the representable nanosecond limit. Boundary tests run on
  Linux without executing Windows APIs.
- Removed an unused aggregate include from CoreInit and corrected the case of
  Windows SDK includes in the Foundation/Execution/IO sources needed to link.
  This resolves the prior cross-link blockers without changing the Exception
  API's C++23 stacktrace requirement or introducing a header shim.

Verification (Linux/WSL2, GCC 15.2 and Windows x64 cross-compilation):

- Async/Execution/IO/Net: 321 selected in both Debug and ASan/UBSan; 314 passed,
  seven skipped (five unavailable TLS-provider cases, one Windows IOCP suite,
  one portable host-descriptor check). Independent public-header checks passed.
- The 42 focused Net runtime/socket cases passed in Debug and in 20 ASan/UBSan
  repetitions. The connect cancellation/exclusive-admission case is also enabled
  for native Windows runs now that its backend exists.
- Time.PlatformTime: all four cases passed in Debug and ASan/UBSan, including
  the old overflow boundary, long uptime, fractional ticks, high frequencies,
  and both saturation paths.
- Net-only and IORuntime-only installed consumers passed. Hello.IO rebuilt and
  passed all three execution modes, including in-flight shutdown.
- Seven Windows test executables cross-compiled and linked against static
  components with LLVM-MinGW 20260908 /
  Clang 23.1.1. No Windows test has run here. The user will perform Windows
  execution in a Windows environment; no further execution/emulation attempts
  are part of this workspace verification. Catch2 emits compiler-extension
  warnings with this cross-compiler; these are not production runtime warnings.

For native Windows verification, use the package's canonical tests preset from
`Dependencies/NGIN/NGIN.Base` with a supported C++23 Windows compiler:

```powershell
cmake --preset tests
cmake --build build/tests --config Debug
ctest --test-dir build/tests -C Debug --output-on-failure -L 'Async|Execution|IO|Net|Time'
```

The full goal remains active. Host integration, remaining resource/capacity
and cleanup audits, macOS validation, and final benchmark acceptance remain
outstanding; cross-linking does not establish native platform behavior.


## Host wait-source checkpoint (2026-09-09)

- Added `Runtime::CopyNativeWaitSources` and the IORuntime-owned
  `NativeWaitSource` header. The caller supplies bounded storage; the copy does
  not allocate or drive callbacks. Too-small spans report `no_buffer_space`
  without partial output, and copying starts no file/network service or thread.
- Linux epoll and macOS/BSD kqueue expose one aggregate read descriptor. Portable
  poll exposes its wake pipe plus each registered descriptor's read/write
  interests, copied under the registration mutex. New registration changes wake
  a host using an older snapshot. Monitoring only the wake pipe would miss native
  readiness; the documented host protocol monitors every returned source.
- Documented pumping, snapshot refresh, changed timer deadlines, invalidation,
  and shutdown in the runtime guide, public contract, and Hello.IO. Snapshot
  storage of registrationCapacity + 1 covers the portable maximum. Callback
  ownership remains on the host thread and there is no periodic discovery poll.
- Windows returns `operation_not_supported` for descriptor snapshots. Its
  supported host pattern keeps RuntimeRunner driving IOCP and sends application
  continuations to a notifying host/UI executor. Clarified that callers must not
  dequeue runtime IOCP packets or treat the port as a general Win32 wait handle.
- Updated independent header ownership and both component-consumer harnesses.
  Tests cover cross-thread submissions, earlier deadlines and timer-driven host
  wake, native readiness, registration refresh, insufficient storage, and the
  public API's lack of backend initialization. The portable host case no longer
  skips.

Verification (Linux/WSL2, GCC 15.2):

- Rebuilt affected Async/Execution/IO/Net targets and independent public-header
  checks in Debug and ASan/UBSan, without compiler warnings. Each suite selected
  326 cases: 320 passed, six skipped (five unavailable TLS-provider cases and
  the Windows-only native file IOCP suite).
- All 40 normal/portable runtime-loop cases passed after the final test cleanup
  change, including 20 consecutive ASan/UBSan repetitions. No host case skipped.
- IORuntime-only and Net-only installed consumers passed, including the new
  snapshot API in the IORuntime consumer. Hello.IO rebuilt and all three entry
  patterns passed.
- Windows runtime-loop and public runtime test executables cross-compiled and
  linked with LLVM-MinGW. Only Catch2's known compiler-extension warnings were
  emitted. No Windows execution was attempted in this checkpoint; native Windows
  testing remains assigned to the user. macOS execution is still unverified.
- Reviewed source/header/test integration and diff whitespace. No commits,
  branches, or pushes were made; unrelated root documentation changes remain.

Remaining work includes the resource/capacity and cleanup audits, native platform
validation, additional benchmark coverage, and resolving performance regressions
against the fixed thresholds. Host snapshot implementation does not establish
those broader completion criteria.


## Performance characterization checkpoint (2026-09-10)

- Rebuilt the original recorded revision and current sequential/file workloads
  with GCC 15.2 and static Release components. Five paired processes completed
  for each workload; raw data and current medians are recorded in
  `IORuntimeMeasurements.md`. The initial acceptance thresholds are unchanged.
- Added a Net-only benchmark for 8/1,024 pending UDP receives with 8 active
  receivers, including suspended latency, rearm-inclusive throughput, idle CPU,
  thread count, cold setup, cancel-and-join, and shutdown. Its fixture owns and
  joins every admitted receive before releasing buffers, including error unwind.
- Corrected the fixture's reused ephemeral ports after both implementations
  stalled with duplicate endpoints. It now disables address reuse and verifies
  uniqueness before admission. The reproduction and invalid attempts are
  documented separately; five corrected original/current pairs all completed.
- The current scaling fixture also passed in Debug with ASan/UBSan enabled on
  the executable and linked components. The new benchmark builds without
  compiler warnings. No production source was changed in this checkpoint;
  existing full test-suite results remain those of the preceding checkpoint.
- Suspended receive means were close to the original (1.7–5.3% higher), but
  rearm-inclusive throughput was 12.7–13.6% lower. Current ready UDP and file
  latencies still exceed the fixed limits. Native/fallback cold-open medians
  also exceed their allowances. Performance acceptance has not passed.
- Idle network CPU improved by more than 99% with no periodic runtime wake in
  the observations. The comparable network-only binary has 239,596 text bytes
  versus 185,332 originally (+29.3%); component isolation does not imply smaller
  total runtime code. Batch cancellation is reported separately from the fixed
  single-operation cancellation limit.
- Diagnostic strace counts (excluded from latency results) show 8,011 epoll_ctl
  calls currently versus two originally in the sequential workload. The next
  tuning target is immediate socket admission: secure bounded storage first,
  while avoiding native readiness registration when the syscall completes
  immediately. Any change must preserve rejection before observable I/O when
  configured capacity is exhausted, directional leases, cancellation, and
  exactly-once affine completion.

Further work remains: mixed-load/timer lateness and allocation/overload memory
measurements, long-timer cancellation, runtime-executor entry costs, resource
and cleanup auditing, native platform validation, and performance tuning against
all fixed thresholds. No Windows execution was attempted; the user owns native
Windows testing. The full implementation goal remains active.

## Immediate socket and completion wake checkpoint (2026-09-11)

- Split private readiness admission from native monitoring. POSIX operations
  reserve bounded registration ownership before their first syscall and arm
  native readiness only when they must wait. Unarmed retirement avoids poller
  calls, and immediate duplex operations preserve the pending peer's interests.
- Kept directional leases, executor/common completion reservations, cancellation
  registration, and capacity checks before OS I/O. Stop visits unarmed handlers;
  their final off-owner retirement wakes a stopping loop. Failed native interest
  updates restore the peer or fail it terminally. Monitoring failure after an
  issued connect closes that socket to avoid an unobserved connection.
- Completion queues now notify on publication, admission close, and final
  retirement after close. Ordinary capacity recovery creates no runnable work
  and no longer wakes the owner. No completion or shutdown delivery was removed.
- Added admission/arm/recovery/stop tests with ready pipe data, plus queue wake
  and off-worker unpublished-reservation shutdown tests. Existing pre-send
  saturation, directional overlap, cancellation, and descriptor-lifetime tests
  continue to pass.
- Built all affected Async, Execution, IO, and Net targets and public-header
  checks in Debug and ASan/UBSan configurations. Each suite selected 332 tests:
  326 passed and six skipped (five TLS configuration skips and the Windows-only
  IOCP test). All 95 focused queue/runtime-loop/network checks passed 20 sanitizer
  repetitions; both final off-owner host-wake checks also passed 20 sanitizer
  repetitions. The Net and IORuntime installed consumers and Hello.IO's three
  execution modes passed. Builds had no compiler warnings; diff checks passed.
- Recorded separate five-pair Release measurements for the two changes, with
  unchanged harnesses and preserved pre-change binary hashes. Deferred readiness
  cut epoll_ctl calls from 8,011 to seven in a diagnostic run; queue notification
  changes subsequently cut wake-related reads/writes from about 12,000 each to
  about 4,000 each. Traced timings were excluded from latency measurements.
- Ready-UDP mean improved 23.8% in the first paired series and another 27.3% in
  the second. The latest mean was 38.784 us. Several fixed acceptance limits
  still fail, including UDP p95/mean and file latency/cold initialization. Neither
  tuning series replaces the original baseline. Full results and limitations
  are recorded in IORuntimeMeasurements.md.

The full goal remains active. Mixed-load/timer lateness, allocations and queued
memory under overload, long-timer cancellation, concurrent TCP load, runtime-
executor entry costs, file-gate admission across external executors, cleanup
failure audits, and final performance acceptance remain. No Windows execution
was attempted; native Windows testing remains assigned to the user. Native
macOS validation remains unverified.

## Allocation and overload checkpoint (2026-09-11)

- Added a static Net-only allocation benchmark with replacement-new instrumentation
  scoped to that executable. It reuses the first-party system allocator and
  allocation-statistics type; no production allocator or API was changed.
- Added repeated submission, reserved-completion, long-timer, delayed-task, and
  socket-operation/registration overload workloads. They verify expected errors,
  capacity recovery, exactly-once callbacks, cancellation/join completion, and
  stable retained storage under repeated rejection. Timer captures release before
  another loop drive; every fixture returns to its initial allocation level.
- The initial delayed-task fixture caught 1 KiB of retained cancellation-table
  capacity after the first rejection. Source inspection identified table growth
  for the transient registration. The corrected measurement records that growth,
  verifies subsequent rejection plateaus, includes it in peak storage, and requires
  its release when the group ends. The diagnostic is preserved with the results.
- Five static Release processes produced identical counts. At capacity 64, direct
  submission and completion queues peaked at 7,168 tracked requested C++ bytes.
  Both ready and suspended receives used 11 allocations / 2,232 requested bytes
  per operation. Separate callback instrumentation verified zero additional
  selected-executor completion callbacks for ready receives and one for suspended
  receives; it does not distort the raw allocation comparison.
- The complete benchmark passed Debug ASan/UBSan with linked components instrumented,
  including the four-thread instrumentation self-check. Final builds were clean.
  This checkpoint changes benchmarks and documentation only; the preceding 332-test
  Debug/sanitizer verification remains the latest production-suite result.

These measurements exclude allocations that bypass C++ replacement new, allocator
bookkeeping, and kernel storage. They are current single-owner runtime workloads,
not an original-baseline comparison or a universal application-memory cap. Full
measurement scope and raw artifacts are recorded in IORuntimeMeasurements.md.

Remaining work includes mixed-load timer lateness, file/external-executor allocation
and overload coverage, concurrent producer and TCP load, the file-gate admission
boundary across external executors, cleanup failure audits, original-baseline
allocation comparisons, and the unresolved latency/cold-start limits. No Windows
execution was attempted; native Windows testing remains assigned to the user.
Native macOS validation remains unverified, and the full goal remains active.


## Mixed-load fairness and file admission checkpoint (2026-09-11)

- Added mixed UDP/file/yield workloads with 1,000 one-millisecond timer samples,
  native/fallback files, and runtime/external child continuations. The first
  external-worker workload exposed starvation of injected or older local tasks.
  ThreadPoolScheduler now rotates completion, injection, oldest-local and
  newest-local dispatch. External RunOne can also help the sole worker's queue.
- Added deterministic progress tests for the pool and retained the failed fixture
  output. Thirteen pool-fairness/completion checks passed 100 ASan/UBSan repetitions.
  Five serial before/after measurement pairs preserve both improvements and slower
  cases; the checkpoint meets ready-UDP limits but still misses file limits.
- Closed the file admission gap across multiple external executors. Each file
  service now accounts for queued and active handle operations against
  files.queueDepthHint, separately from backend/worker budgets. Rejections occur
  before queueing; rejected close preserves admission, canceled waiters retain
  their slot until cleanup runs, and backend retirement precedes active release.
  File state construction binds every gate to its shared service on POSIX and
  Windows. No new public option, dependency, or compatibility path was added.
- Added repeated saturation/recovery checks across two gates and three executors,
  cancellation with a paused executor, and sixteen rounds of four concurrent
  producers sharing a capacity of eight. Existing close-failure/rollback checks now
  use a capacity of one to verify recovery after every terminal outcome.
- Added a static IO allocation benchmark for two handles, four external cooperative
  executors, 64 admitted reads plus 256 rejections, and 16 complete fixtures per
  backend. Five Release processes and Debug ASan/UBSan passed. Retained storage
  plateaus after the first rejection and returns to the initial allocation count
  and byte level after every fixture. Peaks include setup and all four executors;
  they are not per-read costs. Counts, exclusions and hashes are in
  IORuntimeMeasurements.md and its raw artifacts.
- Final affected suites selected 336 tests each in Debug and Debug ASan/UBSan:
  330 passed and six skipped (five TLS-provider tests and Windows-only IOCP).
  Eleven file-gate tests passed 50 sanitizer repetitions. Public-header compilation,
  all three installed IO/Net/IORuntime consumers, and canonical Hello.IO passed.
  All final mixed benchmark cases passed five Release processes and ASan/UBSan.
  Builds had no compiler warnings and diff whitespace checks passed.

Remaining work includes concurrent TCP load, comparable runtime/external root
entry costs, original-baseline allocation comparisons, concurrent-producer memory
measurements, remaining cleanup failure audits, and the unresolved original file
latency/cold-start limits. No Windows execution was attempted; Windows testing
remains assigned to the user. Native macOS validation remains unverified. The
full goal remains active.
