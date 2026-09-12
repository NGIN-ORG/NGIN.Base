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
- `TaskScope<E>`: bounded child ownership, shared cancellation, and explicit joining
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

Registering a raw coroutine continuation also reserves its executor delivery
before publishing the callback. Exhausted storage reports `ResourceExhausted`;
an unsupported, stopped, or rejecting executor reports `CompletionUnavailable`.
A coroutine without a valid executor reports `InvalidTarget`. Callback-only
registrations do not require an executor reservation. An empty token remains
an inert successful registration.

Cancellation invokes the callback on the canceling thread; if it requests the
optional continuation, that continuation is queued through its reserved slot,
including during executor shutdown. It never resumes inline. `Reset()` prevents
an invocation that has not begun and waits for an invocation on another thread
to finish its callback and delivery handoff. It does not retract a continuation
already transferred to the executor. The caller owns a registered raw coroutine
through delivery and must keep its executor operational until it has finished.

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
every child before returning. A winning domain error, cancellation, or
fault is propagated after the drain. A loser may safely reference state in the
parent coroutine frame. This structured lifetime adds loser cancellation and
drain time to observable completion latency; children should cooperate with
cancellation or otherwise finish promptly.

Before invoking any factory, `WhenAny` reserves one terminal notification per
child. Insufficient capacity or allocation failure reports a scheduling fault
without starting a child. A factory exception, empty task, or child executor
rejection contributes a terminal fault and still joins the other children.
The combinator requires a tracked parent executor and resumes its join through
that task's existing reservation, including after ordinary admission closes.
It releases child frames and factory captures before returning, and waits for
winner-triggered cancellation callbacks to finish publishing before the join.
Terminal result publication can precede release of a resume callback's frame
lease. The same notification slot is reused to report final frame retirement;
loser locals are gone before the join completes, without another allocation.

## Frame Lifetime and Publication

Task frames use explicit owner, execution, queued-work, and continuation
references. Resume callbacks release their temporary frame holds before
publishing completion or invoking a parent continuation. Active execution keeps
the task alive during its next resume; delivery does not keep a failed child's
locals alive past destruction of parent locals they borrow. Dropping an `Operation` or calling `Detach` releases only the
owner reference; a running task remains alive until execution and all retained
continuations or executor work have released their references. The final
release is the only operation that destroys the frame.

Continuation installation and terminal completion use a CAS-controlled state.
Completion release-publishes its payload before an awaiting reader observes the
terminal state with acquire semantics. A child continuation retains its parent
frame until the child completion handler has either resumed the parent or
observed that the parent already completed. Cancellation-aware `YieldNow()` and
`Delay()` callbacks similarly retain the suspended frame and unregister
cancellation before resuming it. Token-free waits on the task's own executor
use its active execution reference and existing continuation slot. Their submission
handshake prevents resumption or terminal publication until both submission and
dispatch/discard have ended their frame access. Discarding an admitted initial
task, generator advance, yield, or timer delivers a scheduling fault through its
reserved path; immediate rejection preserves the executor's specific error.

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

Calling `Next` retains the producer frame before returning its cold task, so
moving or destroying the generator handle does not invalidate an existing
advance. Producer arguments, contexts, and other borrowed resources must still
outlive that advance. Each advance runs on the producer context's executor and
delivers its result on the consumer task's executor. Both executors must support
completion reservations and remain operational through the advance. Admission
failure is reported before entering the producer; accepted delivery uses reserved
storage, including while the executors are stopping.

Only one advance may produce or consume a result at a time. A competing `Next`
reports `InvalidContinuationState` without interrupting the active advance.
Cancellation before an advance starts enters no producer code. Cancellation
during an advance is observed after the producer reaches its next `co_yield` or
terminal outcome. A producer using a cancellation-aware `TaskContext` wait can
finish promptly when that context is canceled; an uncancelable operation or raw
suspension must finish naturally. Consumer cancellation alone does not detach
the producer or cancel a different producer context. Join the advance before
releasing anything borrowed by the producer.

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


### Cancellation-aware child waits

`task.WithCancellation(ctx)` observes the supplied token without detaching the
child. Cancellation during the wait is reported after the child reaches its
terminal result, so parent-owned buffers remain alive throughout child access.
An already canceled wait does not start a cold child. The child's own context
controls cancellation of its operations; share or link cancellation tokens when
both parent and child should cancel together. Task and Operation status queries
observe terminal publication before reading the completion payload.

Linked context creation may allocate. `BindLinkedCancellationToken` and
`WithLinkedCancellationToken` propagate `std::bad_alloc`; the original context
remains unchanged if allocation fails.

## Owned child tasks

`TaskScope<E>` retains each child operation, its context, and its factory until
joining. `Spawn(factory)` uses the scope executor; `SpawnOn(executor, factory)`
selects an independently driven executor. Factories receive a stable
`TaskContext&` with the scope cancellation token. All children use the scope's
error type; successful child values are discarded.

Admission reserves a terminal notification before invoking the factory. The
configurable child limit defaults to 256 and includes completed children retained
before Join. Saturation returns `ScheduleError::ResourceExhausted`. A child
factory or task fault after admission is reported by Join. Check Spawn's result:
a rejected child is not part of the scope.

`RequestCancel()` requests cancellation without waiting. The first observed
child failure also requests sibling cancellation. `co_await scope.Join()` closes
child admission, waits for every admitted child, releases their frames and factory
captures, and returns a borrowed `const Completion<void, E>&`. It reports the
first observed domain failure, fault, or cancellation, or success when all children
succeed. Sequential joins may inspect the same result. `TakeResult()` moves the
joined result once; borrowed outcome references must no longer be used afterward.

Join uses its awaiting task's existing continuation reservation, so it remains
available during runtime shutdown. Canceling its context does not abandon the
join. Only one Join may be pending. Destroying a scope with unfinished children
or a pending join terminates; destruction never blocks or detaches work. Completed
children may be destroyed without a join, but their failures are then unobserved.

Capture child-owned data by value, or keep borrowed data alive through Join.
In particular, children borrowing locals from a root coroutine must be joined
before those locals leave scope. The root helper can join remaining children
whose data belongs to their retained factories or the caller above RunTask.

Started tasks on tracked executors reserve one reusable continuation slot for
their lifetime. Child success, failure propagation, and Operation observation run
on the parent's executor using that slot after ordinary admission closes. A new
cold task still requires admission. WhenAll joins already-started children without
starting helper tasks between waits. Executors without reservation support can
run standalone tasks, but a suspended child wait requires tracked delivery.

For standalone I/O applications, `IO::RunTask` owns a root task, TaskScope, and
runtime shutdown; see [I/O runtime](IORuntime.md).
