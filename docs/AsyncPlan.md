# AsyncPlan

Purpose: define the async core changes required to support builds with exceptions disabled while preserving performance
and ease of use.

Constraints:
- Must compile and run with exceptions disabled.
- Preserve low overhead on hot paths.
- Keep call sites ergonomic for engine-style code.

## Current Behavior (Today)

- Task<T> stores a value or AsyncError; exception_ptr capture is optional via NGIN_ASYNC_CAPTURE_EXCEPTIONS.
- co_await/Get on Task<T> return AsyncExpected<T> and never throw.
- TaskContext::Yield/Delay return AsyncExpected<void>; cancellation returns AsyncErrorCode::Canceled.
- WhenAll/WhenAny return AsyncExpected results and surface cancellation as AsyncErrorCode::Canceled.
- Fiber backends terminate on unrecoverable errors instead of throwing.
- Async code uses error-as-data rather than exceptions for control flow.

## Core Tradeoffs in the Current Design

1. AsyncExpected handling is now required at call sites.
   - Clear control flow but more boilerplate in user code.
2. Cancellation is modeled as an error code, not an exception.
   - Requires consistent checking, but is predictable and cheap.
3. Exception details are optional.
   - When capture is disabled, faults carry only AsyncErrorCode::Fault and native codes.
4. Fiber failures are treated as unrecoverable invariants.
   - This favors correctness and determinism over recovery in no-exception builds.

## Proposed Error Model (No-Exceptions)

Introduce an explicit async error type and propagate it via values.

Option A (recommended):
- Add NGIN::Async::AsyncErrorCode (Ok, Canceled, Fault, TimedOut, InvalidState, etc.).
- Add NGIN::Async::AsyncError { AsyncErrorCode code; int native; }.
- Add NGIN::Async::AsyncExpected<T> = std::expected<T, AsyncError>.

This mirrors NetExpected and keeps the error channel cheap and explicit.

## Task API Changes

Goal: remove exception propagation and make errors explicit.

- Task<T> becomes a carrier of AsyncExpected<T> at the await boundary.
  - Option 1: Task<AsyncExpected<T>> directly.
  - Option 2: keep Task<T> and add TaskResult<T> = AsyncExpected<T> returned by Get/await.

Recommended for clarity:
- Keep Task<T> signatures, but make co_await/Get return AsyncExpected<T>.
- Provide helpers to reduce boilerplate:
  - NGIN_TRY or a small helper function to unwrap expected values.
  - For Task<void>, use `Task<void>::ReturnError(...)` to record a fault before `co_return`.

Define new rules:
- Awaiting a Task never throws.
- Cancellation returns AsyncError{AsyncErrorCode::Canceled}.
- Faults return AsyncError with a native code if available.
- If exceptions are enabled, unhandled exceptions are converted to AsyncErrorCode::Fault.

## TaskContext and Awaitables

- TaskContext::Yield/Delay return AsyncExpected<void>.
- TaskContext::CheckCancellation returns AsyncExpected<void> (ThrowIfCancellationRequested removed).
- CancellationToken::IsCancellationRequested remains a cheap check.

## Combinators

- WhenAll returns Task<Tuple<...>>; co_await/Get return AsyncExpected<Tuple<...>> and stop on first failure or cancellation.
- WhenAny returns Task<IndexType>; co_await/Get return AsyncExpected<IndexType> and expose completion index.
- Document whether WhenAny treats canceled tasks as completion or failure.

## Execution / Scheduler Integration

- Executor and scheduler interfaces remain unchanged; only completion paths change.
- Continuations must carry AsyncExpected values instead of exceptions.

## Fiber Backends

Replace throw sites with explicit error handling:
- Convert fiber creation failures to AsyncError or hard-fail (NGIN_ASSERT + abort) based on policy.
- Ensure fiber switching does not require exceptions for unwinding.

Decision: define a policy for unrecoverable errors in no-exception builds:
- Option A: return AsyncError::Fault.
- Option B: terminate for unrecoverable internal errors.

Chosen policy:
- Abort/terminate for unrecoverable internal invariants (invalid executor, corrupted state, fiber init that cannot be recovered).
- Return AsyncErrorCode::Fault for recoverable runtime failures.

## Interop Policy

- If exceptions are enabled, do not surface them to callers; convert to AsyncError::Fault.
- Avoid dual-mode behavior where the same API sometimes throws and sometimes returns errors.
- Add compile-time flag `NGIN_ASYNC_CAPTURE_EXCEPTIONS` (default on) to capture exception_ptr on fault and expose
  a read-only accessor for diagnostics. No control-flow changes.
- In no-exception builds, the flag is ignored and exception capture is compiled out.

## Pros and Cons of the Proposed Changes

Pros:
- Works in engine builds with exceptions disabled.
- Uniform control flow: errors and cancellation are explicit values.
- Easier to reason about hot-path costs (no hidden throw/stack unwind).
- Aligns async error handling with NetExpected style.
- Optional exception capture enables detailed fault logging without changing behavior.

Cons:
- Call sites must check expected values consistently (more boilerplate).
- Converting existing exception-based code requires broad API changes.
- Some failure modes lose rich type info unless explicitly encoded in AsyncErrorCode.
- Capturing exception_ptr adds Task state size and minor fault-path cost when enabled.

## Migration Strategy (Ordered)

1. Introduce AsyncErrorCode/AsyncError/AsyncExpected in NGIN::Async.
2. Update Task/TaskContext to propagate AsyncExpected and remove throw sites.
3. Add exception capture flag and accessor, compiled out in no-exception builds.
4. Update WhenAll/WhenAny and other combinators.
5. Update fiber backends to avoid throw and return explicit failures.
6. Update all async consumers (NetworkDriver, sockets, transports) to return AsyncExpected.
7. Update docs and add tests for cancellation/error paths.

## Open Questions

Resolved decisions:
- Use a separate AsyncError type (not NetError).
- Keep Task<T> signatures; co_await/Get return AsyncExpected<T>.
- Exception capture is opt-in via a Task-level accessor under NGIN_ASYNC_CAPTURE_EXCEPTIONS (default on).
- Cancellation policy for WhenAll/WhenAny: first-cancel wins.

Remaining question:
- None (policy chosen above).
