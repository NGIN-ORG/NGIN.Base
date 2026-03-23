# AsyncPlan

Status: implemented redesign with continuing polish and follow-up work. This document now serves as the contract and
follow-up plan for the typed async model in `NGIN.Base`.

Purpose: document the `NGIN::Async` redesign so async code has a typed domain-error model, explicit
cancellation/runtime semantics, and clean coroutine flow without depending on exceptions or nested result types.

This document records the replacement of the old `Task<T>` + `AsyncExpected<T>` design as the primary async model.

## Summary

The target design is:

- public coroutine type: `Task<T, E>`
- public terminal observation type: `TaskOutcome<T, E>`
- non-success states remain distinct:
  - `DomainError(E)`
  - `Canceled`
  - `Fault(AsyncFault)`

Core semantic law:

> Awaiting `Task<T, E>` either produces `T`, or immediately completes the awaiting coroutine with the same non-success
> outcome.

That is the central rule of the redesign. It removes double-unwrapping, removes routine manual bridging between async
and domain failures, and keeps exceptions optional rather than fundamental.

## Goals

- make normal coroutine code read like direct success-path code
- expose domain errors explicitly through `E`
- keep cancellation distinct from domain errors
- keep runtime/execution failures distinct from domain errors
- keep exception-free builds first-class
- support exception-enabled builds without changing public async semantics
- document propagation and lifecycle rules tightly enough that behavior is not left to implementation accident

## Non-Goals

- preserving `Task<T>` + `AsyncExpected<T>` as a first-class long-term public model
- using exceptions as the main async control-flow mechanism
- encoding cancellation as a domain error
- encoding runtime/execution failures as a domain error
- preserving old manual unwrapping ergonomics through long-lived compatibility helpers or new examples
- introducing macro-based propagation as the main way to write async code

## Current Problems

The current async stack effectively exposes two layers at ordinary await sites:

- async/runtime completion through `AsyncExpected<T>`
- domain result transport inside task values such as `Task<Result<T>>`

That leads to:

- double unwrapping
- duplicated branching at every await site
- boilerplate bridging such as `ReturnError(fileExpected.Error())`
- poor separation between domain failure and async/runtime failure
- examples that expose implementation plumbing instead of intended usage

The redesign fixes this by moving typed domain errors into `Task<T, E>` directly while keeping cancellation and fault as
separate async states.

## Core Public Model

## `Task<T, E>`

`Task<T, E>` is the main coroutine type.

Properties:

- cold by default
- move-only
- single-completion
- single-start
- single-continuation in v1

`Task<void, E>` is the `void` specialization with the same non-success semantics.

## `TaskOutcome<T, E>`

`TaskOutcome<T, E>` is the terminal observation API for completed tasks.

It is deliberately not the normal in-coroutine programming model.

Philosophy:

- inside coroutines:
  - `co_await Task<T, E>` yields `T` or propagates non-success automatically
- at root and observation boundaries:
  - `Get()`
  - tests
  - scheduler or executor boundaries
  - bridge/adaptation code
  - debugging and diagnostics
  use `TaskOutcome<T, E>`

Required states:

- `Succeeded`
- `DomainError`
- `Canceled`
- `Fault`

Required API for non-void:

- `Status()`
- `Succeeded()`
- `HasValue()`
- `Value()`
- `IsDomainError()`
- `DomainError()`
- `IsCanceled()`
- `IsFault()`
- `Fault()`

Required API for `TaskOutcome<void, E>`:

- `Status()`
- `Succeeded()`
- `IsDomainError()`
- `DomainError()`
- `IsCanceled()`
- `IsFault()`
- `Fault()`

`TaskOutcome<void, E>` does not expose `Value()`.

## Semantic Laws

## Await Propagation Law

This redesign must obey one crisp propagation rule:

> Awaiting `Task<T, E>` either produces `T` into the awaiting coroutine, or immediately completes the awaiting
> coroutine with the same non-success outcome.

That means:

- success:
  - `co_await child` yields `T`
- domain error:
  - the awaiting coroutine completes with the same `E`
- canceled:
  - the awaiting coroutine completes canceled
- fault:
  - the awaiting coroutine completes with the same `AsyncFault`

No implicit wrapping, translation, or partial handling happens by default.

## Exact Domain Error Matching in v1

Automatic propagation requires the same domain error type exactly.

Rules:

- `Task<U, E>` may directly `co_await Task<T, E>`
- `Task<U, E1>` does not directly auto-propagate `Task<T, E2>` when `E1 != E2`
- cross-type composition requires an explicit adapter

This same rule applies to:

- ordinary `co_await`
- combinators
- continuation helpers

This keeps v1 predictable and avoids surprising implicit conversions.

## Lifecycle and Observation Rules

Tasks remain cold by default.

Rules for v1:

- constructing a task does not start it
- root tasks are started explicitly
- a task may be started at most once
- a task may have at most one awaiting continuation at a time
- `Get()` is a terminal observation API
- `Get()` waits for completion if the task is not yet complete
- `Get()` does not throw
- repeated `Get()` on a completed task is supported

Chosen v1 observation rule:

- `Get()` returns `TaskOutcome<T, E>`
- repeated observation is by const access to stored outcome state
- move-only success values must remain observable through reference-based outcome access

Invalid lifecycle usage such as starting a task twice or observing an invalid task returns a `Fault` outcome with an
appropriate runtime fault code.

## Async-Specific Non-Success States

## Domain Error

`E` is reserved for operation-domain failures such as:

- `IOError`
- parse errors
- network or transport domain errors
- protocol/domain validation failures

Domain errors are not used for:

- cancellation
- runtime corruption or invalid async state
- scheduler or executor failures

## Cancellation

Cancellation is a first-class async completion state.

It is not:

- a domain error
- a fault
- an exception-driven control-flow mechanism

Cancellation should arise from:

- explicit cancellation tokens
- cancellation-aware await points
- explicit coroutine decisions such as `if (ctx.CheckCancellation()) co_return Sentinels::Canceled;`

## Fault

`AsyncFault` is reserved for async runtime or execution failures.

It covers categories such as:

- invalid task/coroutine state
- scheduler or executor dispatch failure
- broken async runtime contract
- continuation dispatch failure
- unhandled exception converted to fault when exceptions are enabled

It does not cover ordinary operation-domain failures such as:

- file not found
- permission denied
- parse failure
- socket connect timeout or read timeout when those are part of the network/domain contract

Operation timeouts belong in the domain error type for that subsystem unless the timeout is specifically an async
runtime or scheduler failure.

## `AsyncFault`

`AsyncFault` must remain:

- small
- movable
- cheap in no-exception builds
- stable across exception-enabled and exception-free builds

Required payload:

- `AsyncFaultCode code`
- optional native/platform code when relevant

Optional payload:

- lightweight diagnostic identifier
- debug-oriented message handle
- captured exception diagnostics when enabled

Heavier diagnostics must be stored out-of-line or behind optional shared state so they do not bloat every task.

## Exception Policy

Exceptions remain optional and non-fundamental.

Rules:

- async public APIs do not throw as part of normal control flow
- exception-free builds are first-class
- when exceptions are enabled:
  - unhandled exceptions inside tasks are converted to `Fault`
  - exception data may be retained for diagnostics
  - exceptions do not escape through `co_await`, `Get()`, or terminal task observation APIs

This keeps async semantics stable regardless of exception mode.

## Task and Promise Semantics

Each task stores one completion state, not loosely coupled value/error flags.

The promise holds exactly one of:

- success value
- domain error `E`
- canceled state
- fault state

Primary terminal coroutine forms:

- `co_return value`
- `co_return NGIN::Utilities::Unexpected<E>{...}`
- `co_return Sentinels::Canceled`
- `co_return Fault(...)`

Helpers such as `ReturnError`, `ReturnCanceled`, and `ReturnFault` may exist as optional convenience helpers, but they
are not the primary semantic path and should not dominate examples or migration targets.

## `TaskContext` and Awaitables

`TaskContext` continues to bind:

- executor or scheduler
- cancellation token

The redesign changes how awaitables surface non-success.

## `CheckCancellation()`

`CheckCancellation()` is a cheap non-throwing status query only.

It does not directly complete the current task.

Intended use:

- long-running CPU loops poll `CheckCancellation()`
- if cancellation has been requested, the coroutine explicitly `co_return Sentinels::Canceled`

## `YieldNow()` and `Delay(...)`

These are cancellation-aware await points.

Behavior:

- success resumes normally
- cancellation completes the awaiting task as `Canceled` through the propagation law
- runtime/executor failure completes the awaiting task as `Fault`

This removes the current pattern where callers manually bridge `AsyncExpected<void>` into task-level error handling.

## Combinators

## `WhenAll`

Public v1 shape:

- `Task<std::tuple<T...>, E>`
- `Task<void, E>` for the all-void form

Rules:

- all children must share the same `E`
- if all children succeed, return the tuple or success
- if any child completes non-success, `WhenAll` completes with the earliest observed non-success child completion
- ties within the same observation point are broken by child index order

This is the chosen deterministic policy for v1.
No precedence scheme such as `Fault > Canceled > DomainError` is used.

## `WhenAny`

Public v1 shape:

- `Task<UIntSize, E>`

Rule:

> `WhenAny` reports which child completed first, regardless of whether that child succeeded, failed with domain error,
> canceled, or faulted. The caller then inspects that child separately.

Behavior:

- if a winner is observed, return its index
- if the awaiting context is canceled before a winner exists, complete canceled
- if the combinator itself encounters runtime failure, complete fault

`WhenAny` does not reinterpret the winning child’s completion as its own domain error.

## Continuations

Continuation-style helpers must align with the same propagation model.

Requirements:

- v1 uses same-`E` composition only
- success continuations receive values directly
- non-success propagates automatically unless an explicit observation or adaptation API is used

## Async Generator Model

Generators need an explicit chosen shape rather than being loosely adapted from the task model.

Chosen v1 shape:

- `AsyncGenerator<T, E>`
- `Next(ctx) -> Task<GeneratorNext<T>, E>`

`GeneratorNext<T>` distinguishes:

- `Item(T)`
- `End`

Rules:

- end-of-sequence is not encoded as a domain error
- end-of-sequence is not encoded as cancellation or fault
- cancellation and fault remain task-level non-success states on `Next(ctx)`

This keeps end-of-sequence distinct from failure.

## Async IO Redesign

The redesign removes `Task<Result<T>>` as the normal async IO shape.

Target shapes:

- `OpenFileAsync(...) -> Task<AsyncFileHandle, IOError>`
- `GetInfoAsync(...) -> Task<FileInfo, IOError>`
- `ReadAllBytesAsync(...) -> Task<Vector<Byte>, IOError>`
- `WriteAllBytesAsync(...) -> Task<void, IOError>`

Rules:

- domain IO failures use `IOError`
- cancellation yields `Canceled`
- runtime/executor failures yield `Fault`

If async handle value wrappers are available by the time async IO is migrated, use them.
If not, temporary pointer-backed handles are acceptable internally, but nested async/domain-result layering is not.

## Networking and Transport Redesign

Networking and transport APIs must also move to typed domain errors.

Target direction:

- socket and driver APIs use `Task<T, NetError>` or transport-specific `E`
- runtime/executor failures remain `Fault`
- cancellation remains `Canceled`

Ordinary network and transport failures must not continue to surface as generic async runtime faults in the public API.

## Migration Strategy

Ordered rollout:

1. introduce `AsyncFault`, `TaskOutcome<T, E>`, `Task<T, E>`, and `Task<void, E>`
2. refactor promise storage, `co_await` propagation, and root-task observation
3. refactor `TaskContext`, `YieldNow()`, `Delay(...)`, and cancellation observation
4. migrate `WhenAll`, `WhenAny`, and continuation utilities
5. migrate async IO APIs and helpers
6. migrate networking and transports
7. migrate generators
8. update tests, docs, and examples
9. remove the old `Task<T>` + `AsyncExpected<T>` first-class path

Anti-goals:

- do not preserve old-style manual unwrapping in new examples
- do not keep long-lived compatibility helpers that reintroduce nested result ergonomics
- do not leave fault/domain boundaries vague for convenience

Compatibility stance:

- this is a breaking redesign
- only minimal transitional adapters should exist, and only to reduce migration risk
- no long-term dual async model

## Test Plan

## Core Task Behavior

- success completes with `Succeeded`
- domain error completes with `DomainError(E)`
- cancellation completes with `Canceled`
- runtime failure completes with `Fault`
- the await propagation law holds for all four cases
- invalid lifecycle use yields `Fault(InvalidState)` or an equivalent precise runtime fault
- single-start and single-continuation rules are enforced

## Exception Mode Coverage

- no-exception builds compile and pass async tests
- exception-enabled builds convert unhandled exceptions to `Fault`
- optional exception diagnostics are present only when enabled
- async boundaries do not expose thrown exceptions as public control flow

## Awaitables and Context

- `YieldNow()` success/cancel/fault behavior
- `Delay(...)` success/cancel/fault behavior
- `CheckCancellation()` remains a cheap explicit status query

## Combinators

- `WhenAll` all-success
- `WhenAll` earliest observed domain error
- `WhenAll` earliest observed cancellation
- `WhenAll` earliest observed fault
- `WhenAll` deterministic tie-break by child index
- `WhenAny` returns winning index regardless of winner status
- `WhenAny` canceled before winner
- continuation helpers preserve same-`E` propagation behavior

## Async IO and Network Migration

- no user-facing async APIs remain on `Task<Result<T>>`
- representative async IO flows succeed and fail through `IOError`
- representative network and transport flows succeed and fail through their domain error types
- no user-facing examples require double unwrapping

## Generator Coverage

- `AsyncGenerator<T, E>` item production
- `End` signaling distinct from failure
- cancellation propagation through `Next(ctx)`
- fault propagation through `Next(ctx)`

## Acceptance Criteria

The redesign is ready for broad adoption when all of the following are true:

- ordinary coroutine code can use `co_await Task<T, E>` without double-unwrapping
- domain errors, cancellation, and fault are distinct and documented
- `TaskOutcome<T, E>` is used mainly at observation boundaries, not as the routine in-coroutine style
- exact-`E` propagation rules are enforced in v1
- `CheckCancellation()` is a status query only
- `WhenAll` and `WhenAny` have deterministic documented semantics
- async IO and network APIs stop exposing nested async/domain result layers
- exception-free and exception-enabled builds both preserve the same public async control-flow model
