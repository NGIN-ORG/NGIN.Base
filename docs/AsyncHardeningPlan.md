# Async Hardening Plan

This plan records the API and implementation review findings for
`NGIN::Async` from the perspective of a third-party developer adopting
`NGIN.Base` directly. The current async model has a useful direction: cold
coroutine tasks, explicit `TaskContext`, typed domain errors, cancellation
separate from faults, and composition helpers. Before treating it as a stable
public foundation API, the lifetime and ownership rules need to become much
harder to misuse.

Compatibility is not a constraint for this plan. The async library currently
has no external users, so prefer a smaller, safer contract over preserving
existing surface area.

## Implementation Status

Implemented in the current hardening pass:

- scheduled roots detach instead of leaving queued coroutine handles dangling
- combinator watchers no longer depend on caller-owned task object lifetime
- running tasks enforce a single-consumer continuation contract consistently
- `TaskResult` owns shared completion storage instead of borrowing a frame
- `Get()` is completed-result inspection; `Wait()` is the explicit blocking
  primitive
- generator concurrent `Next()` misuse returns a fault instead of relying on
  release-disabled assertions
- `AsyncFault` owns its diagnostic message
- docs and examples use the hardened `Schedule(ctx)`/`TaskResult` contract

The remaining operational rule is intentional: a detached task frame is cleaned
up when the coroutine reaches final suspend. Code that suspends forever without
cooperating with cancellation is still an unbounded operation by definition.

## Goals

- Make normal-looking async code memory-safe by construction.
- Define one clear ownership model for scheduled tasks, awaited tasks,
  detached work, and task results.
- Make task composition structured: `WhenAll`, `WhenAny`, and continuations
  must not leave suspended helper coroutines referencing stack-owned tasks.
- Keep domain errors, cancellation, and runtime faults distinct.
- Keep the third-party developer experience explicit, documented, and easy to
  diagnose.

## Non-Goals

- Do not add legacy compatibility layers for current `Task`/combinator quirks.
- Do not hide scheduling behind global runtimes or implicit worker threads.
- Do not solve every high-level async abstraction before the core lifetime
  model is safe.

## Review Findings

### Critical: Scheduled Task Lifetime Can Dangle

`Task::Schedule()` queues the coroutine handle into the executor, while
`Task::~Task()` destroys the coroutine frame whenever the `Task` object dies.
This allows ordinary code such as scheduling a temporary or letting a scheduled
task leave scope before the executor drains to produce a use-after-free.

Affected areas:

- `include/NGIN/Async/Task.hpp`
- `Task<T, E>::Schedule`, `TrySchedule`, destructor, move assignment
- `Task<void, E>` equivalents

Required direction:

- Make scheduled coroutine frames owned by a stable shared operation state, or
  make root scheduling transfer ownership into an explicit runtime handle.
- Add an explicit detach/join story if fire-and-forget work is supported.
- Make destroying a not-yet-completed non-detached task either cancel-and-join,
  assert/fault in debug, or become impossible through the type system.
- Add tests that schedule a task, destroy or move the visible task object, and
  then drain the executor under ASan/UBSan.

### Critical: `WhenAny` and Early-Failing `WhenAll` Break Structured Concurrency

`WhenAny` and `WhenAll` create detached watcher coroutines that await
caller-owned task references. `WhenAny` returns as soon as the first task
finishes, leaving watchers for losing tasks potentially suspended against
stack-owned task objects. `WhenAll` has the same problem on early failure or
cancellation.

Affected areas:

- `include/NGIN/Async/WhenAny.hpp`
- `include/NGIN/Async/WhenAll.hpp`
- detached watcher coroutine helpers

Required direction:

- Redesign combinators to own child operations or require an explicit task
  scope that outlives every watcher.
- On `WhenAny`, either cancel and drain losing children before returning, or
  return a scope/result object that keeps losers alive and observable.
- On `WhenAll`, do not return early while helper coroutines can still reference
  caller-owned tasks.
- Add tests for local child tasks used with `WhenAny`, immediate parent return,
  loser completion after parent return, early child fault, and cancellation.

### Critical: Await Continuation Ownership Is Inconsistent

`Task` stores a single continuation handle. Propagation awaiters reject an
existing continuation, but `AsCompletion()` installs a continuation without the
same guard. That can overwrite a previous waiter and strand or resume the wrong
coroutine.

Affected areas:

- `Task<T, E>::PropagationAwaiter`
- `Task<T, E>::CompletionAwaiter`
- `Task<void, E>` equivalents
- `PromiseRuntimeCommon::m_continuation`

Required direction:

- Choose one contract:
  - single-consumer tasks, enforced consistently by every await path; or
  - multi-consumer tasks, backed by a continuation list/shared state.
- Prefer single-consumer by default unless a real multi-await use case exists.
- Make duplicate await attempts produce a deterministic fault, not silent
  continuation replacement.
- Add tests covering `co_await task` plus `task.AsCompletion()`, two
  `AsCompletion()` awaiters, and combinator watchers racing with direct awaits.

### Critical: `TaskResult` Is a Borrowed View Masquerading as a Result

`Task::Get()` returns `TaskResult<T, E>`, but `TaskResult` stores only a pointer
to completion storage inside the coroutine frame or invalid-completion storage
inside the `Task` object. Keeping a result after the task is destroyed or moved
can dangle. The type also exposes mutable access through `const_cast`.

Affected areas:

- `include/NGIN/Async/Completion.hpp`
- `TaskResult<T, E>`
- `TaskResult<void, E>`
- `Task::Get`, `GetCompletion`, `AsCompletion`

Required direction:

- Make `TaskResult` own its `Completion`, or rename the current type to a
  clearly borrowed `TaskResultView`.
- Remove mutable access from const result views.
- Prefer value-returning `GetResult()`/`TryGetResult()` APIs at root
  boundaries.
- Add tests proving result values remain valid after the task object moves or
  is destroyed if the API promises ownership.

### Critical: `noexcept` Is Over-Promised for User Types

Several coroutine promise paths are marked `noexcept` while moving or
emplacing arbitrary user `T` and `E` values. If those operations throw, the
program terminates rather than producing a fault or propagating an exception in
a documented way.

Affected areas:

- `PromiseStorage::SetCompletion`
- `promise_type::return_value`
- domain-error and mapping paths that copy or move user errors

Required direction:

- Either constrain `Task<T, E>` to nothrow-movable `T` and `E`, or remove and
  condition `noexcept` where user code can throw.
- If exceptions are enabled, convert unexpected user move/copy failures to
  async faults with captured exceptions.
- Add tests with throwing move/copy value and error types.

### High: Cancellation Callbacks Store Raw Coroutine State

Cancellation registrations store raw promise or awaiter pointers. That is only
safe if the registration lifetime is strictly nested within the coroutine
frame/awaiter lifetime and cannot race with frame destruction. The current
scheduled-task and detached-watcher lifetime issues make that invariant hard to
trust.

Affected areas:

- `TaskContext::YieldAwaiter`
- `TaskContext::DelayAwaiter`
- `Task::CancellablePropagationAwaiter`
- `AsyncGenerator::AdvanceAwaiter`
- `CancellationRegistration`

Required direction:

- Revisit cancellation after the task ownership model is redesigned.
- Keep cancellation registrations owned by the same stable operation state as
  the coroutine frame, or unregister before any referenced frame/awaiter can be
  destroyed.
- Add stress tests for cancellation racing task destruction, delay timer
  dispatch, and executor drain.

### Medium: Blocking `Get()` and `Wait()` Are Easy to Deadlock

`Get()` waits for completion by blocking on a condition. It does not drive a
cooperative or manual executor. A caller can schedule a task and then call
`Get()` before the scheduler makes progress, which can hang unless something
else drains the scheduler.

Affected areas:

- `Task::Wait`
- `Task::Get`
- README and Async guide examples

Required direction:

- Separate "inspect completed result" from "block this thread until done".
- Consider `TryGetResult()`, `ResultView()`, and a deliberately named
  `SyncWait(ctx, task)` that documents executor-driving behavior.
- Ensure examples show the required scheduler progress explicitly.

### Medium: `AsyncGenerator` Needs Release-Mode Misuse Handling

`AsyncGenerator::Next()` only asserts against concurrent consumers. In release
builds, concurrent consumers can overwrite `promise.consumer`. Generator fault
state is also written from `unhandled_exception()` without the same locking
used by other state transitions.

Affected areas:

- `include/NGIN/Async/AsyncGenerator.hpp`

Required direction:

- Return an async fault for concurrent `Next()` calls.
- Lock consistently for producer fault/domain/canceled/completed state.
- Add tests for concurrent `Next()`, cancellation during a suspended `Next()`,
  and producer exception racing consumer wakeup.

### Medium: `AsyncFault::message` Is a Non-Owning String View

`AsyncFault` stores `std::string_view message`. Faults can be stored in task
completions beyond the lifetime of the source string.

Affected areas:

- `include/NGIN/Async/AsyncError.hpp`

Required direction:

- Use an owning string, a fixed static-message table, or remove arbitrary
  message storage from `AsyncFault`.
- Document which fault fields are stable to store and report.

### Low: Documentation and Naming Need Cleanup

The public docs still contain signs of an in-progress design: missing design
note links, multiple root-starting verbs in examples, compatibility aliases,
and stale version labels.

Affected areas:

- `docs/README.md`
- `docs/Async.md`
- `README.md`
- `include/NGIN/Async/Task.hpp`
- `include/NGIN/Async/Completion.hpp`

Required direction:

- Pick one root-starting verb, preferably the one that matches the final
  ownership model.
- Remove stale version labels and compatibility language once the new contract
  lands.
- Add a dedicated async component README under `include/NGIN/Async/README.md`
  if the public API remains broad.

## Proposed Workstreams

### 1. Define the Core Task Contract

Decide and document:

- Is `Task<T, E>` a unique operation handle, a shared operation, or a future-like
  value view?
- What happens when a task object is destroyed before completion?
- Can a task be awaited more than once?
- Can root tasks be detached?
- Are `TaskResult` and `Completion` owning result values or borrowed views?

Deliverables:

- Updated `docs/Async.md` contract section.
- Small design note in this plan or a replacement task-contract plan.
- Tests for each lifetime and duplicate-await rule.

### 2. Rebuild Task Storage Around That Contract

After the contract is decided, update `Task<T, E>` and `Task<void, E>` together.

Likely implementation direction:

- Move runtime state out of the visible `Task` object and into stable operation
  state associated with the coroutine frame.
- Make queued executor work hold whatever ownership token is needed to keep the
  frame alive until the work item runs or is canceled.
- Make invalid usage deterministic and test-covered.
- Remove over-broad `noexcept`.

Deliverables:

- Revised `Task.hpp`.
- Focused `Task.cpp` tests for scheduled lifetime, moves, destruction,
  duplicate awaits, result ownership, and throwing user types.

### 3. Rebuild Composition as Structured Concurrency

Redesign `WhenAll`, `WhenAny`, and `ContinueWith` after task lifetime is safe.

Likely implementation direction:

- Prefer taking child tasks by value when the combinator needs ownership.
- Provide reference-based overloads only if they can prove lifetime safety.
- Make `WhenAny` semantics explicit for losing children: cancel-and-drain,
  leave-running-with-scope, or return handles for follow-up observation.
- Avoid detached watcher coroutines unless their lifetime is owned by a scope.

Deliverables:

- Revised `WhenAll.hpp`, `WhenAny.hpp`, and continuation helpers.
- Tests for local child lifetime, early return, loser cancellation/drain,
  domain error, cancellation, and fault behavior.

### 4. Harden Cancellation and Timers

Once task/combinator ownership is stable, audit cancellation registrations.

Deliverables:

- Cancellation callback lifetime tests.
- Race-oriented tests for cancel-before-dispatch, cancel-during-delay,
  cancel-after-task-destruction, and timer callback after cancellation.
- Documentation for cooperative cancellation rules.

### 5. Harden Generators

Bring synchronous and async generator behavior up to the same standard.

Deliverables:

- Release-mode misuse faults for `AsyncGenerator`.
- Locking consistency for async generator terminal states.
- Docs showing generator consumption and non-concurrent-consumer rule.

### 6. Public Documentation Pass

After the behavior settles, update user-facing docs.

Deliverables:

- `docs/Async.md` examples matching the final API.
- `README.md` smallest example using the final root execution pattern.
- `docs/README.md` links only to existing design notes.
- Optional `include/NGIN/Async/README.md` with API map and invariants.

## Verification Strategy

Use focused verification first:

- Build and run `NGINBaseTests` or the closest focused async test target.
- Run ASan/UBSan for async tests after lifetime changes.
- Add thread-sanitizer or stress-style cancellation tests if the scheduler and
  platform support it.

Do not rely only on green happy-path tests. The core acceptance criteria are
that misuse either cannot compile, produces a deterministic async fault, or is
explicitly documented as undefined only for narrow low-level escape hatches.

## Acceptance Criteria

- Scheduling a task cannot leave an executor with a dangling coroutine handle.
- Destroying, moving, or detaching a task before completion follows one
  documented behavior.
- Duplicate awaiting follows one documented behavior across every await path.
- `WhenAny` and `WhenAll` cannot leave helper coroutines referencing destroyed
  caller-owned tasks.
- Task result ownership is explicit and safe for the documented use cases.
- User `T` and `E` throwing behavior matches the declared exception contract.
- Cancellation registrations cannot outlive the state they reference.
- Public examples show the safe root execution pattern.
- Async docs no longer link missing files or mention stale compatibility
  direction.
