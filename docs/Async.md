# Async

NGIN’s async stack is split across two namespaces:

- `NGIN::Async`: coroutine types and combinators (`Task`, `TaskContext`, cancellation, `WhenAll`, `WhenAny`, generators).
- `NGIN::Execution`: executors/schedulers (`ExecutorRef`, `CooperativeScheduler`, `ThreadPoolScheduler`, etc).

The core idea is: **coroutines stay scheduler-agnostic**, and a `TaskContext` binds them to an `ExecutorRef` + `CancellationToken`.

## Key Types

- `NGIN::Async::Task<T>` / `NGIN::Async::Task<void>`
  - Single-result coroutine type (similar to .NET `Task<T>`).
  - `co_return` produces the result; exceptions propagate to awaiters (`Get()`/`co_await`).
  - `Task` is intentionally *not* a generator; `co_yield` is not part of `Task`.

- `NGIN::Async::TaskContext`
  - Holds an `NGIN::Execution::ExecutorRef` and a `CancellationToken`.
  - Provides awaitables: `Yield()` and `Delay(duration)` (duration is an NGIN Units time quantity).

- `NGIN::Async::CancellationSource` / `CancellationToken`
  - Cooperative cancellation. `TaskContext::ThrowIfCancellationRequested()` throws `TaskCanceled`.

- `NGIN::Async::WhenAll` / `WhenAny`
  - Combinators for awaiting multiple tasks with .NET-like behavior.

- `NGIN::Async::Generator<T>`
  - Synchronous pull generator (`co_yield`) for sequences in non-async code.

- `NGIN::Async::AsyncGenerator<T>`
  - Cooperative async pull generator advanced via `co_await gen.Next(ctx)` returning `std::optional<T>`.

## Getting Started

Typical “root task” pattern:

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

NGIN::Async::Task<int> Compute(NGIN::Async::TaskContext& ctx)
{
    co_await ctx.Yield();
    co_return 123;
}

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext ctx(scheduler);

    auto t = Compute(ctx);
    t.Start(ctx);
    scheduler.RunUntilIdle();
    return t.Get();
}
```

Notes:

- A **root** task must be scheduled somehow (usually `Start(ctx)` or `ctx.Run(...)`).
- Child tasks that are `co_await`’d from a started/bound task can be scheduled automatically.

### `TaskContext::Run`

```cpp
auto task = ctx.Run([](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<int> {
    co_await ctx.Yield();
    co_return 7;
});
// task already started
```

## Cancellation Model

Cancellation is cooperative:

- `TaskContext::Yield()` and `Delay(...)` throw `TaskCanceled` when cancellation is observed.
- Use `ctx.ThrowIfCancellationRequested()` at well-defined points in long-running tasks.

```cpp
NGIN::Async::Task<void> Work(NGIN::Async::TaskContext& ctx)
{
    for (;;)
    {
        ctx.ThrowIfCancellationRequested();
        co_await ctx.Yield();
    }
}
```

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

### `WhenAny`

- `WhenAny(ctx, Task<T>& first, Task<T>&... rest) -> Task<NGIN::UIntSize>`
- Returns the index (0-based) of the first task that completes (success/fault/cancel).

## Generators

### `Generator<T>` (sync)

```cpp
NGIN::Async::Generator<int> Range(int n)
{
    for (int i = 0; i < n; ++i) { co_yield i; }
}

for (int v : Range(3)) { /* 0,1,2 */ }
```

### `AsyncGenerator<T>` (async)

`Next(ctx)` is an awaitable that returns `std::optional<T>`. `nullopt` means the sequence is complete.

```cpp
NGIN::Async::AsyncGenerator<int> Produce(NGIN::Async::TaskContext& ctx)
{
    co_yield 1;
    co_await ctx.Yield();
    co_yield 2;
}

NGIN::Async::Task<int> Consume(NGIN::Async::TaskContext& ctx)
{
    auto gen = Produce(ctx);
    int sum = 0;
    while (auto v = co_await gen.Next(ctx))
    {
        sum += *v;
    }
    co_return sum;
}
```

## Design Notes / Invariants

- `Task<T>` is a **single-result** type: `co_yield` does not belong in `Task`.
- `AsyncGenerator<T>` is a **single-consumer** sequence (concurrent `Next()` calls are not supported).
- Prefer explicit scheduling with `TaskContext` to keep execution deterministic and testable.

