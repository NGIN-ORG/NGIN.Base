# Async Simplification Plan

Purpose: define a clean breaking-release direction for `NGIN::Async` after the
hardening pass. This plan intentionally allows breaking changes and does not
preserve compatibility bridges. The goal is a smaller async core that is easier
to maintain, faster on common paths, more reliable under cancellation and
composition, and clearer for third-party developers.

## Position

The current async stack is safer than before, but still carries too many
ownership and consumption modes in the same public types:

- `Task<T, E>` is both a cold operation object and a scheduled operation handle.
- scheduled task destruction implicitly detaches work.
- `Get()`, `Wait()`, `AsCompletion()`, `AsThrowing()`, `ContinueWith`, and
  mapping helpers all live on the core task type.
- `WhenAll` and `WhenAny` use watcher coroutines and shared side state to
  compensate for caller-owned child task lifetime.
- result observation currently uses shared completion storage to make borrowed
  result views safe.

Those pieces can be made correct, but the implementation cost is high. For a
foundation library, the public contract should be simpler and should make the
safe path the natural path.

## Non-Negotiables

- No compatibility aliases, migration shims, legacy overloads, or silent
  fallback behavior.
- Remove old APIs in the same change that introduces their replacements.
- Prefer compile errors over runtime compatibility behavior.
- Keep cancellation, domain errors, and runtime faults distinct.
- Keep the core single-consumer unless a separate shared abstraction is added.
- Do not add a global runtime or hidden worker threads.

## Target Model

### `Task<T, E>` Is A Cold Unique Operation

`Task<T, E>` should mean exactly one thing:

- a coroutine operation that has not been started yet
- move-only
- single-consumer
- no root scheduling side effects in its destructor
- no blocking wait behavior

Destroying an unstarted task destroys the coroutine frame. Destroying a moved
or empty task is a no-op.

### Running Work Uses A Separate Handle

Scheduling should consume a task and return a running operation handle:

```cpp
auto op = NGIN::Async::Spawn(ctx, Compute(ctx));
auto result = co_await op;
```

or:

```cpp
auto op = ctx.Spawn(Compute(ctx));
```

Open naming decision:

- `Spawn(ctx, task)` is concise and familiar for coroutine work.
- `Schedule(ctx, task)` matches the current vocabulary.
- `Run(ctx, task)` is short but less precise.

Preferred direction: `Spawn(ctx, std::move(task))`.

The returned handle owns observation of the running operation. If a user wants
fire-and-forget behavior, they must request it explicitly.

### Detached Work Is Explicit

Implicit detach should be removed. Use an explicit API:

```cpp
NGIN::Async::Detach(ctx, BackgroundWork(ctx));
```

Detached work contract:

- the coroutine frame is cleaned up when the coroutine reaches final suspend
- cancellation is cooperative
- if a detached coroutine never resumes or never completes, its frame remains
  live by definition
- detached work cannot be observed after detaching

This contract is blunt, but it is honest and easy to document.

### Completion Is A Value

Replace shared `TaskResult` storage with value-owned terminal completion:

```cpp
auto completion = co_await op;
if (completion.Succeeded()) { ... }
```

At root boundaries:

```cpp
auto completion = op.TakeResult();
```

Potential API:

- `Completion<T, E>` remains the terminal state type.
- `TryTakeResult() -> Optional<Completion<T, E>>`
- `TakeResult() -> Completion<T, E>` only valid after completion, or asserts on
  misuse.

Remove `TaskResult<T, E>` unless it is renamed and deliberately reintroduced as
a separate shared/future result type.

### Blocking Is Not On `Task`

Remove `Task::Wait()`.

If a blocking bridge is needed, provide a named helper:

```cpp
auto completion = NGIN::Async::SyncWait(scheduler, Compute(ctx));
```

`SyncWait` must document whether it drives the scheduler, blocks the current
thread, or both. It should not be a member function on the core task type.

### Combinators Own Children

`WhenAll` and `WhenAny` should consume child tasks or running handles. They
should not await caller-owned task references through detached watcher
coroutines.

Preferred shape:

```cpp
auto all = co_await NGIN::Async::WhenAll(ctx, ChildA(ctx), ChildB(ctx));
auto any = co_await NGIN::Async::WhenAny(ctx, ChildA(ctx), ChildB(ctx));
```

Contract:

- `WhenAll` owns every child until all children finish or the combinator is
  canceled.
- `WhenAny` owns every child until the first child finishes, then cancels losing
  children cooperatively and drains or detaches them according to the selected
  policy.
- no helper coroutine may reference a caller-owned task object after the
  combinator returns.

Open policy decision for `WhenAny` losers:

- cancel-and-drain before returning, safest but can delay first-result delivery
- detach losers explicitly into an owned losing-work scope
- return a result object that owns losers until they finish

Preferred direction: cancel-and-drain for `WhenAll`-style structured
concurrency; add a separate `RaceDetached` later if early return with detached
losers is truly needed.

### Functional Helpers Are Optional Layer

Remove `ContinueWith`, `MapValue`, `MapError`, `MapCompletion`, and similar
helpers from the core `Task` type.

Normal coroutine code should be the primary composition model:

```cpp
auto value = co_await child;
co_return Transform(value);
```

If functional composition is still wanted, put it in a separate helper header
after the core contract stabilizes.

### Exception View Is Optional Layer

Keep typed completion as the core model. If `AsThrowing()` remains, move it out
of the core task implementation:

```cpp
auto value = co_await NGIN::Async::Throwing(op);
```

This keeps exception adaptation separate from the task state machine.

## Proposed Public Surface

Core:

- `Task<T, E>`
- `Completion<T, E>`
- `TaskContext`
- `Spawn(ctx, Task<T, E>) -> Operation<T, E>`
- `Detach(ctx, Task<void, E>)`
- `WhenAll(ctx, tasks...)`
- `WhenAny(ctx, tasks...)`
- `SyncWait(...)` as an explicit bridge, not a task member

Likely remove from core:

- `Task::Start`
- `Task::Schedule`
- `Task::TrySchedule`
- `Task::Wait`
- `Task::Get`
- `Task::GetCompletion`
- `Task::AsCompletion`
- `Task::AsThrowing`
- `Task::ContinueWith`
- `Task::MapValue`
- `Task::MapError`
- `Task::MapCompletion`
- `TaskResult`

Possible new type:

- `Operation<T, E>` or `RunningTask<T, E>` for scheduled work

Open naming decision: prefer `Operation<T, E>` if this handle is mostly an
owned operation state, or `RunningTask<T, E>` if the public docs should stay
task-oriented.

## Implementation Direction

### 1. Split Cold Task From Running Operation

Move runtime state out of `Task<T, E>` into an internal operation state owned by
the running handle.

`Task<T, E>` should mainly wrap the coroutine handle before start. Starting
transfers ownership into `Operation<T, E>`.

Expected simplifications:

- `Task` destructor only destroys unstarted frames.
- no `m_started` state on `Task`.
- no implicit detach flag on `Task`.
- no invalid-completion storage on moved or empty tasks.
- fewer branches in `Get`/await paths because those APIs move to
  `Operation`.

### 2. Make Completion Storage Direct

Store `Completion<T, E>` directly in the promise or operation state once
complete. Move it out on terminal observation.

Avoid one heap allocation per completion unless shared observation is
explicitly requested.

### 3. Replace Continuation Special Cases

Keep one continuation path:

- one awaiting consumer
- duplicate await is programmer error, preferably assert in debug and fault or
  terminate according to the selected public misuse policy
- no separate completion handler function pointer for combinators

If propagation needs custom behavior, express it as normal coroutine code or
operation await behavior.

### 4. Rebuild `TaskContext` Around Spawn/Delay/Yield

Keep `TaskContext` as the explicit executor and cancellation binding, but reduce
raw callback coupling.

Targets:

- `TaskContext` owns no operation state.
- `YieldNow()` and `Delay()` resume through the active operation state.
- cancellation registrations do not hold raw awaiter pointers whose lifetime is
  hard to reason about.

### 5. Rebuild `WhenAll` And `WhenAny`

Implement combinators on top of owned child operations, not borrowed task
references.

Expected simplifications:

- remove watcher coroutine frame vectors
- remove child handle vectors and child detach function pointers
- remove weak/shared state used only to retire watchers
- make cancellation behavior explicit in the combinator state machine

### 6. Move Optional APIs Out

After the core model is smaller:

- reintroduce throwing adapters in a separate header if needed
- reintroduce functional helpers in a separate header if needed
- consider a separate `SharedOperation<T, E>` only if real multi-await usage
  appears

## Migration Policy

Because this is a clean breaking release:

- delete old APIs directly
- update all internal users in the same change
- update docs and examples to only show the new model
- do not leave deprecated aliases
- do not add overloads that accept both old and new ownership shapes
- do not preserve old root scheduling behavior

Internal users to update:

- async filesystem APIs and examples
- network async APIs and examples
- message stream filters
- async dispatch helpers
- tests under `tests/Async`
- docs under `docs/Async.md`, `docs/IO.md`, and `docs/Network.md`

## Workstreams

### Workstream 1: Finalize Contract And Names

Decide:

- `Spawn` vs `Schedule` naming
- `Operation<T, E>` vs `RunningTask<T, E>`
- `WhenAny` loser policy
- misuse policy for duplicate await and invalid result taking
- whether `Task<void, E>` is required for `Detach`, or whether value-returning
  detached work is allowed and discards the value

Deliverables:

- updated `docs/Async.md` contract section
- short API map in this plan or an `include/NGIN/Async/README.md`

### Workstream 2: Introduce `Operation<T, E>`

Add the new running handle and transfer ownership from cold task to operation.

Deliverables:

- `Task<T, E>` reduced to cold operation wrapper
- `Spawn(ctx, task)` implemented
- `Detach(ctx, task)` implemented
- focused tests for start, move, destruction, completion, cancellation, and
  duplicate await misuse

### Workstream 3: Remove Old Task Members

Delete root scheduling and observation members from `Task`.

Deliverables:

- no `Task::Schedule`, `Start`, `TrySchedule`, `Get`, `Wait`,
  `AsCompletion`, `AsThrowing`, or `ContinueWith`
- all internal call sites updated
- no compatibility aliases

### Workstream 4: Rebuild Combinators

Rewrite `WhenAll` and `WhenAny` around owned child operations.

Deliverables:

- simpler combinator state machines
- no watcher coroutine vectors
- tests for success, domain error, fault, cancellation, early `WhenAny`, loser
  handling, and local temporaries
- ASan run over focused async tests

### Workstream 5: Update IO And Network Users

Update async filesystem and network surfaces to the new contract.

Deliverables:

- async IO examples compile with `Spawn`/`Operation`
- network docs no longer mention removed task APIs
- message stream filters use the new task/operation shape

### Workstream 6: Documentation And Examples

Rewrite docs around the clean model.

Deliverables:

- `docs/Async.md`
- `docs/IO.md` async sections
- `docs/Network.md` async sections
- README examples
- optional `include/NGIN/Async/README.md`

## Verification Plan

Targeted verification:

```bash
cmake --build build/ngin-base-ci --target Async_TaskTests Async_WhenTests Async_AsyncGeneratorTests Async_ContinueWithTests Async_CancellationTests Async_TaskContextTests
./build/ngin-base-ci/tests/Async_TaskTests
./build/ngin-base-ci/tests/Async_WhenTests
./build/ngin-base-ci/tests/Async_AsyncGeneratorTests
./build/ngin-base-ci/tests/Async_CancellationTests
./build/ngin-base-ci/tests/Async_TaskContextTests
```

After combinator and lifetime rewrites, also run ASan equivalents.

Broader verification:

- async filesystem tests when IO call sites change
- network/message stream tests when network call sites change
- `NGIN.Base.Static` build after public header changes

## Success Criteria

- `Task<T, E>` has one meaning: cold unique operation.
- scheduling consumes a task and returns a running handle.
- detach is explicit and documented.
- blocking is explicit and outside the task type.
- terminal completion is value-owned.
- combinators own children and do not reference caller-owned task objects after
  return.
- duplicate await and invalid result observation have one documented misuse
  policy.
- no compatibility bridges remain.
- async docs teach one normal path rather than several equivalent paths.

## Risks

- Updating IO and network users may reveal assumptions about root scheduling and
  result observation.
- `WhenAny` loser policy can affect latency and resource lifetime; decide this
  before implementation.
- Removing functional helpers may temporarily reduce convenience, but it should
  make the core state machine much smaller.
- Direct completion storage improves allocation behavior but requires careful
  move/take-result semantics.

## Recommended First Cut

Do the breaking cleanup in this order:

1. Define `Operation<T, E>` and `Spawn`.
2. Convert core task tests to the new model.
3. Delete old `Task` scheduling/result APIs.
4. Convert `WhenAll`/`WhenAny` to consume tasks.
5. Convert IO/network users.
6. Remove optional composition helpers from core.
7. Rewrite docs and examples.

This order avoids carrying two public models at once and keeps compile errors
as the migration guide.
