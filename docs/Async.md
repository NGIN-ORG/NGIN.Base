# Async

`NGIN::Async` is the coroutine and cancellation layer in `NGIN.Base`.

The current public model is:

- `Task<T, E>` for async work that produces `T` or a typed domain error `E`
- `Task<void, E>` for async work with no success payload
- `TaskOutcome<T, E>` for terminal observation at task boundaries
- distinct non-success states:
  - domain error `E`
  - canceled
  - fault (`AsyncFault`)

This is an explicit, exception-optional async model. Coroutine code should normally read like straight-line success-path code, while root callers inspect `TaskOutcome<T, E>`.

## Mental Model

- A `Task<T, E>` is cold by default.
- A `TaskContext` supplies the executor and cancellation token.
- `co_await childTask` inside another compatible `Task<U, E>` either:
  - yields `T`, or
  - completes the awaiting coroutine with the same non-success outcome.
- `Get()` is for terminal observation, not the normal in-coroutine style.

In v1, automatic propagation requires the exact same domain error type `E`.

## Key Types

### `Task<T, E>`

Use `Task<T, E>` for ordinary async coroutines:

```cpp
NGIN::Async::Task<int, MyError> Compute(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.YieldNow();
    co_return 123;
}
```

Success is returned with `co_return value`.

Non-success can be produced with:

- `co_return NGIN::Utilities::Unexpected<E>(error)`
- `co_return NGIN::Async::Canceled`
- `co_return NGIN::Async::Fault(asyncFault)`

Helper awaiters such as `Task<T, E>::ReturnError(...)` still exist, but they are convenience helpers, not the preferred public style.

### `TaskOutcome<T, E>`

`TaskOutcome<T, E>` is the observation API returned by `Get()`.

Use it at:

- root-task boundaries
- tests
- schedulers/executors
- bridge code

Main queries:

- `Succeeded()`
- `HasValue()` / `Value()` for non-void
- `IsDomainError()` / `DomainError()`
- `IsCanceled()`
- `IsFault()` / `Fault()`

### `TaskContext`

`TaskContext` binds a coroutine to:

- an executor (`ExecutorRef`)
- a cancellation token

It also provides the main cancellation-aware await points:

- `YieldNow()`
- `Delay(...)`

`CheckCancellation()` is a cheap status query only.

## Getting Started

Typical root-task flow:

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

namespace
{
    enum class DemoError
    {
        InvalidInput,
    };

    NGIN::Async::Task<int, DemoError> Compute(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
        co_return 7;
    }
}

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext ctx(scheduler);

    auto task = Compute(ctx);
    task.Start(ctx);
    scheduler.RunUntilIdle();

    auto outcome = task.Get();
    if (!outcome)
    {
        return 1;
    }

    return outcome.Value();
}
```

What happens here:

1. `Compute(ctx)` creates a cold task.
2. `task.Start(ctx)` binds it to the context and schedules it.
3. `scheduler.RunUntilIdle()` drives execution.
4. `task.Get()` returns `TaskOutcome<int, DemoError>`.

## Error and Fault Model

`E` is only for domain errors such as:

- IO errors
- parse errors
- network/transport domain failures
- validation failures

Cancellation and async runtime failures are not encoded as `E`.

### Cancellation

Cancellation is a first-class completion state.

Examples:

```cpp
NGIN::Async::Task<void, MyError> Work(NGIN::Async::TaskContext& ctx)
{
    for (;;)
    {
        if (ctx.CheckCancellation())
        {
            co_return NGIN::Async::Canceled;
        }

        co_await ctx.YieldNow();
    }
}
```

### Faults

`AsyncFault` is reserved for async runtime/execution failures, such as:

- invalid task state
- scheduler/executor failure
- broken runtime contract
- unhandled exception converted to fault when exceptions are enabled

Ordinary operation failures should stay in `E`.

## Combinators

### `WhenAll`

- `WhenAll(ctx, Task<T, E>&...) -> Task<std::tuple<T...>, E>`
- `WhenAll(ctx, Task<void, E>&...) -> Task<void, E>`

Behavior:

- all children must use the same `E`
- if all succeed, the combined task succeeds
- if any child completes non-success, `WhenAll` completes with the earliest observed non-success child outcome
- cancellation of the awaiting context completes the combined task as canceled, even if children do not observe cancellation themselves

### `WhenAny`

- `WhenAny(ctx, Task<...>& first, Task<...>&... rest) -> Task<NGIN::UIntSize, E>`

Behavior:

- returns the index of the first child to complete
- the winning child may have succeeded, failed with a domain error, canceled, or faulted
- the caller inspects the winning child separately
- if the awaiting context is canceled before a winner exists, `WhenAny` completes as canceled

## `ContinueWith`

`ContinueWith(ctx, func)` runs a continuation after the source task completes.

Rules:

- the continuation must return `Task<..., E>` with the same `E`
- parent domain error, cancellation, and fault propagate automatically
- context cancellation can wake the continuation even if the parent task never completes

Example:

```cpp
NGIN::Async::Task<int, MyError> Parent(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.YieldNow();
    co_return 5;
}

NGIN::Async::Task<int, MyError> Child(NGIN::Async::TaskContext& ctx)
{
    auto parent = Parent(ctx);
    co_return co_await parent.ContinueWith(ctx, [&](int value) -> NGIN::Async::Task<int, MyError> {
        co_return value * 2;
    });
}
```

## Async Generators

`AsyncGenerator<T, E>` is the multi-yield async sequence type.

Advance it with:

- `Next(ctx) -> Task<GeneratorNext<T>, E>`

`GeneratorNext<T>` distinguishes:

- `Item(T)`
- `End`

End-of-sequence is not encoded as an error, cancellation, or fault.

Example:

```cpp
NGIN::Async::AsyncGenerator<int, MyError> Produce(NGIN::Async::TaskContext& ctx)
{
    co_yield 1;
    co_await ctx.YieldNow();
    co_yield 2;
}

NGIN::Async::Task<int, MyError> Consume(NGIN::Async::TaskContext& ctx)
{
    auto gen = Produce(ctx);
    int sum = 0;

    for (;;)
    {
        auto next = co_await gen.Next(ctx);
        if (next.IsEnd())
        {
            break;
        }
        sum += next.Value();
    }

    co_return sum;
}
```

## Lifecycle Rules

Current v1 rules:

- tasks are cold by default
- tasks are move-only
- a task may be started at most once
- `Get()` is terminal observation and waits for completion when needed
- repeated `Get()` on a completed task is supported
- invalid lifecycle use becomes a fault outcome instead of throwing

## Exception Policy

Exceptions remain optional.

- exception-free builds are first-class
- async public APIs do not throw as part of normal control flow
- when exceptions are enabled, unhandled exceptions inside a task are converted to `AsyncFault`

## Current Guidance

Use this style by default:

- inside coroutines: `co_await Task<T, E>` directly and write success-path code
- at roots/tests: inspect `TaskOutcome<T, E>` from `Get()`
- use `WhenAll`/`WhenAny` for composition rather than manually wiring multiple continuations

For async filesystem and networking examples, see:

- [`IO.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/IO.md)
- [`Network.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Network.md)
- [`AsyncPlan.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/AsyncPlan.md)
