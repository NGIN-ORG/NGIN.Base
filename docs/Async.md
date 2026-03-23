# Async

`NGIN::Async` is the coroutine layer used across `NGIN.Base`.

Use it when you want:

- coroutine-based async code
- typed domain errors instead of exception-driven async control flow
- explicit cancellation
- task composition without hidden runtime threads

Most users only need three types to get started:

- `Task<T, E>` for an async operation that returns `T` or a domain error `E`
- `TaskContext` for the executor and cancellation token
- `TaskOutcome<T, E>` to inspect how a root task finished

## When To Use It

Use `NGIN::Async` when:

- your operation can suspend
- failures are part of the domain and should be typed
- you want cancellation to be explicit
- you are already using an executor and coroutine-based flow

## When Not To Use It

You probably do not need it when:

- the work is purely synchronous and has no suspension points
- plain return values and `Expected<T, E>` are enough
- you only need a single blocking call and no async composition

## Stability

- Stable:
  - `Task<T, E>`
  - `TaskContext`
  - root-task observation with `TaskOutcome`
  - `WhenAll` / `WhenAny`
- Usable and maturing:
  - higher-level patterns built on top of the task model

## Which API Should I Use?

- Need an async coroutine with typed domain errors:
  - use `Task<T, E>`
- Need executor and cancellation access inside a coroutine:
  - use `TaskContext`
- Need to inspect success, domain error, cancellation, or fault at the root:
  - use `TaskOutcome<T, E>`
- Need to wait for many child tasks:
  - use `WhenAll` or `WhenAny`
- Need a multi-yield async sequence:
  - use `AsyncGenerator<T, E>`

## Smallest Useful Example

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

enum class DemoError
{
    InvalidInput,
};

NGIN::Async::Task<int, DemoError> Compute(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.YieldNow();
    co_return 7;
}

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext ctx(scheduler);

    auto task = Compute(ctx);
    task.Start(ctx);
    scheduler.RunUntilIdle();

    auto outcome = task.Get();
    if (!outcome.Succeeded())
    {
        return 1;
    }

    return outcome.Value();
}
```

What to notice:

- `Compute()` creates a cold task
- `Start(ctx)` schedules the root task
- inside the coroutine, write ordinary success-path code
- at the edge of the program, call `Get()` and inspect the outcome

## Normal Style

This is the recommended style for most code:

- inside coroutines:
  - `co_await` child tasks directly
- at the root of the program or in tests:
  - inspect `TaskOutcome<T, E>` from `Get()`
- through a composed chain:
  - keep the same domain error type `E`
- for cancellation:
  - handle it separately from domain errors

In normal code, do not use `Get()` as ordinary control flow inside other coroutines.

## Common Workflows

### Propagate a child task

```cpp
NGIN::Async::Task<int, DemoError> Child(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.YieldNow();
    co_return 3;
}

NGIN::Async::Task<int, DemoError> Parent(NGIN::Async::TaskContext& ctx)
{
    auto child = Child(ctx);
    co_return co_await child;
}
```

This is the normal composition model:

- if `child` succeeds, the value is yielded into `Parent`
- if `child` fails with a domain error, is canceled, or faults, that non-success propagates automatically

### Return a domain error

```cpp
NGIN::Async::Task<int, DemoError> ParseCount(NGIN::Async::TaskContext&)
{
    co_return NGIN::Utilities::Unexpected<DemoError>(DemoError::InvalidInput);
}
```

Use the task error type `E` for ordinary operation-domain failures.

### Handle cancellation in long-running work

```cpp
NGIN::Async::Task<void, DemoError> Work(NGIN::Async::TaskContext& ctx)
{
    for (;;)
    {
        if (ctx.CheckCancellation())
        {
            co_return NGIN::Async::Sentinels::Canceled;
        }

        co_await ctx.YieldNow();
    }
}
```

Use `CheckCancellation()` in long-running loops. Cancellation-aware await points such as `YieldNow()` and `Delay(...)`
also observe cancellation automatically.

### Wait for many tasks

Use `WhenAll` when every child must complete successfully:

```cpp
auto a = Child(ctx);
auto b = Child(ctx);
auto both = co_await NGIN::Async::WhenAll(ctx, a, b);
```

Use `WhenAny` when you need the first finisher:

```cpp
auto a = Child(ctx);
auto b = Child(ctx);
auto index = co_await NGIN::Async::WhenAny(ctx, a, b);
```

`WhenAny` returns the index of the first completed child. The child may have succeeded, failed with a domain error,
been canceled, or faulted.

## Error Handling

`Task<T, E>` separates three different kinds of non-success:

- domain error:
  - the operation failed in an expected, typed way
  - example: parse failure, IO failure, network-domain failure
- canceled:
  - the task stopped cooperatively because cancellation was requested
- fault:
  - async runtime or execution-layer failure
  - example: invalid task state or unhandled exception converted to a fault

At call sites:

- handle domain errors as part of your ordinary program logic
- handle cancellation separately
- treat faults as infrastructure/runtime problems, not ordinary business errors

Typical root handling looks like this:

```cpp
auto outcome = task.Get();
if (outcome.Succeeded())
{
    Use(outcome.Value());
}
else if (outcome.IsDomainError())
{
    HandleDomainError(outcome.DomainError());
}
else if (outcome.IsCanceled())
{
    HandleCancellation();
}
else
{
    HandleFault(outcome.Fault());
}
```

## Core Concepts

### `Task<T, E>`

The main async type.

- cold by default
- move-only
- started explicitly at the root
- normally consumed through `co_await`

### `TaskContext`

The coroutine context.

It carries:

- an executor
- a cancellation token

It also provides:

- `YieldNow()`
- `Delay(...)`
- `CheckCancellation()`

### `TaskOutcome<T, E>`

The terminal result of a completed task, returned by `Get()`.

Use it at:

- root-task boundaries
- tests
- bridge code between async and non-async parts of a program

### `AsyncGenerator<T, E>`

Use it when you need an async sequence instead of a single async result.

Advance it with:

- `Next(ctx) -> Task<GeneratorNext<T>, E>`

## Common Mistakes

- Forgetting to `Start(ctx)` or `Schedule(ctx)` on a root task.
- Using `Get()` inside normal coroutine control flow instead of `co_await`.
- Mixing incompatible error types across composed tasks.
- Treating cancellation like a domain error.
- Using faults for normal operation failures that should be represented by `E`.

## Reference Notes

- The cancellation sentinel is `NGIN::Async::Sentinels::Canceled`.
- `TaskOutcome<T, E>` is mainly for root boundaries and tests.
- Exact `E` matching matters for ordinary automatic propagation in the current model.

For design notes and follow-up work, read [AsyncPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/AsyncPlan.md).
