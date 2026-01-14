# Async

Async in NGIN means writing code that can **suspend instead of blocking** and later **resume on an executor** when it
can make progress. It is built on standard C++ *stackless coroutines*: the coroutine machinery handles suspension and
state, while NGIN decides *when/where* to resume continuations.

The design is performance-first and explicit:

- No hidden global runtime: you choose an executor/scheduler.
- No implicit thread usage: some executors are cooperative (you “pump” them), others run background workers.
- Cancellation is cooperative: code must observe it (`CheckCancellation`, `YieldNow`, `Delay`).

## Architecture

NGIN’s async stack is split across two namespaces:

- `NGIN::Async`: coroutine types and combinators (`Task`, `TaskContext`, cancellation, `WhenAll`, `WhenAny`, generators).
- `NGIN::Execution`: executors/schedulers (`ExecutorRef`, `CooperativeScheduler`, `ThreadPoolScheduler`, etc).

The core idea is: **coroutines stay scheduler-agnostic**, and a `TaskContext` binds them to an `ExecutorRef` +
`CancellationToken`.

## Mental Model

- A `Task<T>` is a *description* of work that can complete later (it is not necessarily running yet).
- A `TaskContext` describes *where* the work runs (executor) and *under what cancellation rules* (token).
- Work runs only after it is scheduled (either explicitly, or implicitly when awaited from a running task).

If you are familiar with C#/.NET:

- `Task<T>` ≈ `Task<T>` (but cold by default; see below)
- `TaskContext` ≈ `SynchronizationContext` + `CancellationToken`
- `ExecutorRef` ≈ a scheduler / dispatcher

## Key Types

- `NGIN::Async::Task<T>` / `NGIN::Async::Task<void>`
  - Single-result coroutine type (similar to .NET `Task<T>`).
  - `co_return` produces the result; `Get()`/`co_await` return `AsyncExpected<T>` for explicit error handling.
  - `Task` is intentionally *not* a generator; `co_yield` is not part of `Task`.
  - `Task<T>` instances are **cold by default**:
    - Constructing a task does not start execution.
    - Execution begins once the task is scheduled (e.g. `Start(ctx)`), or when it is first `co_await`’d from a task that
      is already running in a context.

- `NGIN::Async::TaskContext`
  - Holds an `NGIN::Execution::ExecutorRef` and a `CancellationToken`.
  - Provides awaitables: `YieldNow()` and `Delay(duration)` (duration is an NGIN Units time quantity).
  - Tasks scheduled through a context share its executor and cancellation scope.
  - Typical usage is long-lived and reused across many tasks. Avoid mutating (`Bind(...)`) while tasks are concurrently
    using the context.

- `NGIN::Async::CancellationSource` / `CancellationToken`
  - Cooperative cancellation. `TaskContext::CheckCancellation()` returns `AsyncErrorCode::Canceled`.

- `NGIN::Async::WhenAll` / `WhenAny`
  - Combinators for awaiting multiple tasks with .NET-like behavior.

- `NGIN::Async::Generator<T>`
  - Synchronous pull generator (`co_yield`) for sequences in non-async code.

- `NGIN::Async::AsyncGenerator<T>`
  - Cooperative async pull generator advanced via `co_await gen.Next(ctx)` returning `AsyncExpected<std::optional<T>>`.

## Getting Started

Typical “root task” pattern:

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

NGIN::Async::Task<int> Compute(NGIN::Async::TaskContext& ctx)
{
    auto yielded = co_await ctx.YieldNow();
    if (!yielded)
    {
        co_return std::unexpected(yielded.error());
    }
    co_return 123;
}

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext ctx(scheduler);

    auto t = Compute(ctx);
    t.Start(ctx);
    scheduler.RunUntilIdle();
    auto result = t.Get();
    if (!result)
    {
        return 1;
    }
    return *result;
}```

Notes:

- A **root** task must be scheduled somehow (usually `Start(ctx)` or `ctx.Run(...)`).
- Child tasks that are `co_await`’d from a started/bound task can be scheduled automatically.

Execution flow:

1. `Compute(ctx)` constructs a `Task<int>` (no coroutine body runs yet).
2. `t.Start(ctx)` schedules the coroutine on the executor inside `ctx`.
3. `scheduler.RunUntilIdle()` drives execution until no runnable work remains.
4. `t.Get()` retrieves an `AsyncExpected<int>`; check for errors or cancellation.

### `TaskContext::Run`

`TaskContext::Run` is a convenience helper that creates a task, binds it to the context, and schedules it immediately.

```cpp
auto task = ctx.Run([](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<int> {
    auto yielded = co_await ctx.YieldNow();
    if (!yielded)
    {
        co_return std::unexpected(yielded.error());
    }
    co_return 7;
});
// task already started
```

Use `Run` for top-level entry points where you don’t need to control the start moment.

## Error Handling

- Tasks do not throw; failures are returned as `AsyncExpected<T>`.
- Unhandled exceptions (when enabled) are converted to `AsyncErrorCode::Fault`.
- `co_await` and `Get()` always return `AsyncExpected<T>` so callers can branch explicitly.

```cpp
NGIN::Async::Task<void> Fails(NGIN::Async::TaskContext&)
{
    co_await NGIN::Async::Task<void>::ReturnError(
            NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Fault));
    co_return;
}
```

If you schedule and run `Fails`, `Get()` will return `AsyncExpected<void>` with `AsyncErrorCode::Fault`.
## Cancellation Model

Cancellation is cooperative:

- `TaskContext::YieldNow()` and `Delay(...)` return `AsyncErrorCode::Canceled` when cancellation is observed.
- Use `ctx.CheckCancellation()` at well-defined points in long-running tasks.

```cpp
NGIN::Async::Task<void> Work(NGIN::Async::TaskContext& ctx)
{
    for (;;)
    {
        auto cancel = ctx.CheckCancellation();
        if (!cancel)
        {
            co_await NGIN::Async::Task<void>::ReturnError(cancel.error());
            co_return;
        }

        auto yielded = co_await ctx.YieldNow();
        if (!yielded)
        {
            co_await NGIN::Async::Task<void>::ReturnError(yielded.error());
            co_return;
        }
    }
}
```

Cancellation notes:

- Cancellation is **cooperative**: if your task never checks the token, it will not stop �?oby itself�??.
- Child tasks run under whatever `TaskContext` you pass to them (same context ��' same token).
- `WhenAll`/`WhenAny` observe cancellation from the `TaskContext` they are awaited with, not from any implicit global.
## Combinators

### `WhenAll`

- `WhenAll(ctx, Task<T>&...) -> Task<std::tuple<T...>>`
- `WhenAll(ctx, Task<void>&...) -> Task<void>`

```cpp
auto a = Foo(ctx);
auto b = Bar(ctx);
auto all = NGIN::Async::WhenAll(ctx, a, b);
all.Start(ctx);
co_await all; // or all.Get() after completion
```

Failure behavior:

- If any child task faults, the combined task faults (first error wins).
- If the context is canceled, the combined task returns `AsyncErrorCode::Canceled`.
- Remaining tasks continue executing unless canceled via the context.

### `WhenAny`

- `WhenAny(ctx, Task<T>& first, Task<T>&... rest) -> Task<NGIN::UIntSize>`
- Returns the index (0-based) of the first task that completes (success/fault/cancel).

Failure behavior:

- `WhenAny` completes when any input completes, even if that task faulted or was canceled.
- `WhenAny` itself faults/cancels only if the *context* is canceled.
- After `WhenAny` returns an index, call `Get()` on the corresponding task to observe its `AsyncExpected` result.

## Generators

Use `Generator<T>` when:

- You want a synchronous sequence (range-for) produced lazily.
- No async work occurs between produced elements.

Use `AsyncGenerator<T>` when:

- Producing the next element requires async work (e.g. waiting, IO, scheduling).
- You want backpressure via `co_await Next(ctx)`.

### `Generator<T>` (sync)

```cpp
NGIN::Async::Generator<int> Range(int n)
{
    for (int i = 0; i < n; ++i) { co_yield i; }
}

for (int v : Range(3)) { /* 0,1,2 */ }
```

### `AsyncGenerator<T>` (async)

`Next(ctx)` is an awaitable that returns `AsyncExpected<std::optional<T>>`. `nullopt` means the sequence is complete.

```cpp
NGIN::Async::AsyncGenerator<int> Produce(NGIN::Async::TaskContext& ctx)
{
    co_yield 1;
    auto yielded = co_await ctx.YieldNow();
    if (!yielded)
    {
        co_await NGIN::Async::AsyncGenerator<int>::ReturnError(yielded.error());
        co_return;
    }
    co_yield 2;
}

NGIN::Async::Task<int> Consume(NGIN::Async::TaskContext& ctx)
{
    auto gen = Produce(ctx);
    int sum = 0;
    for (;;)
    {
        auto next = co_await gen.Next(ctx);
        if (!next)
        {
            co_return std::unexpected(next.error());
        }
        if (!*next)
        {
            break;
        }
        sum += **next;
    }
    co_return sum;
}

## Design Notes / Invariants

- `Task<T>` is a **single-result** type: `co_yield` does not belong in `Task`.
- `AsyncGenerator<T>` is a **single-consumer** sequence (concurrent `Next()` calls are not supported).
- Prefer explicit scheduling with `TaskContext` to keep execution deterministic and testable.

## Common Pitfalls

- Forgetting to schedule a root task (`Start(ctx)` / `ctx.Run(...)`).
- Assuming a task starts running when constructed.
- Calling `Get()`/`Wait()` without driving the executor (e.g. not pumping `CooperativeScheduler`).
- Using `AsyncGenerator` from multiple consumers concurrently.
- Forgetting to observe cancellation in long-running loops.










