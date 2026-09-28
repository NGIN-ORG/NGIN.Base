# Stage 3 ownership audit

Local audit date: 2026-09-13. This records contract evidence, not completion of the
whole AsyncRuntime plan. Native-platform and final performance gates remain open.

## T13 — complete locally

| Requirement | Implementation and authoritative checks |
| --- | --- |
| R08: every spawn has an owner; dropped consumers retain no producer responsibility | Global Spawn and Detach are absent; TaskScope owns detail::ProducerOperation and public Operation stores only result state. ScopedOperation tests cover retained results, dropped interest, final leases, and owner destruction. Compile-time checks reject public frame/result construction. |
| R12: close and join on every supported body exit | WithScope catches body exceptions, closes admission, requests OwnerClosed, and awaits Join. WithScope tests now exercise success/early return, domain error, exception, and parent cancellation with an already-started child, checking destruction and timer retirement. |
| R12: no blocking/detaching destructor | TaskScope destructor terminates on pending work or active Join. NGINBaseScopeLifetimeFixture compiles with NDEBUG; exact termination exit status is checked for live scope, live supervisor, and active join, with a successfully joined control. |
| R13: prompt sibling stop, body precedence, stable bounded errors | TaskScope::State::Complete requests stop on child error. Join collects diagnostics from bounded children in admission order. TaskScope tests deliberately reverse completion order, check prompt stop, and verify errors outrank stopped. WithScope tests verify body precedence and retained move-only secondary errors. |
| Bounded admission and cleanup on rejection | ScopeAdmission tests reject before factory invocation and deliver admitted startup despite ordinary rejection. ScopeAllocation checks partial allocation cleanup, rejected record allocation, and final diagnostic release. |

Current closure batch: WithScope, ScopeAdmission, and ScopeAllocation pass 11 Debug
cases / 120 assertions. Earlier producer-separation evidence covers current public
Operation/TaskScope code (46 Debug cases and 22 ASan/UBSan cases); subsequent changes
to runtime timed drainage do not change those scope implementations. The isolated
release guard test passes all four subprocess modes. New live-child body-exit cases
were run in Debug; they are not claimed as newly sanitizer-validated.

Evidence under build/async-runtime-evidence: scope-closure-audit-*,
producer-split-*, operation-producer-split-*, and scope-release-failfast-*.

## T17/T18 and local Stage 3 exit — complete

The named integration checks now pass against the removed global API and current
shutdown implementation:

- UI: NGINUITests, NGINUIOwnershipTests, and NGINUIAsyncTests rebuilt in the existing
  build/ngin-ui tree; all 215 discovered tests pass. This includes command,
  validation, view-model, application shutdown, and reserved executor delivery.
  The build log contains no compiler warnings or errors.
- File shutdown: AsyncDispatchTests and NativeRuntimeTests rebuilt and passed
  16 cases / 590 assertions, including worker-borrow retirement before external
  delivery and queued native-file cancellation with RuntimeShutdown origin.
- Combined scope/observation/race/deadline/supervisor batch: all 11 binaries rebuilt
  as needed and passed 84 Debug cases / 2789 assertions. This exercises the common
  ownership protocol after public Operation/producer separation.
- Timed runtime drainage and external socket delivery were already checked against
  the same shutdown code: incomplete status preserves ownership; runtime Stopped
  does not imply external application joining. See the linked Stage3 checkpoints.

Evidence under build/async-runtime-evidence: stage3-ui-integration-*,
stage3-file-*, and stage3-combined-*. No source fixes were needed in this integration
batch. Source remained unchanged throughout builds and test execution.

Stages 0–3 are complete locally. T19–T27 retain their own integration,
allocation/performance, consumer, and native platform requirements. These Linux
checks do not validate Windows/macOS execution, hosted/native Gallery behavior,
or final performance acceptance; no result here waives those gates.
