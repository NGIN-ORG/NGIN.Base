# Local Stage 4 acceptance audit

Audit date: 2026-09-13. T19–T21 are complete locally. Native Windows/macOS
execution remains explicitly required by T25/T26; final correctness, installed
consumers, examples, and end-to-end performance remain under T22–T24. This audit
does not promote synthetic improvements into original-baseline acceptance.

| Task | Implementation/contract evidence | Local verification |
| --- | --- | --- |
| T19: completion integration | Existing native request and fallback worker awaiters use admitted completion, context executor placement, backend retirement, and RuntimeShutdown origin. Public handles retain state through cold/pending moves. | NativeRuntime tests cover native/fallback position/close behavior, unclaimed descriptors, stop, and delivery after native completion. SocketOperation tests cover descriptor reuse, selected external executor, ready/pending dispatch, and a write completing while the same socket's read remains pending. NetSocket covers loopback, vectored I/O, transports, cancellation, and EOF. |
| T20: remove redundant gate coroutine | FileOperationLease now lives in each platform backend coroutine. RunFileOperation/WithFileOperation are removed. Request/rollback guards destroy before lease release. Public wrapper retains its distinct borrowed-provider-state contract. | FileOperationGate covers independent offset overlap, position ordering, flush and close barriers, queued cancellation, close rollback on every terminal channel, shared capacity, and rejected admission. AsyncFileHandle covers cold/pending state retention. Five-process synthetic comparison records latency; instrumentation proves one fewer allocation and 664 fewer requested bytes per gated/public operation. |
| T21: admission and overload | ResourceBounds.md records each capacity, acquisition/release, and reserved delivery guarantee; caller-held result history and explicit observer-set sizes are distinguished. | Runtime allocation overload covers submissions, completions, timers, and socket capacities. Integrated native/fallback file fixture verifies zero retained allocation growth after teardown. Gate tests cover concurrent producers and canceled-slot recovery. Scope/observer allocation rollback and saturated delivery are checked. |

The final Stage 4 batch rebuilt FirstCompleted, SocketOperation, NetSocket,
NativeRuntime, and FileOperationGate, and passed 64 Debug cases / 8589 assertions.
No runtime skip was reported. Windows-only cases are compiled out on Linux and
are not claimed as executed. The new two-mode observer check exhausts the
observer executor's completion capacity, then verifies terminal notification and
wait cancellation use already admitted delivery while source work remains owned.

Relevant prior checkpoints in Stage4.md record 23 ASan/UBSan file cases and the
native/fallback overload fixture. The final stop-origin and observer-saturation
additions were Debug-validated; T22 must include them in the final sanitizer batch.
Existing runtime loop/runner shutdown, host notification, and file worker transfer
checks are recorded in Stage3Audit.md and the timed-shutdown checkpoints.

Evidence: build/async-runtime-evidence/stage4-acceptance-*.log, together with the
specific logs linked in Stage4.md and ResourceBounds.md. Next work is the single
Linux T22 correctness/integration milestone, not a repeat of individual step builds.
