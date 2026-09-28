# Stage 4 — file and socket integration

## File ownership map and pre-change measurement — 2026-09-13

T19/T20 remain open. The current native/fallback path is public DispatchData or
DispatchControl → RunFileOperation gate coroutine → platform LocalAsyncFile*
coroutine → native request or worker dispatch awaiter. Global Spawn has already
been removed, but the gate coroutine still adds an allocation and frame.

| Responsibility | Current owner | Constraint on replacement |
| --- | --- | --- |
| Cold handle state lifetime | Public wrapper's by-value shared state parameter | Preserve state if the handle is moved/destroyed before the cold task starts; backend functions currently borrow that shared_ptr by reference. |
| Early executor placement and invalid handle outcome | Public wrapper | Preserve for custom operation tables as well as built-in files. |
| Service admission and file ordering | FileOperationGate::Admission node in RunFileOperation frame | Move the lease into the actual backend coroutine; preserve position serialization, independent offsets, flush/close barriers, and rejection before OS work. |
| Backend result, cancellation, and buffer retirement | NativeFileAwaiter or DriverDispatchAwaiter, retained by backend frame | Do not release the gate or buffer borrowing before request/callback retirement. |
| Native close rollback | RestoreUnsubmittedClose in platform close coroutine | Its destructor restores an unsubmitted handle before gate release decides NativeIsOpen. Declare any integrated lease earlier so rollback destroys first. |
| Error/stopped publication | Sequential AsCompletion in gate wrapper | Integrated lease must release on every terminal path, including failure suspension and exceptions, before the public parent resumes. |

The intended replacement is a non-coroutine lease in each backend operation,
reusing the existing Admission node and native/worker awaiter state. It must not
introduce another Task wrapper. Public wrapper removal is a separate lifetime/API question; the contract decision below explains why it remains necessary.

### Matched synthetic baseline

Added `--file-only` to AsyncRuntimeBenchmarks and AsyncRuntimeAllocationBenchmarks.
No argument retains the full existing run. This selects the existing three-layer
fixture without running unrelated chain/cancellation workloads. Both Release
targets compiled. Five uninstrumented processes and one allocation-instrumented
process ran successfully, 1000 samples per row after warmup. Layer 0 is backend
leaf, 1 adds the actual gate, and 2 adds the public handle wrapper. The table is
the median of each reported statistic across five processes, not pooled samples.

| Workers | Layer | Mean µs | P50 µs | P95 µs | P99 µs |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 1.786 | 1.701 | 1.765 | 2.914 |
| 0 | 1 | 2.007 | 1.873 | 2.613 | 3.819 |
| 0 | 2 | 2.041 | 1.953 | 2.090 | 2.939 |
| 1 | 0 | 32.002 | 26.449 | 63.071 | 88.526 |
| 1 | 1 | 33.806 | 26.762 | 66.949 | 94.725 |
| 1 | 2 | 34.134 | 27.990 | 64.907 | 93.171 |
| 4 | 0 | 45.811 | 30.489 | 117.680 | 165.030 |
| 4 | 1 | 43.511 | 30.330 | 102.426 | 148.700 |
| 4 | 2 | 42.773 | 30.136 | 100.308 | 146.687 |

The instrumented run reports 13/14/15 allocations and 3264/4912/5840 requested
bytes per sample for layers 0/1/2, identically across runtime and one-/four-worker
placement. Root ownership is included. Instrumented latency is excluded from the
table. These measurements establish a pre-change comparison; they do not measure
OS transfer costs or satisfy native/fallback end-to-end performance acceptance.

Evidence: build/async-runtime-evidence/stage4-file-baseline-*.log. Preserve these
samples when comparing the integrated implementation. Existing historical
pre-redesign gates remain independently required.


## Integrated backend admission lease — 2026-09-13

Removed RunFileOperation and WithFileOperation. Linux, non-Linux POSIX, and
Windows operation tables now call their backend functions directly. Each backend
coroutine owns a FileOperationLease around its existing admission node. It checks
rejection/closed/runtime-stop outcomes before submitting OS work; inline ready
Completion propagation creates no additional coroutine. The lease releases on
backend-frame destruction, after later-declared native close rollback guards.
Existing native and worker request retirement remains unchanged. Public handle
wrappers continue retaining cold state and establishing executor placement.

The close rollback regression fixture now exercises the same integrated lease,
including success, stop, infrastructure fault, and domain failure. The synthetic
benchmark embeds admission in its backend leaf instead of measuring a retained
wrapper. Source audit finds no RunFileOperation/WithFileOperation callers or definitions.

Debug FileOperationGate, NativeRuntime, and AsyncFileHandle suites pass 23 cases /
6360 assertions. Review corrected misplaced lease code in the non-Linux POSIX
flush/close entries before completion; those platform branches have not been
executed here. Sanitizer and native Windows/macOS validation remain required.

Five uninstrumented post-change processes and one separately instrumented process
passed. Allocation counts for leaf/integrated-gate/public paths are now 13/13/14,
versus 13/14/15 previously. Requested bytes are 3264/4248/5176 versus
3264/4912/5840: one allocation and 664 bytes removed per gated/public operation.
Median process means follow; raw per-process percentiles remain in the logs.

| Workers | Layer | Before mean µs | After mean µs | Change |
| --- | --- | --- | --- | --- |
| 0 | 0 | 1.786 | 1.780 | -0.3% |
| 0 | 1 | 2.007 | 1.882 | -6.2% |
| 0 | 2 | 2.041 | 2.030 | -0.5% |
| 1 | 0 | 32.002 | 35.462 | +10.8% |
| 1 | 1 | 33.806 | 34.115 | +0.9% |
| 1 | 2 | 34.134 | 34.352 | +0.6% |
| 4 | 0 | 45.811 | 47.241 | +3.1% |
| 4 | 1 | 43.511 | 52.546 | +20.8% |
| 4 | 2 | 42.773 | 56.930 | +33.1% |

These short synthetic runs show structural cost reduction, not native/fallback
end-to-end latency acceptance. Scheduling noise affects pool samples; the original
historical performance gates remain unchanged. T19/T20 stay open for platform and
resource-behavior verification and end-to-end performance evidence.
Evidence: build/async-runtime-evidence/integrated-file-lease-*.log.


## Lease sanitizer, overload, and public-wrapper audit — 2026-09-13

The integrated FileOperationGate, NativeRuntime, and AsyncFileHandle suites passed
ASan/UBSan (23 cases). The native/fallback file allocation overload fixture rebuilt
and passed 16 rounds per backend, with zero retained bytes above baseline after
full teardown. This is evidence for bounded retirement under rejection, not a
native-platform or end-to-end latency gate.

The public wrapper remains intentionally: AsyncFileHandle::Operations callbacks
accept shared state by const reference, and custom providers in AsyncFileHandleTests
forward that reference into cold coroutine helpers. DispatchData/DispatchControl
own the by-value state before suspension and establish context executor placement.
The existing tests prove preservation across handle moves, handle destruction,
invalid binding replacement, and abandoning a cold task. Removing this wrapper
would require changing the backend-provider contract; it is not a redundant
ordering/spawn layer. T20's redundant gate coroutine has been removed, while this
public lifetime boundary retains its concrete responsibility.

Admission review found a stop-origin bug inherited from the wrapper: runtime
rejection mapped to Canceled and read an uncanceled context's default Requested
reason. Admission now distinguishes the cancellation callback path from runtime
rejection; the integrated lease propagates that reason. A new two-mode regression
checks RuntimeShutdown versus explicit Deadline, no backend invocation, and an
open file/admission boundary. The FileOperationGate Debug suite passes after this
fix. The final reason-only change was not rerun under sanitizers; the 23-case
sanitizer evidence precedes it. No execution handles remain active.

Evidence: build/async-runtime-evidence/integrated-file-lease-asan-*,
integrated-file-overload-*, and file-admission-stop-origin-*.


## Resource-bound ledger — 2026-09-13

Added [ResourceBounds.md](ResourceBounds.md), mapping runtime/service/owner resources
to capacity acquisition, release, reserved delivery, and existing overload evidence.
It distinguishes bounded live admission from caller-retained result/diagnostic
history and explicitly sized observer sets. Corrected stale IORuntimeContracts
text that claimed all admission used one lock and prohibited current authorized
inline completion. This documentation-only audit required no build. T21 remains
open for its remaining acceptance coverage; no whole-platform claim is made.


## Local Stage 4 exit — 2026-09-13

Added explicit saturated observer-delivery coverage for terminal notification and
wait-only cancellation; both use admitted storage and preserve source ownership.
The combined FirstCompleted/SocketOperation/NetSocket/NativeRuntime/FileOperationGate
batch passes 64 Debug cases / 8589 assertions. [Stage4Audit.md](Stage4Audit.md)
reconciles the implementation, resource ledger, and evidence and completes T19–T21
locally. T22–T27 retain final integration, documentation, consumer, performance,
and native-platform acceptance. Evidence: stage4-acceptance-*.log under
build/async-runtime-evidence. No native-platform result is implied by this exit.
