# Async

`NGIN::Async` is the coroutine layer used across `NGIN.Base`.

Use it when you want:

- coroutine-based async code
- typed domain errors instead of exception-driven async control flow
- explicit cancellation
- task composition without hidden runtime threads

Most users only need these types:

- `Task<T, E>`: a cold coroutine object
- `Operation<T, E>`: a started task owned at a root boundary
- `TaskContext`: executor and cancellation state
- `Completion<T, E>`: the value-owned terminal result
- `WhenAll` / `WhenAny`: consumed-task combinators

## Core Contract

`Task<T, E>` is cold and move-only. Creating a task does not schedule work.

Start root work with:

- `Spawn(ctx, std::move(task)) -> Operation<T, E>`
- `Detach(ctx, std::move(task))` for explicit fire-and-forget
- `SyncWait(ctx, std::move(task)) -> Completion<T, E>` for blocking bridge code

Inside another coroutine, compose child tasks with `co_await`. Child task
failures propagate automatically.

Root result inspection happens through `Operation::TakeResult()`, which returns
a value-owned `Completion<T, E>`. A result can be taken once.

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

    auto operation = NGIN::Async::Spawn(ctx, Compute(ctx));
    scheduler.RunUntilIdle();

    auto result = operation.TakeResult();
    if (!result)
    {
        return 1;
    }

    return result.Value();
}
```

## Normal Style

Inside coroutines:

```cpp
NGIN::Async::Task<int, DemoError> Child(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.YieldNow();
    co_return 3;
}

NGIN::Async::Task<int, DemoError> Parent(NGIN::Async::TaskContext& ctx)
{
    co_return co_await Child(ctx);
}
```

If `Child` succeeds, the value is returned. If it domain-fails, is canceled, or
faults, that non-success propagates to `Parent`.

At root boundaries:

```cpp
auto operation = NGIN::Async::Spawn(ctx, Parent(ctx));
scheduler.RunUntilIdle();

auto result = operation.TakeResult();
```

For blocking bridge code:

```cpp
auto result = NGIN::Async::SyncWait(ctx, Parent(ctx));
```

Do not use blocking waits inside normal coroutine control flow.

## Error Handling

`Completion<T, E>` separates three non-success states:

- domain error: expected typed failure from `E`
- canceled: cooperative cancellation was requested
- fault: async runtime or infrastructure failure

Typical root handling:

```cpp
auto result = operation.TakeResult();
if (result.Succeeded())
{
    Use(result.Value());
}
else if (result.IsDomainError())
{
    HandleDomainError(result.DomainError());
}
else if (result.IsCanceled())
{
    HandleCancellation();
}
else
{
    HandleFault(result.Fault());
}
```

For value-returning tasks, return a completion for explicit non-success:

```cpp
NGIN::Async::Task<int, DemoError> ParseCount(NGIN::Async::TaskContext&)
{
    co_return NGIN::Async::Completion<int, DemoError>::DomainFailure(DemoError::InvalidInput);
}
```

`Expected<T, E>`, `Unexpected<E>`, and bare `E` domain errors are also accepted
by `Task<T, E>`.

For `Task<void, E>`, use explicit completion awaiters:

```cpp
NGIN::Async::Task<void, DemoError> Validate(bool ok)
{
    if (!ok)
    {
        co_await NGIN::Async::DomainFailure(DemoError::InvalidInput);
        co_return;
    }

    co_return;
}
```

Use `Faulted(...)` only for runtime/infrastructure failures, not ordinary domain
failures.

## Cancellation

`TaskContext` carries the cancellation token. Cancellation-aware await points
such as `YieldNow()` and `Delay(...)` observe it automatically.

`CancellationSource` may be constructed with a
`std::pmr::memory_resource*` when registrations must use a caller-controlled
resource. That resource must outlive the source, every token copied from it,
and every registration associated with it. Registration allocation failure is
reported as `CancellationRegistrationError::ResourceExhausted`.

Manual cancellation checks:

```cpp
NGIN::Async::Task<void, DemoError> Work(NGIN::Async::TaskContext& ctx)
{
    for (;;)
    {
        if (ctx.CheckCancellation())
        {
            co_await NGIN::Async::Canceled();
            co_return;
        }

        co_await ctx.YieldNow();
    }
}
```

## Combinators

`WhenAll` consumes child tasks. Pass freshly created tasks or move existing
task objects into it.

```cpp
auto both = co_await NGIN::Async::WhenAll(ctx, Child(ctx), Child(ctx));
```

```cpp
auto firstIndex = co_await NGIN::Async::WhenAny(
        ctx,
        [](NGIN::Async::TaskContext& child) { return Child(child); },
        [](NGIN::Async::TaskContext& child) { return Child(child); });
```

`WhenAny` accepts factories so it can construct every task with a distinct
child context linked to the parent context. It records the first terminal
child, requests cancellation through every losing child context, and drains
all child watchers before returning. A winning domain error, cancellation, or
fault is propagated after the drain. A loser may safely reference state in the
parent coroutine frame. This structured lifetime adds loser cancellation and
drain time to observable completion latency; children should cooperate with
cancellation or otherwise finish promptly.

## Frame Lifetime and Publication

Task frames use explicit owner, execution, queued-work, and continuation
references. Dropping an `Operation` or calling `Detach` releases only the
owner reference; a running task remains alive until execution and all retained
continuations or executor work have released their references. The final
release is the only operation that destroys the frame.

Continuation installation and terminal completion use a CAS-controlled state.
Completion release-publishes its payload before an awaiting reader observes the
terminal state with acquire semantics. A child continuation retains its parent
frame until the child completion handler has either resumed the parent or
observed that the parent already completed. Queued `YieldNow()` and `Delay()`
callbacks similarly retain the suspended frame, unregister cancellation before
resuming it, and release the queued-work reference when the callback is run or
discarded.

These rules make operation release, detachment, cancellation, and completion
safe to race. They do not make a single `Task` or `Operation` a general-purpose
multi-consumer object; duplicate awaits and repeated result consumption remain
programmer errors.

## Async Generators

Use `AsyncGenerator<T>` for multi-yield async sequences.

Advance a generator with:

```cpp
auto next = co_await generator.Next(ctx);
```

`Next(ctx)` returns `Task<GeneratorNext<T>, E>`.

## Common Mistakes

- Creating a root `Task` and never passing it to `Spawn`, `Detach`, or `SyncWait`.
- Calling `TakeResult()` before an operation is complete.
- Calling `TakeResult()` more than once.
- Awaiting the same running operation from multiple consumers.
- Passing lvalue tasks to `WhenAll` instead of moving/creating tasks.
- Passing preconstructed tasks to `WhenAny`; use child-context factories so
  loser cancellation reaches the task body.
- Mixing incompatible error types across composed tasks.
- Treating cancellation like a domain error.
- Using faults for normal operation failures that should be represented by `E`.

## Reference Notes

- `Task<T, E>` is the coroutine return object.
- `Operation<T, E>` is the owning handle for started root work.
- `Completion<T, E>` is the value-owned terminal-state type.
- `Spawn` is the normal root-start API.
- `Detach` is the explicit fire-and-forget API.
- `SyncWait` is the explicit blocking bridge.
