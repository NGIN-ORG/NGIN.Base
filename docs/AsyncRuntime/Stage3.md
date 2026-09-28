# Stage 3 — Structured ownership and lifecycle policy

## T13 scope reports and asynchronous closure — 2026-09-13

Implemented the first coherent T13 batch:

- TaskScope accepts AsyncEnvironment and supplies its properties to children with the scope-owned token.
  Close rejects subsequent admission; RequestStop requests cooperative cancellation. Existing factory Spawn
  still discards successful values; returned scope-owned Operation handles are not implemented yet.
- Child outcomes are retained separately until join. An error requests sibling stop promptly; stopped alone
  does not. Join builds errors in spawn order, so completion order cannot choose the primary error. Errors
  outrank stopped outcomes; stopped information remains available in ScopeReport::StoppedReason().
- ScopeReport retains immutable aliases into retired child records. It supports move-only E without copying
  payloads. Factory captures are released outside the owner lock before join returns; concurrent/reentrant
  joins cannot race capture release. Repeated joins inspect the same stable report.
- WithScope retains the body, installs the supplied scope environment, catches body creation/execution
  failures, closes admission, requests stop, and joins before propagation. ScopeFailure selects a body error
  first, otherwise the lowest-index child error, and retains secondary diagnostics. Value/void and stopped
  paths follow the same cleanup protocol. Borrowed body locals still require an explicit join before their
  C++ lifetime ends, as specified in Contracts.
- RunTask forwards retained report errors. Hello.IO's explicit stop call is migrated to RequestStop. Public
  Async documentation describes the implemented helper and explicitly identifies unfinished result handles.

The report integration exposed AsyncError's mutable-lvalue getter rejecting retained errors even for reads.
Lvalue access is now immutable; rvalue extraction moves inline payloads, copies retained copyable payloads,
or rejects extraction of a retained move-only payload with a diagnostic directing callers to forward the
whole AsyncError. This preserves report ownership rather than mutating aliased diagnostic storage.

Verification after batching the changes:

- Rebuilt all Async targets plus IO_RunTaskTestsTests in Debug: **158 passed**, zero failures, using
  `ctest --test-dir build/tests -C Debug --output-on-failure --timeout 30 -R '^Base\.(Async\.|IO\.RunTask\.)'`.
  Log: `build/async-runtime-evidence/scope-closure-batch-tests.txt`.
- Rebuilt TaskScope, WithScope, Completion, CompletionAwait, ChildLifetime, and RunTask under ASan/UBSan:
  **28 passed**, zero failures, in the existing RelWithDebInfo tree. Log:
  `build/async-runtime-evidence/scope-closure-batch-asan-tests.txt`.
- New fixtures cover body success/error/stop/exception, child cleanup, reverse completion versus spawn order,
  stopped children not stopping siblings, error-over-stop precedence, move-only primary/secondary errors,
  and inherited body/child environment. Initial RunTask retained-error inspection failure is kept in the
  initial focused log; the final verification above includes that regression.
- Final diff review/check passed. No benchmark or broad I/O/network suite was repeated for this partial
  ownership batch. Hello.IO will be rebuilt at the completed T13 migration gate; its changed stop call is
  not claimed as newly smoke-tested here.

At this checkpoint T13 remained open. Next work was to separate the scope's execution/diagnostic ownership from the single-consumer
Operation handle, return handles from scoped Spawn, reserve all child admission before startup, migrate
root/SyncWait/RunTask and ownerless Spawn/Detach consumers, and integrate owner stop reasons. Scope-owned
result handles and the no-ownerless-startup requirement must be verified before closing T13. Allocation
resource propagation for the new owner/report records also remains part of that owner-state work.


## Scoped result handles — 2026-09-13

TaskScope's factory and cold-task Spawn overloads now return
`expected<Operation<T, E>, ScheduleError>`. Scope records retain the producer independently of the
move-only result handle. Dropping the handle leaves execution, sibling stop, and report ownership intact.
Cold-task rejection destroys the input without starting its body.

A separate result state retains the typed completion. The scope records immutable error aliases before
retiring the producer; both the consumer and report can retain a move-only error without consuming each
other's diagnostics. The result becomes ready only after the final producer frame lease is released.
Pending consumers use their own task's reserved continuation and executor. Publication and extraction
precede dispatch, outside locks, allowing a consumer to join and release its scope reentrantly. A second
pending consumer or second consumption receives InvalidTaskUsage. Result storage uses the environment's
PMR resource; the resource must outlive any escaping handle or retained error.

Focused tests cover move-only values escaping a joined scope, move-only errors retained by both the
consumer and report, producer retirement leases, independent consumer executor placement, duplicate
pending consumers, rvalue await/WithScope reentrancy, and rejected cold inputs. Existing dropped-handle,
report, body cleanup, terminal-channel, and RunTask fixtures remain in the same verification batch.

T13 remains open for complete admission, root/ownerless-startup migration, owner stop reasons, and full
owner/report allocator propagation. No benchmark or broad I/O/network gate is repeated for this batch.

Verification: **35 Debug tests and the same 35 ASan/UBSan tests passed**, zero failures. Rebuilt
ScopedOperation, TaskScope, WithScope, Completion, CompletionAwait, ChildLifetime, and RunTask targets
in the existing trees, then selected their exact CTest suites. Logs:
`build/async-runtime-evidence/scoped-results-tests.txt` and
`build/async-runtime-evidence/scoped-results-asan-tests.txt` (matching build logs alongside them).
Final diff check passed; the subsequent Task.hpp edits only clarify ownership comments.


## Scope allocation ownership — 2026-09-13

Scope state, child records, the reserved child table, report storage/table, and linked cancellation state
now use the environment's PMR resource. Result state already used it. Cancellation source control blocks
and linked-registration containers also use the selected resource; explicit TaskContext linking follows
the same allocation policy. Parent registration nodes still belong to their parent's resource.

Scope construction validates the environment before allocating. Allocation failures unwind all partial
owner storage. Failure to allocate a child record or result state returns ResourceExhausted without
invoking the factory or consuming admission capacity. Joined reports alias retired child records and may
outlive the scope, so their resource lifetime includes escaping errors and cancellation tokens. Building
the report uses the table capacity reserved at scope construction.

New tests inject failure at every construction allocation, reject each child-record allocation before
factory invocation, and retain an error beyond scope destruction before verifying all resource storage
is released. The same batch covers cancellation, environment propagation, scoped consumers, and closure.
Complete startup admission and root migration are still outstanding; allocator propagation does not
close T13 by itself.

Verification: **45 Debug tests and the same 45 ASan/UBSan tests passed**, zero failures. Rebuilt the
ScopeAllocation, ScopedOperation, TaskScope, WithScope, Cancellation, and Environment targets in the
existing trees. Logs: `build/async-runtime-evidence/scope-allocation-tests.txt` and
`build/async-runtime-evidence/scope-allocation-asan-tests.txt`, with matching build logs. Final diff check
passed. No benchmark or broad integration suite was repeated.


## Scoped startup admission — 2026-09-13

Scoped Spawn reserves child/result storage, the owner notification, and the execution state/continuation
before invoking the factory or committing the child table entry. Failure releases those resources and
returns ScheduleError without invoking the factory or consuming capacity. The allocation fixture now
includes execution-state failure.

The shared internal startup helper accepts the pre-admitted execution state. Scoped startup publishes
its first coroutine step directly to that guaranteed continuation; it does not subsequently submit to
the general work queue. This also supports an executor that rejects general submissions while delivering
reserved work. Inline-ready inputs release their unused execution reservation normally. Existing
standalone startup delegates to the helper while its consumers await the root ownership migration.

The new admission fixtures verify reservation saturation, rollback of the already-reserved owner slot,
and successful reserved-only startup. Scoped values/errors, ready completions, factory failure, frame
leases, WithScope closure, and RunTask remain in the same focused verification batch. T13 still requires
root/ownerless-startup migration and owner stop reasons.

Verification: **41 Debug tests and the same 41 ASan/UBSan tests passed**, zero failures. Rebuilt
ScopeAdmission, ScopeAllocation, ScopedOperation, TaskScope, WithScope, Ready, ChildLifetime, and RunTask
in the existing trees. Logs: `build/async-runtime-evidence/scope-admission-tests.txt` and
`build/async-runtime-evidence/scope-admission-asan-tests.txt`, with matching build logs. Final diff check
passed. No benchmarks or broad integration suites were repeated.


## Stop origin propagation in Async — 2026-09-13

StopReason now has its own shared header. Cancellation sources atomically publish the first reason;
subsequent requests cannot change it. Tokens expose GetStopReason, and linked sources retain stable
per-parent callback records that forward the winning parent's reason. Callback addresses are reserved
before registration; empty parent lists do not allocate link records.

TaskContext's early and pending timer/yield cancellation paths, explicit cancellable child awaits,
generator cancellation, and combinator early cancellation now propagate the token reason. WithScope
closure and scope sibling-error cancellation use OwnerClosed, while explicit RequestStop defaults to
Requested. The void WhenAll path also preserves an existing child stop reason and forwards complete
AsyncError payloads rather than extracting retained move-only errors.

New fixtures cover all four reasons before/after suspension, duplicate and nested links, linking after
cancellation, first-request precedence, owner closure, and void WhenAll forwarding. I/O adapter reason
propagation, automatic deadline/runtime-shutdown origins, and root ownership migration remain open.

Verification: **77 focused Debug tests passed**, covering StopReason, Cancellation, TaskScope, WithScope,
ScopeAllocation, Completion, CompletionAwait, AsyncGenerator, When, and WhenAny. The **36 ASan/UBSan
checks passed** for StopReason, Cancellation, TaskScope, WithScope, and ScopeAllocation. Logs:
`build/async-runtime-evidence/stop-reasons-tests.txt` and
`build/async-runtime-evidence/stop-reasons-asan-tests.txt`, with matching build logs. Final diff check
passed; no benchmarks or broad integration suites were repeated.


## I/O stop origin delivery — 2026-09-13

Native and fallback file completion records now carry a stop reason selected when cancellation is
published to the public awaiter. Context cancellation preserves the token reason; runtime shutdown uses
RuntimeShutdown. Local filesystem adapters on Linux, Windows, and POSIX forward that stored reason.
File-operation gate cancellation and virtual filesystem checkpoints preserve context reasons, and gate
rejection due to driver stop reports RuntimeShutdown. RunTask's child closure requests OwnerClosed.

Socket operations retain the linked operation token and a completion reason before releasing the native
lease. Caller cancellation forwards its reason, socket close uses OwnerClosed, and runtime shutdown uses
RuntimeShutdown. Resolver cancellation callbacks have stable per-token records and pass the triggering
reason into the winning terminal publication. No terminal resolver result is overwritten by a late request.

Focused fixtures now assert Deadline for canceled file open, a native completion waiting for common
delivery, explicit resolver cancellation, and socket connect cancellation. They also assert OwnerClosed
for socket destruction and a resolver canceled before driver discard, and RuntimeShutdown for queued
native/fallback file leases. Windows/macOS native execution and TLS-enabled validation remain open.
Root ownership migration and the plan's automatic deadline/shutdown policy work are not closed here.

Verification: **50 focused Debug tests passed** for NativeRuntime, LocalFileSystem, RunTask, Resolve,
and SocketOperation. **30 ASan/UBSan tests passed** for NativeRuntime, Resolve, and SocketOperation.
Logs: `build/async-runtime-evidence/io-stop-reasons-tests.txt` and
`build/async-runtime-evidence/io-stop-reasons-asan-tests.txt`, with matching build logs. Final diff check
passed. Subsequent Windows-only review records the first cancellation reason under the operation lock,
including early startup and watch rejection; those platform changes remain unexecuted on this Linux host.
No benchmarks or broad integration suites were repeated.


## Scoped root entry points — 2026-09-13

SyncWait now admits cold roots through TaskScope, waits for a final retirement notification, then joins
and consumes the scoped result. It preserves the ready-input path without an executor or allocation,
rejects canceled cold roots without starting them, and returns allocation admission faults. Its old
producer-terminal-only blocking wait methods were removed. The public entry remains reachable from
Task.hpp and has a dedicated SyncWait.hpp header.

Scope notifications now capture borrowed pointers rather than holding a shared owner across user
resumption. Live-child fail-fast ownership keeps those pointers valid. The retirement delivery acquires
its temporary state lease before reducing pending count, then drops it before consumer/join dispatch or
root notification. The first terminal callback accesses no owner state after handing off retirement.
This lets a blocking root finish without a hidden callback retaining its allocator-owned scope storage.

RunTask owns its supervisor through an outer scope and requests runtime stop only after that supervisor
retires. Its body producer still joins children before retiring failure-frame locals. The body context
now uses the child scope environment, so child failure stops a suspended body; child error outranks that
stopped outcome, while a body error remains primary. RootScopeAccess provides the shared internal
retirement notification/finalization mechanism. Global Spawn/Detach and other consumers remain to be
migrated; T13 is not marked complete.

Verification: **63 focused Debug tests passed**, including SyncWait, Ready, ScopedOperation, TaskScope,
WithScope, Cancellation, Resolve, and RunTask. **29 ASan/UBSan tests passed** for SyncWait,
ScopedOperation, TaskScope, WithScope, and RunTask. New fixtures hold a root frame lease beyond terminal
publication, check 64 root allocator-release cycles, exercise allocation rejection and move-only errors,
and verify that RunTask child failure stops the body without hiding the child error. Logs:
`build/async-runtime-evidence/scoped-roots-tests.txt` and
`build/async-runtime-evidence/scoped-roots-asan-tests.txt`, with matching build logs.

Hello.IO passed canonical ngin validation, build, and all three execution modes (manual RunTask,
RuntimeRunner, and external continuation executor), including file/TCP/cancellation checks with io_uring.
Logs: `build/async-runtime-evidence/scoped-roots-hello-build.txt` and
`build/async-runtime-evidence/scoped-roots-hello-tests.txt`. No benchmarks were rerun. Final diff check
passed. A subsequent resolver callback-local rename only removes a shadow warning exposed by this build.


## Sequential I/O consumers — 2026-09-13

Removed thirteen ownerless startup calls from sequential production paths: the file-operation gate and
twelve TLS transport/flush/read wrappers. They now consume child Tasks through AsCompletion, retaining
all three outcome channels while sharing the parent execution chain. File-gate cleanup still follows
child frame retirement; failures cannot bypass admission-lease release. TLS contexts, buffers, and timeout
cleanup remain in their existing enclosing frames through child completion.

Verification: **22 Debug file tests passed** (NativeRuntime and LocalFileSystem), and **8 native-file
ASan/UBSan tests passed**. The TLS provider was enabled in an isolated build using the canonical tests
preset with `-B build/tests-tls -DNGIN_BASE_TLS_REQUIRE_PROVIDER=openssl`, GCC 15.2, static libraries,
and the already available Catch2 source. CMake found OpenSSL 3.0.13. **All six OpenSSL-enabled TLS tests
passed**, without skips, including handshake/data transfer, trust rejection, mutual authentication,
cancellation/timeout, and truncation. Logs under `build/async-runtime-evidence/`:
`sequential-io-tests.txt`, `sequential-io-asan-tests.txt`, and `sequential-io-tls-tests.txt`, with matching
build/configuration logs. Final diff check passed; no benchmarks were rerun.

This closes the local provider-enabled TLS behavior check, not the cross-platform or final performance
matrix. WhenAll, WhenAny, and remaining application/test consumers still require ownership migration
before removing global Spawn/Detach.


## Scoped WhenAll — 2026-09-13

Both WhenAll overloads now admit their children through TaskScope instead of global Spawn. Void groups
can drop their result handles because the scope retains diagnostics and execution. Value groups singly
consume the handles and retain values in argument order. Admission failure stops further admission,
requests stop, and still joins prior children. Exceptions during admission or result consumption also
request stop and join before returning a fault; scope destruction never becomes the cleanup path for
live children. Child errors come from the spawn-ordered scope report and outrank stopped children.

New fixtures cover saturation after one admitted child, move-only error retention when an earlier child
stopped, and a value move that throws after producer retirement while another child waits on a timer.
The latter verifies sibling stop, frame cleanup, and removal of the pending timer. **21 focused Debug
tests passed** (ScopedWhenAll, When, Completion, StopReason), and **14 ASan/UBSan tests passed**
(ScopedWhenAll, When). Logs: `build/async-runtime-evidence/scoped-when-all-tests.txt` and
`build/async-runtime-evidence/scoped-when-all-asan-tests.txt`, with matching build logs. Final diff check
passed; no benchmark or broad integration suite was repeated. WhenAny and remaining external consumers
still need migration before removing global Spawn/Detach.


## Non-consuming observation — 2026-09-13

Added OperationRef, Operation::Observe, CompletionNotification, and FirstCompleted. Scope-owned result
storage embeds a compiled notification registry; references share its existing control block without
allocation and preserve terminal kind after consumption. Ordinary sequential Task state is unchanged.
Pending observation allocates a state and bounded PMR registration array, with stable intrusive records.
One atomic claim selects the terminal notification or wait-only cancellation before executor dispatch.
A setup/publication barrier prevents delivery from racing unfinished registration. Delivery uses the
awaiting task's reserved continuation, joins cancellation/source callbacks, and releases source references
before resuming user code. No user code runs under the notification lock.

Empty/invalid inputs fail explicitly. Precompleted input ties use index order and precede an already
canceled wait token. Cancellation preserves its reason and leaves the independently owned operation
running and consumable. Legacy global-Spawn producer handles deliberately return invalid observation
references while their removal remains part of T13/T15; no permanent compatibility observer was added.

Verification: **23 focused Debug tests passed** (FirstCompleted, ScopedOperation, TaskScope), and
**15 ASan/UBSan tests passed** (FirstCompleted, ScopedOperation). Tests cover delayed observer dispatch,
concurrent publication and cancellation with concurrent delivery, duplicate registrations, retained
value/error/stop information, independent scope cleanup, and rollback of state/array/cancellation-node
allocation failure. Logs: `build/async-runtime-evidence/first-completed-tests.txt` and
`build/async-runtime-evidence/first-completed-asan-tests.txt`, with matching build logs. No benchmark or
broad integration suite was repeated. Race and WhenAny consumer migration remain open.


## Owning Race and WhenAny migration — 2026-09-13

Race replaces the WhenAny header and symbol. It returns typed, index-addressed variant winners and a
bounded RaceFailure containing indexed errors and retained successful/stopped winner information.
An error winner remains primary; otherwise the lowest-index loser error overrides value/stop. Immutable
aliases retain move-only errors. A winner slot is allocated before startup so returning a loser error
never moves the successful payload again.

TaskScope now shares its child implementation with an internal prepare/start/abort admission path.
Race prepares every child execution reservation, retirement notification, context, linked cancellation
source, and retained winner slot before invoking factories. Failed admission aborts the entire batch
without execution. ScopeBatchAccess exposes this only to internal owners; public Spawn still prepares
and starts its single child. An internal TaskExecutionState terminal hook performs atomic winner
arbitration before queued observer dispatch and before producer frame retirement. It requests loser
OwnerClosed stop; the shared scope join waits for every admitted frame and factory capture.

Migrated the existing WhenAny tests to Race, plus When/Completion/ChildLifetime and RuntimeRunner
consumers and both HardeningBenchmarks callers. Added heterogeneous/move-only winner retention,
error-winner priority with multiple errors, stopped-winner override, void/duplicate alternatives,
terminal-before-retirement selection, and rollback for every environment allocation before factory
invocation. The runtime WhenAll fixture now reserves five slots for its parent and two scoped children;
its prior three-slot budget described the superseded ownership path.

Verification: **52 focused Debug tests passed** (Race, When, Completion, ChildLifetime, TaskScope,
ScopeAdmission, RuntimeRunner), and **32 ASan/UBSan tests passed** (Race, ScopeAdmission,
ScopeAllocation, ChildLifetime, RuntimeRunner). Logs: `build/async-runtime-evidence/race-tests.txt`
and `build/async-runtime-evidence/race-asan-tests.txt`, with matching build logs. The migrated
HardeningBenchmarks Release target compiled successfully (`race-benchmark-build.txt`); timings were
not run. The existing concurrent race fixture exercises 128 multiworker iterations.

Final review made factory invocation order explicit and releases its unused execution reservation on
factory failure. Retained scope diagnostics must not hold execution capacity. The new regression and
the two other ScopeAdmission tests then passed in **both Debug and ASan/UBSan**, recorded in
`race-retained-admission-tests.txt` and `race-retained-admission-asan-tests.txt` with matching build logs.
This focused follow-up did not repeat the full batch. Final diff check passed. Source/tests/benchmarks
contain no WhenAny references; historical plans and measurements retain their original names. T15 is
implemented locally; native platform and final performance gates remain open, as does removal of the
remaining global Spawn/Detach consumers under T13.


## Inherited deadlines — 2026-09-13

Added WithDeadline and WithTimeout. Relative durations convert once when execution begins, with
non-finite rejection, immediate expiry for nonpositive durations, fractional-tick rounding, and saturated
monotonic addition. Ready inputs retain their original completion without a boundary. Equal/later
inherited deadlines share the owner's timer and cancellation token; stricter boundaries allocate their
own compiled DeadlineState and linked source from the environment resource. Inner work inherits the
new deadline/token through CurrentEnvironment. The wrappers do not rewrite explicit borrowed contexts.

Timer removal support and timer admission are checked before starting inner work. Expiry requests
Deadline stop; produced values and errors remain their actual outcomes. The wrapper consumes inner
completion and joins frame retirement, then removes/joins timed work. A move-only timer callback lease
reports retirement when its WorkItem is destroyed, so claimed or externally retired timer callbacks
cannot outlive wrapper storage. Joining uses the parent's existing continuation reservation and queues
outside the state lock. Discarded timer work requests RuntimeShutdown; prior parent stop reasons survive.

Verification: **31 focused Debug tests passed** (Deadline, Environment, TaskContext) and **26 ASan/UBSan
tests passed** (Deadline, TaskContext). Fixtures cover 64 inherited helper levels sharing one timer,
stricter nesting, early timer removal, cooperative expiry, noncooperative cleanup past expiry with value
and domain-error results, claimed timer joining including retirement on a foreign thread, parent stop,
discarded delivery, cold-start duration conversion, ready outcomes, invalid durations, unsupported timer
removal, timer rejection, and rollback of every boundary allocation before execution. Logs:
`build/async-runtime-evidence/deadline-tests.txt` and `deadline-asan-tests.txt`, with matching build logs.
Final diff check passed. No benchmarks or broad/native integration suite were repeated for this batch.
Application supervision, remaining global Spawn/Detach migration, and final cross-platform/performance
gates remain open.

## Application supervisor foundation — 2026-09-13

Added TaskSupervisor with Spawn/SpawnOn result handles, explicit Transfer, Close/RequestStop, Join, and
Shutdown. It reuses TaskScope's admitted child/execution/retirement records. Successful and acknowledged
error records release factory captures and recycle their slots, so application lifetime is not limited
to a cumulative task count. Outstanding result handles retain independent result state across slot reuse.

An optional noexcept, nonblocking error sink receives a retained immutable AsyncError after producer
frame retirement and outside owner locks. Accepted reports permit continued independent work. Missing
or rejecting sinks close admission, request OwnerClosed stop, and retain bounded unhandled diagnostics
for the shutdown report. Missing sinks trigger stop when the error is observed; a sink's rejection is
known after its retirement-time report. Shutdown requests RuntimeShutdown and joins all admitted work.
Multithreaded owners require sinks that support concurrent calls. The four Base detach tests now use
explicit supervisor transfer and joining; the immediate-completion fixture uses the canonical tracked
thread pool instead of its old untracked helper.

Recycled record deallocation now precedes the final pending decrement. A concurrently started ready Join
must not release an allocation resource while retirement cleanup still uses it. The raw callback is kept
valid by pending ownership until the final lock release, with no extra shared owner lease or subsequent
owner/child access after joined readiness can become visible. A regression deliberately blocks record
deallocation on another thread and verifies that independent joining remains pending until it finishes.

Verification: **45 focused Debug tests passed** (TaskSupervisor, TaskScope, ScopeAllocation,
ScopeAdmission, Task, ScopedOperation), and **39 ASan/UBSan tests passed** (TaskSupervisor, TaskScope,
ScopedOperation, Task). New tests cover 128 reuse cycles without retained successful storage, surviving
move-only result handles, accepted move-only error reporting, missing/rejecting sinks, held producer-frame
leases, shutdown cancellation, concurrent reporting, and allocator retirement ordering. Logs:
`build/async-runtime-evidence/supervisor-tests.txt` and `supervisor-asan-tests.txt`, with matching build
logs. Final diff check passed; no benchmarks or broad integration suites were repeated.

T17 remains open. Seven remaining naked Detach callers are in UI validation, commands, view-model work,
and the gallery; they need integration with an explicit application owner before removing the API.
Remaining global Spawn consumer migration, timed shutdown/status separation, native platform validation,
and final performance gates are also still open.

## UI executor and application ownership — 2026-09-13

TaskSupervisor::TryJoin provides nonblocking event-loop finalization: it closes admission and checks
cleanup readiness before creating a Join awaiter, so repeated pending polls install no waiter. The
new regression covers repeated pending polls, closed admission, and repeated completed inspection.

The UI's old void-returning queue adapter, which invoked rejected work inline and offered no completion
reservations, is replaced by a private independently testable UIAsyncScheduler. It bounds ordinary/timer
admission, reserves cleanup through CompletionQueue, removes timers, preserves its platform clock,
tracks current-executor identity, and dispatches at most 64 callbacks per UI batch with rotation between
completion, ordinary, and timer sources. Shutdown discards ordinary/timed captures outside locks while
leaving admitted completions deliverable.

Application now owns a TaskSupervisor, exposes BackgroundTasks, and calls explicit ShutdownTasks before
normal Run completion. Manual pump users can call ShutdownTasks while borrowed resources remain alive.
A failed pump retains unfinished work for a later shutdown attempt; destruction still cannot abandon
live supervised work. Unhandled errors remain inspectable through the joined owner report. Shutdown and
window closure use RuntimeShutdown/OwnerClosed stop origins. Four gallery motion starters now transfer
to the application owner and expose rejected admission through their status text. Gallery smoke mode
calls ShutdownTasks before releasing its model. Validation, AsyncCommand, and ViewModelTaskScope still
contain three Detach callers; they require explicit owner injection before removal of the legacy API.

Verification: **six UI.Async executor tests passed** using the new repository target NGINUIAsyncTests
in the existing `build/ngin-ui` tree. The same **six passed under ASan/UBSan** in the isolated
`build/ngin-ui-async-asan` configuration (native text/images disabled because this target depends only
on Execution). **Nine Base TaskSupervisor tests passed in both Debug and ASan/UBSan**. Logs:
`build/async-runtime-evidence/ui-owner-tests.txt`, `ui-owner-asan-tests.txt`,
`supervisor-poll-tests.txt`, and `supervisor-poll-asan-tests.txt`, with matching configuration/build logs.

A targeted Application.cpp object build was attempted and remains blocked by existing std::expected
conversion/accessor incompatibilities in Platform.hpp, State.hpp, and Application.cpp. The log is
`build/async-runtime-evidence/ui-owner-application-build.txt`. The new Application integration fixtures
and gallery changes are consequently **not claimed as compiled or executed end to end**. The isolated
executor tests do not substitute for those integration checks. Final diff checks passed in workspace
and Base. No benchmarks or unrelated broad suite were run; T17 and the overall goal remain open.


## UI consumer supervision — 2026-09-13

AsyncCommand, ViewModelTaskScope, and KeyedViewModelHost now receive an explicit
TaskSupervisor; asynchronous validation receives it with SetAsyncValidator.
Gallery and existing UI fixtures supply the application/test owner. The three
remaining naked Detach calls and the Base API/friend declarations are removed.
Sequential UI Spawn/await pairs use AsCompletion within their owned execution.

Admission rejection clears running state and publishes a command fault/rejection,
validation issue, or invalid ViewModel handle with a fault outcome. Run contexts
retain local cancellation links while adding owner cancellation. A destruction
regression caught loss of a linked source when replacing the original context;
the original context now retains the complete link chain through cleanup.
Command and validator callable objects are shared with admitted runs, retaining
coroutine captures through destruction without resetting mutable callable state
between runs.

NGINUIOwnershipTests compiles the production Command/ViewModel/State sources and
Validation templates independently of rendering providers. The affected State,
ViewModel, and synchronous Command expected paths now use std::unexpected/error.
Thirteen ownership/validation tests and six executor tests passed in both the
existing normal UI tree and the existing ASan/UBSan tree (19 each). No warnings
were emitted by these builds. Commands were the repository CMake targets followed
by ctest -R '^UI\.(Ownership|Async)\.'; logs are ui-ownership-{configure,build,tests}.log
and ui-ownership-asan-{configure,build,tests}.log under build/async-runtime-evidence.
No benchmarks or unrelated broad suite were run.

T17 remains open. Full Application/Gallery integration is unverified while other
UI std::expected migration errors remain. The final audit must also cover late
errors after UI observer destruction, setup/factory faults after status publication,
and stopped validation availability. Global Spawn migration remains under T13.


## UI late errors and integration — 2026-09-13

UI command/ViewModel tasks retain infrastructure fault details when the UI owner
has expired; destroyed or superseded UI domain errors are forwarded as diagnostic
faults retaining their code/id and message. Stale validation value results still
do not overwrite the newest field. A stopped current validator clears validating
state and leaves the field unavailable. Local cancellation links and cold task
frames are prepared before publishing running state; admitted factories now only
bind the prepared token and move the prepared task. PMR allocation rejection is
covered for each UI starter.

The UI package and Gallery expected-style result paths now use standard expected
accessors and explicit unexpected construction. No compatibility wrapper was
introduced. Full NGINUITests now builds, including the Application and Gallery
ownership-facing public headers. The initial full UI run passed 210/211 tests;
the remaining image-cache fixture destroyed its replacement renderer before the
cache that borrowed it. Declaring both renderers before the cache fixes that
lifetime, and the failed test passed on its focused rerun. Thus all 211 registered
UI checks passed across the full run and that corrective rerun. The focused
ownership/executor ASan+UBSan run passed 23/23, including late fault preservation,
stale validation, shutdown availability, and setup allocation failure.

Commands: cmake --build build/ngin-ui --target NGINUITests NGINUIOwnershipTests -j2;
ctest --test-dir build/ngin-ui --output-on-failure --timeout 45; focused failed-test
rerun after the fixture fix; existing sanitizer targets NGINUIOwnershipTests and
NGINUIAsyncTests followed by ctest -R '^UI\.(Ownership|Async)\.'. Full-build logs
retain three Motion.cpp warnings: its operation wait still ignores ordinary
Execute rejection and cancellation-registration failure. This is an actual
remaining guaranteed-delivery/shutdown gap, not a waived warning. T17 remains open
for the motion wait migration and saturation/shutdown regression coverage.

The headless Gallery test now joins application work on both success and
assertion-failure exits while model/text resources are still alive. Canonical ngin
test compiles Gallery.Shared and Gallery.Tests in separate dependency trees,
rebuilding Base/UI in each; no benchmark runs were added.

The canonical headless Gallery command completed successfully:
CMAKE_BUILD_PARALLEL_LEVEL=2 build/dev/Tools/NGIN.CLI/ngin test --project
Examples/NGIN.UI.Gallery.Tests/NGIN.UI.Gallery.Tests.nginproj --configuration Debug
--output build/manual-next/NGIN.UI.Gallery.Tests. It reported
"NGIN.UI gallery headless checks passed" and exited zero. This verifies the
shared Gallery source and deterministic test backends; the interactive SDL
frontend, hosted Gallery, and native Windows/macOS remain separate unverified
surfaces. Evidence logs are ui-integration-{build,tests}.log,
ui-image-fixture-{build,tests}.log, ui-late-errors-{build,tests}.log,
ui-late-errors-asan-{build,tests}.log, and ui-gallery-tests.log in
build/async-runtime-evidence.


## Motion wait admission and delivery — 2026-09-13

MotionOperationState reserves completion delivery and cancellation observation
before publishing a controller entry. Rejection returns a fault with the concrete
reservation/registration failure category; it cannot start or replace an animation.
The waiter arms the already-reserved delivery, and terminal publication exposes a
ready result only after cancellation registration cleanup. No ordinary Execute
submission is required to resume an admitted motion waiter, including after
executor admission closes.

Cancellation callbacks retain a live operation while executing, target the exact
controller entry by operation identity, and mark cancellation before competing
with entry publication. An already-canceled request therefore leaves the existing
animation unchanged. Callback reset and continuation dispatch occur outside the
operation/controller locks. The operation destructor joins any remaining
cancellation callback before destroying its fields. The guide now uses an
application supervisor and Race instead of global Spawn/removed WhenAny.

Seventeen focused motion/application-shutdown tests passed in both the normal
UI tree and ASan+UBSan tree. Four new regressions cover reservation rejection before
publication, cancellation-registration allocation rejection, reserved delivery
through owner shutdown with ordinary/reservation admission closed, and synchronous
cancellation before replacement. Existing motion completion, interruption,
unmount, and application shutdown cases also passed. Commands: build NGINUITests
in build/ngin-ui and build/ngin-ui-async-asan; ctest --output-on-failure with
-R 'UI\..*(motion|Motion|application.*work|application.*shutdown)' (30/45-second timeouts).
Logs: ui-motion-delivery-{build,tests}.log and
ui-motion-delivery-asan-{build,tests}.log under build/async-runtime-evidence.

The sanitizer integration target had not previously been built. Its image-decoder-
disabled branch needed one explicit std::unexpected conversion, now fixed. Motion's
three ignored-result warnings are gone. That disabled-native-text build still
emits the existing unused ResolveBundledFontPath warning; no sanitizer failures
were reported. No Gallery rebuild, broad suite rerun, or benchmarks were added.
T17 remains open for the final ownership audit with the global Spawn migration
tracked under T13; native Windows/macOS execution remains unverified.


## External Spawn callers — 2026-09-13

All remaining global Async::Spawn calls in Packages/ and Examples/ are migrated.
Hello.IO uses AsCompletion for sequential file/network/delay operations. Its
shutdown demonstration owns the concurrent accept in a bounded TaskScope and
joins it before checking outcomes or releasing the listener. Motion fixtures
admit work to Application::BackgroundTasks; returned scoped operation handles
still test completion and remain readable after the application has joined.

The existing normal UI build passed all 17 focused motion/application-shutdown
tests. The canonical Hello.IO ngin test passed manual, background, and external
execution modes, including its pending-accept shutdown check. Command:
CMAKE_BUILD_PARALLEL_LEVEL=2 build/dev/Tools/NGIN.CLI/ngin test --project
Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output
build/manual/Hello.IO. No benchmarks or repeat sanitizer run were needed for
this caller-only migration. Logs: ui-owned-motion-{build,tests}.log and
hello-io-owned-tests.log in build/async-runtime-evidence.

T13 remains open: Base tests and benchmarks still start roots with the global
Spawn API, and Task/Operation's temporary producer ownership path remains until
those roots migrate. The production packages/examples no longer depend on it.


## Combinator and scope test roots — 2026-09-13

Twenty global Spawn roots in Async/When.cpp and Async/TaskScope.cpp now have
explicit one-task supervisors with error types inferred from their cold tasks.
Tests join those owners before reading final results. No compatibility wrapper or
implicit test-root driver was introduced. The scope inline-delivery regression
observes the actual Join continuation body, separately from the enclosing root's
later scoped-result publication. A cancellation regression admits its root before
requesting stop, then starts the cold race with cancellation already requested;
this exercises race cancellation rather than root-admission rejection.

The 11 combinator tests and eight TaskScope tests passed in both Debug and
ASan+UBSan (19 tests per configuration). Built Async_WhenTests and
Async_TaskScopeTests in the existing Base build/tests and build/tests-asan trees,
then ran those two focused binaries. No benchmarks or broad integration reruns
were added. Evidence: base-owned-roots-{build,asan-build}.log,
base-owned-when-{tests,asan-tests}.log, and
base-owned-scope-{tests,asan-tests}.log under build/async-runtime-evidence.
T13 remains open for the remaining Base test/benchmark roots and legacy producer
path removal; global Spawn is still available only for that migration.


## Supervisor, deadline, and observation test roots — 2026-09-13

Thirty-eight global Spawn roots in TaskSupervisor.cpp, Deadline.cpp, and
FirstCompleted.cpp now use explicit one-task supervisors and join before final
result inspection. Fault injection still starts inside the admitted root, so it
exercises deadline/observation admission rather than failing the root fixture.
The observation rollback test releases its root and result interest before
comparing live PMR allocations with its baseline; retained owner storage is not
mistaken for leaked observation records. Existing concurrent publication,
wait-only cancellation, timer retirement, and supervisor-reporting checks remain.

All nine supervisor, twelve deadline, and eight observation tests passed in
Debug and ASan+UBSan: 29 cases in each configuration. Built the existing
Async_TaskSupervisorTests, Async_DeadlineTests, and Async_FirstCompletedTests
targets and ran their focused binaries; no benchmarks or broad integration
checks were repeated. Logs: owned-deadline-{build,asan-build}.log,
owned-supervisor-{tests,asan-tests}.log, owned-deadline-{tests,asan-tests}.log,
and owned-observer-{build,asan-build,tests,asan-tests}.log under
build/async-runtime-evidence. T13 remains open for other test/benchmark roots and
removal of the legacy global Spawn/producer ownership path.


## Race test roots — 2026-09-13

Migrated the 17 global Spawn calls in tests/Async/Race.cpp to explicit supervisor
ownership. Bounded executor tests keep owner publication on a separate scheduler
and admit execution with SpawnOn, preserving their original reservation limits
and allocation-failure positions. The unsupported-executor test now verifies
SpawnOn rejection before any race factory runs. The inline retirement test
observes the sequential race continuation before later root-result publication.
Every admitted root is joined before its final result is inspected.

Async_RaceTests passed in the existing Debug tree: 15 cases, 777 assertions,
including the existing 128-iteration concurrent completion test. Only this target
was built and run. No production code changed; sanitizer, benchmark, and broad
integration reruns are deferred to the ownership milestone. Logs:
owned-race-build.log and owned-race-tests.log under build/async-runtime-evidence.
T13 remains open for remaining Base test/benchmark roots and legacy producer
ownership removal.


## Scope, completion, and environment test ownership — 2026-09-13

Migrated 38 global Spawn calls across WithScope.cpp, ScopedWhenAll.cpp,
ScopeAllocation.cpp, ScopedOperation.cpp, StopReason.cpp, Completion.cpp,
ContinueWith.cpp, and Environment.cpp. Test roots now have explicit supervisors;
operation-await helpers use local child scopes and await Join before propagating
the result. Completed root owners are joined before result inspection or release.

The bounded WhenAll rejection test publishes its root result on a separate
scheduler, preserving child-admission limits. Early stop is requested after root
admission and before cold execution. Retained-error lifetime checks release the
supervisor as well as the result interest before checking payload destruction.
Environment tests account for the additional owner publication reservation and
separate admission storage from the original two execution-boundary allocations;
they still verify full resource release. Allocation rejection is now observed at
scoped admission before the task body starts.

All eight focused Debug targets passed: 32 cases and 354 assertions. Existing
build/tests configuration was reused. No production source changed, and no
sanitizer, benchmark, or broad integration run was added. Evidence under
build/async-runtime-evidence: owned-scope-completion-build.log,
owned-await-environment-build.log, owned-environment-rebuild.log, and
owned-{WithScope,ScopedWhenAll,ScopeAllocation,ScopedOperation,StopReason,
Completion,ContinueWith,Environment}-tests.log. T13 remains open for remaining
Base test/benchmark callers and legacy producer ownership removal.


## Sequential placement and child-lifetime test ownership — 2026-09-13

Migrated 29 global Spawn calls in ExecutorAffinity.cpp, ExecutorTransitions.cpp,
CompletionAwait.cpp, Sequential.cpp, and ChildLifetime.cpp. Roots use explicit
supervisors, and nested operation-await tests own their children in local scopes
that are joined before returning. Single-slot sequential and AsCompletion tests
keep execution on their original bounded scheduler and publish owner results on
a separate scheduler, preserving the single execution-reservation invariant.
Transition bodies continue to assert target affinity; their owner scheduler is
then driven to publish the final scoped result.

All five focused Debug targets passed: 24 cases and 52,165 assertions, including
the existing concurrent transitions, deep-chain fairness, and cancellation/frame
retirement loops. Built only these targets in the existing build/tests tree.
No production changes, benchmarks, sanitizer reruns, or broad integration runs
were added. Logs under build/async-runtime-evidence:
owned-sequential-placement-build.log, owned-child-lifetime-build.log, and
owned-{ExecutorAffinity,ExecutorTransitions,CompletionAwait,Sequential,
ChildLifetime}-tests.log. T13 remains open for remaining Base callers and removal
of global Spawn and legacy producer ownership.


## Ready-task and task API ownership — 2026-09-13

Removed 22 global Spawn calls from Ready.cpp and Task.cpp. Executor-free ready
values are consumed with SyncWait, including move-only values and repeated
consumption rejection. Running roots have explicit supervisors; one-slot ready
composition tests publish owner results on a separate scheduler. Throwing ready
payload moves execute inside an admitted factory so the failure is checked as a
runtime fault rather than an exception during fixture input transfer.

Scoped scheduler rejection is checked at admission. The result-handle release
race uses a tracked thread pool and joins its supervisor before inspecting frame
counts; the obsolete untracked ConcurrentExecutor test helper was removed.
Early result access remains tested before completion, with the root joined only
after the scheduler drains. Existing transfer and cancellation checks remain.

Async_ReadyTests and Async_TaskTests passed in the existing Debug build: 23 cases,
2,064 assertions. No production changes, benchmarks, sanitizer reruns, or broad
integration builds. Evidence under build/async-runtime-evidence:
owned-ready-task-build.log, owned-ready-rebuild.log, owned-Ready-tests.log, and
owned-Task-tests.log. Remaining Base callers and legacy producer removal keep
T13 open.


## TaskContext ownership and shutdown tests — 2026-09-13

Migrated all 15 global Spawn calls in TaskContext.cpp. Roots publish through
explicit supervisors on a separate scheduler, keeping owners alive across worker
scheduler destruction and keeping owner reservations outside bounded executor
measurements. Nested operation tests use joined child scopes. Roots without
result consumption also explicitly join before owner destruction.

Startup assertions now follow the scoped contract: reserved child startup
survives discarded ordinary submissions and worker admission closure. Generator
ordinary-startup discard still produces a fault. The former initial ordinary-task
discard section was removed; queued yield and timer discard remain tested.
The synchronous submission-handoff test now exercises YieldNow in an admitted
root. The timer inline test observes coroutine resumption directly and subtracts
startup delivery when checking timer/continuation deliveries. Single-slot yield
reuse, allocation-failure injection after admission, foreign-executor cleanup,
and linked cancellation checks remain intact.

Async_TaskContextTests passed in the existing Debug tree: 14 cases and 30,138
assertions. No production changes, sanitizer or benchmark reruns, or broad
integration builds. Logs: owned-task-context-build.log and
owned-TaskContext-tests.log under build/async-runtime-evidence. Remaining global
Spawn calls within tests/Async are in AsyncGenerator.cpp, Cancellation.cpp, and
ScopeAdmission.cpp; other Base test/benchmark callers also keep T13 open.


## Final Async test callers — 2026-09-13

Migrated 20 global Spawn calls in AsyncGenerator.cpp, Cancellation.cpp, and
ScopeAdmission.cpp. All tests/Async callers now use explicit scoped ownership.
Generator reservation tests keep owner publication on a separate scheduler to
preserve bounded consumer reservations. Early consumer cancellation occurs after
root admission but before cold execution, and inline generator boundary delivery
is observed at the coroutine continuation before later root-result publication.
Unconsumed concurrent consumer roots and external child operations are explicitly
joined. Scope admission tests keep their original factory and reservation checks.

The inherited cancellation test measures execution-boundary storage after owner
admission using the environment's PMR resource. It checks zero allocation without
suspension and exactly two boundary allocations with suspension, across helper
depths 1, 8, and 64; root ownership/linkage storage is outside that measurement.

Async_AsyncGeneratorTests, Async_CancellationTests, and Async_ScopeAdmissionTests
passed in the existing Debug tree: 36 cases, 12,520 assertions, including existing
concurrent generator delivery and retirement loops. No production changes,
sanitizer or benchmark reruns, or broad integration builds. Logs under
build/async-runtime-evidence: owned-generator-cancellation-build.log and
owned-{AsyncGenerator,Cancellation,ScopeAdmission}-tests.log. The remaining
global calls are in Base I/O, networking, package-consumer tests, and benchmarks;
T13 remains open until those callers and legacy producer ownership are removed.


## File-handle, runtime-loop, and leaf-placement roots — 2026-09-13

Migrated 12 global Spawn calls across IO/AsyncFileHandleTests.cpp,
IO/RuntimeLoopTests.cpp, IO/ExecutorPlacement.cpp, and Net/ExecutorPlacement.cpp.
File-handle tests preserve admission before/after handle movement and explicitly
join their root owners. Runtime-loop roots publish on a separate cooperative
scheduler, preserving timer-capacity, PollOnce backlog, and per-batch transfer
assertions while permitting shutdown cleanup before owner destruction. File and
network placement tests still assert their selected executor inside the task,
then drive the source owner to publish the final result.

The four focused Debug targets passed: 30 cases and 8,638 assertions. Existing
build/tests configuration was reused; its I/O/network dependencies rebuilt as
needed. No production changes, benchmarks, sanitizer reruns, or broader
integration runs were added. Evidence under build/async-runtime-evidence:
owned-io-placement-build.log and owned-{IO_AsyncFileHandleTestsTests,
IO_RuntimeLoopTestsTests,IO_ExecutorPlacementTests,Net_ExecutorPlacementTests}.log.
Remaining I/O, network, package-consumer, and benchmark callers and legacy
producer ownership removal keep T13 open.


## I/O dispatch and runtime-runner ownership — 2026-09-13

Migrated 17 global Spawn calls in IO/AsyncDispatchTests.cpp and
IO/RuntimeRunnerTests.cpp. Explicit supervisors own worker-dispatch roots and
external operations awaited during runtime stop. Capacity-sensitive runtime roots
publish owner results on separate cooperative schedulers, preserving single-slot
execution and the original WhenAll/Race reservation limits. Reservation exhaustion
is observed at scoped admission before body entry. Worker-thread result polling
also waits for owner retirement before joining.

The dispatch-capacity regression now uses explicit externally published roots
instead of hidden SyncWait ownership, keeping its occupied reservation aimed at
worker completion admission; recovery remains checked after releasing that slot.
The inline-delivery counter accounts for reserved startup independently of the
optional deferred completion. Unsupported completion executors are still tested
inside a tracked root before any worker side effects.

Both focused Debug targets passed: 16 cases, 135 assertions. Existing build/tests
configuration was reused; no production changes, sanitizer or benchmark reruns,
or broader integration runs. Evidence under build/async-runtime-evidence:
owned-io-dispatch-runner-build.log, owned-IO_AsyncDispatchTestsTests.log, and
owned-IO_RuntimeRunnerTestsTests.log. Remaining Base callers and legacy producer
ownership removal keep T13 open.


## File-operation gate test roots — 2026-09-13

Migrated 30 global Spawn calls in IO/FileOperationGateTests.cpp. Individual roots
have explicit supervisors, the ordered flush test uses a bounded owner for its
operation array, and concurrent producers reuse bounded supervisors across all
16 rounds before final join. The two-slot completion-exhaustion test publishes
owner results on a separate scheduler, preserving the original execution limit.
Queue ordering, independent-offset overlap, cancellation/release races,
registration failure, backend rollback, close admission, and shared driver-budget
assertions remain intact.

IO_FileOperationGateTestsTests passed in the existing Debug tree: 11 cases and
5,724 assertions. No production changes, sanitizer or benchmark reruns, or broader
integration builds. Logs: owned-file-gate-build.log and owned-file-gate-tests.log
under build/async-runtime-evidence. Remaining Base callers and legacy producer
ownership removal keep T13 open.


## Resolver, network runtime, and socket roots — 2026-09-13

Migrated 30 global Spawn calls across Net/Resolve.cpp, Net/RuntimeTests.cpp,
and Net/NetSocketTests.cpp. Each admitted root has an explicit supervisor and is
joined before result inspection or owner destruction. Existing resolver timeout
retirement, driver-shutdown delivery, runtime socket close, duplicate-wait
rejection, loopback transport, partial receive, EOF, cancellation, and refused
connection assertions remain intact.

All three focused Debug targets passed: 40 cases and 738 assertions. Reused
build/tests; no production changes, sanitizer or benchmark reruns, or broad
integration builds. Evidence under build/async-runtime-evidence:
owned-net-resolver-runtime-build.log, owned-net-sockets-build.log,
owned-Net_ResolveTests.log, owned-Net_RuntimeTestsTests.log, and
owned-Net_NetSocketTestsTests.log. Remaining native I/O, socket-operation, TLS,
package-consumer, and benchmark callers and legacy producer removal keep T13 open.


## Socket-operation roots — 2026-09-13

Migrated 29 global Spawn calls in Net/SocketOperationTests.cpp. Explicit owners
cover cold/pending socket movement, wrapper destruction, concurrent direction
admission, cancellation and descriptor reuse, and duplex read/write operations.
Unconsumed or separately observed results also have their owners joined. The
single-slot send rejection test publishes owner results externally, and the
inline delivery test keeps owner publication outside its delivery counter.
Placement checks still execute on the selected leaf executor before the source
owner publishes its result.

Net_SocketOperationTestsTests passed in the existing Linux Debug tree: 13 cases
and 992 assertions. The Windows-only IOCP truncation case was migrated but remains
unverified on this host. No production changes, sanitizer or benchmark reruns,
or broader integration builds. Evidence: owned-socket-operation-build.log and
owned-socket-operation-tests.log under build/async-runtime-evidence. Native I/O,
TLS, package-consumer and benchmark callers and legacy producer removal keep
T13 open.


## Native I/O and TLS ownership — 2026-09-13

Migrated 34 global Spawn calls in IO/NativeRuntimeTests.cpp and Net/TLS.cpp.
Native I/O roots use explicit supervisors, including a bounded owner for the
96-read batch. Opening roots join before backend-availability checks; existing
native/fallback ordering, descriptor lifetime, cancellation, and stop-reason
checks remain. TLS handshake and shutdown peers use separately joined scopes;
the concurrent TLS read joins its scope before read-buffer/result inspection.

Eight native I/O cases passed in build/tests Debug (511 assertions), and all six
TLS cases passed in the existing OpenSSL-enabled build/tests-tls Debug tree
(190 assertions), with no skips reported. Only those targets and their necessary
dependencies were built. No production changes, sanitizer or benchmark reruns,
or broader integration runs. Logs under build/async-runtime-evidence:
owned-native-runtime-{build,tests}.log and owned-tls-{build,tests}.log.

The only remaining global Spawn calls in Base tests are the eight package-consumer
fixture calls. Benchmark callers and legacy producer ownership removal also keep
T13 open; native Windows/macOS validation remains a separate unmet gate.


## Installed package-consumer ownership — 2026-09-13

Migrated the last eight global Spawn calls in tests/PackageConsumer/main.cc.
Execution and IO component consumers now instantiate explicit supervisor owners,
join before result inspection, and drive the source owner after executor transfer.
Ownership headers remain inside their respective component include branches.
There are no remaining global Spawn calls in Base tests; 35 benchmark calls remain.

Ran the canonical installed-consumer checks for Execution and IO in Debug with
GCC 15.2 and two build jobs. Execution passed. IO built and installed its component
package, then exposed an invalid multiline condition in the edited consumer.
After correcting it, rebuilt only the existing IO consumer tree and its canonical
NGINBasePackageConsumer.Run test passed. No package rebuild was repeated after
that consumer-only correction. Static installed packages were checked; this batch
did not repeat shared/native-platform or performance gates.

Evidence under build/async-runtime-evidence: owned-package-consumers-tests.log,
owned-package-io-rebuild.log, and owned-package-io-tests.log. T13 remains open for
benchmark caller migration and removal of global Spawn and legacy producer
ownership; no benchmark timings were run in this batch.


## Runtime and hardening benchmark ownership — 2026-09-13

Migrated nine global Spawn calls across AsyncRuntimeBenchmarks.cpp,
HardeningBenchmarks.cpp, and IORuntimeBenchmarks.cpp. Measured runtime and Race
paths now admit explicit owners and join before consuming their result and ending
timing. The network cancellation interval includes asynchronous owner join.
Already-canceled chain workloads use a live admission context while retaining the
explicit canceled leaf context, so they measure suspension cancellation rather
than rejected root admission. This applies to both manually driven and SyncWait
chain workloads.

AsyncRuntimeBenchmarks, AsyncRuntimeAllocationBenchmarks, HardeningBenchmarks,
and IORuntimeBenchmarks compiled successfully in the existing Release benchmark
tree. No benchmark binaries or timing comparisons were run. Runtime correctness
of these edited fixtures and performance gates remain to be exercised at the
benchmark milestone. Their ownership/admission costs have changed, so historical
numbers must not be treated as measurements of these new fixtures. Build evidence:
build/async-runtime-evidence/owned-runtime-benchmarks-build.log.

Twenty-six global benchmark callers remain; global Spawn and legacy producer
ownership removal still keep T13 open.


## Network and mixed-workload benchmark ownership — 2026-09-13

Migrated six global Spawn calls in IORuntimeNetworkBenchmarks.cpp and
IORuntimeMixedBenchmarks.cpp. The receiver fixture owns its bounded concurrent
receives with a supervisor and joins that owner during cancellation/cleanup before
releasing buffers. Mixed workloads await sequential file open, transfer, and close
through AsCompletion. Each overlapping UDP exchange owns its receive in a local
scope, observes send failure, closes the receiver if needed, and joins before
checking outcomes or reusing buffers.

Both Release benchmark targets compiled in the existing benchmark tree. No
benchmark binaries were run, so runtime fixture validation and performance
comparisons remain pending at the milestone. Mixed-workload timing now reflects
sequential composition for nonconcurrent work and explicit joining for concurrent
receives; historical timings are not evidence for these updated workloads.
Build evidence: build/async-runtime-evidence/owned-network-mixed-benchmarks-build.log.
Twenty global benchmark callers remain, along with legacy producer/API removal
under T13.


## Scheduler benchmark ownership — 2026-09-13

Removed all 11 global Spawn calls from SchedulerBenchmarks.cpp. Eight bulk task
workloads use bounded supervisors created during setup, admit tasks within the
timing interval, and end timing only after owner retirement and completion-count
validation. Thread-pool workloads asynchronously join through a live joining
context; cooperative workloads drive pending roots to retirement and collect the
join report. The two cancellation observers use sequential AsCompletion awaits.

Removed the FiberScheduler Task<void> yield workload because FiberScheduler has
no completion-reservation capability and cannot admit structured tasks. Its raw
fiber/coroutine scheduling workloads remain. The changed task workloads measure
retirement as well as body completion, so historical samples are not directly
comparable without an explicit baseline update.

SchedulerBenchmarks compiled successfully in the existing Release tree. No timing
or smoke execution was performed; benchmark runtime validation remains pending at
the milestone. Build evidence:
build/async-runtime-evidence/owned-scheduler-benchmarks-build.log. Nine global
callers remain in the two I/O allocation benchmark fixtures; T13 still requires
those migrations and legacy producer/API removal.


## I/O allocation benchmark ownership — 2026-09-13

Migrated the final nine global Spawn callers in the runtime and file allocation
fixtures. Bounded supervisors include a spare slot per producer for overload
probes; sinks accept expected errors while retained operations verify their exact
outcomes. Cleanup cancels and joins owners before destroying buffers or contexts.
File rejection storage is checked after warming each of the four producer owners.
Owner construction, admission, and retirement are included in allocation totals.

Both Release targets compiled. IORuntimeAllocationBenchmarks and
IORuntimeFileAllocationBenchmarks executed successfully, including native and
fallback file backends, runtime overload rejection, and zero retained allocation
growth after each fixture round. The runtime fixture initially failed its stale
callback expectation; the final check accounts for three ownership callbacks per
receive and inline socket delivery on the runtime executor, for both ready and
suspended inputs. Failure logs and the successful rerun are preserved in
build/async-runtime-evidence/owned-io-allocation-*.log and
owned-file-allocation-run.log. These instrumented runs establish allocation and
behavior checks, not latency acceptance or cross-platform validation.

The source audit finds no remaining global Spawn callers in Base implementation,
tests, benchmarks, Packages, or Examples. T13 remains open: the unused global API
and public Operation's legacy producer representation still require removal.


## Public result and internal producer separation — 2026-09-13

Removed global Spawn and its friend declarations after the caller audit. Public
Operation now has one implementation for value and void results and retains only
scope result state. Moves are noexcept; no public constructor accepts a coroutine
frame or inline Completion. Compile-time negative checks cover both value and void
forms. TaskScope and IO::RunTask retain a separate detail::ProducerOperation through
terminal observation and frame retirement. RunTask preserves root locals until its
children finish cleanup. The async guide now describes this API and uses SyncWait
for standalone examples.

Six focused Debug suites passed: ScopedOperation, TaskScope, TaskSupervisor, Ready,
RuntimeRunnerTests, and RunTaskTests (46 cases, 850 assertions). The first build
caught missing NoError template arguments in the new negative checks; corrected
checks and all implementation targets compiled. ScopedOperation, RuntimeRunner, and RunTask
also passed ASan/UBSan (22 cases, 137 assertions). Evidence is under build/async-runtime-evidence with
operation-producer-split-* and producer-split-* names. The source audit contains
only scoped/supervisor Spawn members, with no global definition, declaration, or
callers. T13's final contract audit and subsequent plan gates remain open.


## T18 timed runtime drainage — 2026-09-13

Added Runtime::ShutdownUntil(monotonicDeadline), returning Stopped or Incomplete.
The owner drives bounded batches up to the deadline; foreign callers wait only
with an active committed driver. Incomplete preserves runtime ownership and all
reserved cleanup delivery, and an active runner keeps driving. Stop closes new
admission even for an already expired deadline. Later polling or Shutdown completes
drainage; no task is forcibly destroyed. Blocking Shutdown uses the same common
path without a deadline. Relative condition-variable waits are bounded to avoid
platform-clock overflow for distant absolute deadlines.

The API and I/O guides distinguish runtime/backend drainage from external executor
delivery and application joining. Deadlines cannot preempt callbacks or backend
stop hooks, and destruction still drains normally. Three new tests cover expired
owner shutdown, subsequent successful timed drainage, timed foreign waiting with
a live runner, rejection of new work, callback misuse, and inactive foreign owners.
Four focused Debug suites passed (RuntimeRunner, RunTask, Runtime, RuntimeLoop):
47 cases and 8573 assertions. Evidence: build/async-runtime-evidence/timed-shutdown-*.log.
This checkpoint does not establish native Windows/macOS or final latency acceptance;
T18 remains open for the complete owner/runtime shutdown audit.


## Ownership and delivery boundary checks — 2026-09-13

Added a pending UDP receive with separate cooperative delivery and owner-publication
executors. There is no application cancellation source. Runtime ShutdownUntil drains
backend work and reports Stopped while the consumer and owner remain pending and
borrowed task locals remain alive. Driving delivery observes RuntimeShutdown; only
driving the publication executor then completes the public result and permits Join.
The Net Runtime suite passes all 10 cases and 70 assertions.

Added the isolated NGINBaseScopeLifetimeFixture, compiled with NDEBUG even in the
Debug tree. Its terminate handler exits with a distinct code, and a CMake test
checks that exact code for live TaskScope destruction, live TaskSupervisor
destruction, and an active Join. A fully joined control must exit successfully.
Base.Async.ScopeLifetime.ReleaseFailFast passed all four subprocess modes. This
establishes the release-mode guard behavior without aborting the main test runner.
Evidence: build/async-runtime-evidence/shutdown-delivery-boundaries-*.log and
scope-release-failfast-*.log. Native Windows/macOS execution and the remaining
full Stage 3 contract audit are not claimed by these focused Linux checks.


## T13 closure audit — 2026-09-13

Added live-child WithScope coverage for body return, exception, domain failure,
and parent cancellation. Each checks child retirement and removal of pending
timers before result consumption. WithScope, ScopeAdmission, and ScopeAllocation
pass 11 Debug cases / 120 assertions. The requirement-to-implementation/test
mapping in [Stage3Audit.md](Stage3Audit.md) completes T13 locally and identifies
specific remaining T17/T18 integration checks. No native or final performance
gate is waived. Evidence: build/async-runtime-evidence/scope-closure-audit-*.log.


## Local Stage 3 integration exit — 2026-09-13

Rebuilt the file external-delivery/native-shutdown suites, all UI test targets,
and the combined 11-binary scope/observer/race/deadline/supervisor batch. File
checks passed 16 cases / 590 assertions; UI passed all 215 discovered tests;
combined async checks passed 84 cases / 2789 assertions. No implementation fixes
were required. [Stage3Audit.md](Stage3Audit.md) closes the named integration gaps
and T17/T18 locally. Source was held fixed during builds and verification.
Evidence: build/async-runtime-evidence/stage3-{file,ui-integration,combined}-*.
Stage 4 integration and final native/consumer/performance gates remain open.
