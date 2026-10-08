# Async

`NGIN::Async` is the coroutine layer used across `NGIN.Base`.

Use it when you want:

- coroutine-based async code
- typed domain errors instead of exception-driven async control flow
- explicit cancellation
- task composition without hidden runtime threads

Most users only need these types:

- `Task<T, E>`: a cold coroutine object
- `Operation<T, E>`: a move-only result consumer; its scope owns execution
- `AsyncEnvironment` / `TaskContext`: executor, cancellation, deadline, resource, and logical metadata
- `Completion<T, E>`: the value-owned terminal result
- `TaskScope<E>`: bounded child ownership, shared cancellation, and explicit joining
- `WhenAll` / `Race`: joined task groups and owning competition
- `AsyncSemaphore`: cancellation-aware permit acquisition and scoped permit ownership

## Core Contract

`Task<T, E>` is cold and move-only. Creating a task does not schedule work.

Start root work with:

- `TaskScope::Spawn(...)` for joined concurrent work
- `TaskSupervisor::Transfer(...)` for explicitly supervised application work
- `SyncWait(ctx, std::move(task)) -> Completion<T, E>` for blocking bridge code

Inside another coroutine, compose child tasks with `co_await`. Child task
failures propagate automatically. Awaiting consumes the source task, including an lvalue task or a
`WithCancellation` await. The awaiter owns the child through completion and retirement. A second await
reports `InvalidTaskUsage`, whether the original child is pending or complete; Task objects must not be
accessed concurrently from multiple threads. Fresh children using the active executor transfer directly and borrow
one reserved continuation slot from their running root. They no longer submit or reserve a separate slot
at each helper layer. Blocking-wait signaling, completion tickets, observer delivery, continuation publication,
and submission handshake storage belong to one execution-state allocation per running root/producer/executor
boundary. Allocation and reservation of this state are compiled into the Execution component. Sequential promises borrow that chain state; they retain only the per-frame outcome, continuation,
context, and lifetime bookkeeping needed by suspension and backend leases. The current direct-chain work budget is 256 transfers (child entries plus returns);
longer chains defer through the root slot. The cooperative scheduler alternates reserved deliveries with
ordinary work/due timers when both are ready. Full runtime PollOnce accounting and the remaining I/O
placement migration are still being completed in the AsyncRuntime redesign.

`Task<T, E>::FromValue(value)`, `Task<void, E>::FromValue()`, and
`Task<T, E>::FromCompletion(completion)` construct inline ready computations. They allocate no coroutine
frame, continuation reservation, or runtime result storage; the payload may have its own allocations.
Ready results can be sequentially awaited or synchronously consumed without scheduling.
Scoped spawning still requires a tracked executor and bounded owner admission, including for ready inputs.
Empty tasks remain invalid.
Cancellation requested by a wait cannot replace an inline result already published before that wait.
A successful coroutine child with outstanding frame leases temporarily transfers its result into the owning
awaiter's inline storage and waits for frame retirement before resuming its parent. The ordinary success path
without outstanding leases still returns directly.

Task moves have a conditional exception specification because inline payload moves may
throw. Operation moves are noexcept transfers of result interest. During a coroutine await, a throwing payload move becomes the awaiting task's exception fault;
Scoped spawning reports a throwing ready-result transfer through the admitted child completion. Ordinary
C++ construction and moves outside those boundaries may throw. Awaiting or spawning consumes the ready
task using the same rules as a coroutine-backed task.

`TaskContext(AsyncEnvironment)` validates and snapshots the starter's logical properties. Scoped admission snapshots
the owner environment before starting the child; nested children inherit the active environment. Use
`co_await CurrentEnvironment()` to obtain a value snapshot without scheduling. For example:

```cpp
NGIN::Async::Task<int> ReadLogicalMetadata()
{
    auto environment = co_await NGIN::Async::CurrentEnvironment();
    NGIN::Async::TaskContext current(environment);
    co_await current.YieldNow();
    co_return *static_cast<const int*>(environment.metadata);
}
```

The environment contains `executor`, `cancellation`, an optional monotonic `deadline`, a non-null PMR
`resource`, borrowed `metadata`, and optional integer `priority`. Absent properties require no allocation.
Copies retain the existing token; ordinary helper inheritance does not create a linked source or register
a callback. Deadline enforcement and priority scheduling are separate policies; these fields currently carry
the inherited properties. Explicit context-taking leaf operations still use their selected delivery binding.

Execution-state allocation and pending TaskContext wait state use the inherited resource. The resource,
executor, metadata, and owners of cancellation links must outlive work that borrows them. A context passed
as a coroutine argument must also outlive that argument's use. Merely copying a token does not extend the
lifetime of registrations owned by a separate linked source/context. A cold compiler frame is allocated
before startup using the normal coroutine allocation policy; a later environment cannot retroactively
choose its allocator. Resource rejection produces a scheduling fault before the root body runs. Backend
and structured-owner allocation policy is being integrated in the later redesign stages.

`co_await task.AsCompletion()` consumes a child and returns its full Completion to the awaiting body,
including domain errors, cancellation, and faults. It also works when the parent's error type differs.
Ready outcomes return immediately; a coroutine child with outstanding frame leases retires before the
awaiting body continues. Empty or consumed tasks return an InvalidTaskUsage completion.

Include `<NGIN/Async/ExecutorTransitions.hpp>` for explicit placement:

- `StartsOn(executor, task)` admits target delivery before starting the child on that executor.
- `ContinuesOn(executor, task)` preserves incoming startup and reserves terminal delivery before starting
  the child. Value, domain-error, canceled, and fault outcomes all reach that executor, including when
  admission closes after reservation or the child switches elsewhere.
- `co_await SwitchTo(executor)` changes the active default while preserving cancellation, deadline,
  resource, metadata, and priority. Same-executor transfers may remain inline within the chain budget.
- `co_await YieldNow()` always defers using the active environment, subject to cooperative cancellation.

Ordinary Task completion carries the child's current executor to its parent; it does not restore the prior
executor. Use StartsOn for an explicit child startup override: merely passing a different TaskContext as a
coroutine argument no longer overrides inherited startup. TaskContext yields/delays adopt their explicit
delivery executor. Observing an independently spawned Operation still resumes on the observer's executor.
Transitions acquire target capacity before releasing the prior delivery guarantee; rejected admission does
not enter the inner task. ContinuesOn's retained terminal reservation is an explicit boundary cost, including
when its target initially matches the caller.

File and directory operations, socket operations, and asynchronous resolution explicitly switch to their
TaskContext executor before validation and submission. This executor becomes the task chain's active
executor for early errors, cancellation, and pending completion alike. File fallback workers, native I/O
backends, and resolver workers perform their backend work on their own resources; they deliver task
continuations through the selected context executor. Target admission failure starts no backend work.
Stage 1 fairness and host-loop budget checks are recorded in the AsyncRuntime measurements.

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

    auto result = NGIN::Async::SyncWait(ctx, Compute(ctx));
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
auto result = NGIN::Async::SyncWait(ctx, Parent(ctx));
```

For blocking bridge code:

```cpp
auto result = NGIN::Async::SyncWait(ctx, Parent(ctx));
```

Do not use blocking waits inside normal coroutine control flow.

## Error Handling

`Completion<T, E>` has exactly three terminal channels: `Value`, `Error`, and `Stopped`.
`AsyncError<E>` distinguishes a typed domain payload from an infrastructure `AsyncFault` inside the error
channel. `StopReason` distinguishes requested cancellation, deadline, owner closure, and runtime shutdown.
A stop request does not replace an already-published value or error.

Typical root handling:

```cpp
auto result = operation.TakeResult();
if (result.HasValue())
    Use(result.Value());
else if (result.IsStopped())
    HandleStop(result.StopReason());
else if (result.Error().IsDomainError())
    HandleDomainError(result.Error().DomainError());
else
    HandleFault(result.Error().Fault());
```

`TaskStatus` adds `Pending` to these three terminal states. Task and Operation expose `HasError()` and
`IsStopped()`; inspect the consumed completion to discriminate error payloads. `DomainFailure` and `Faulted`
construction helpers both create the same Error channel. Use `Completion::Failure(AsyncError<E>)` to forward
an error without extracting or copying its payload, and `Completion::Stopped(reason)` to preserve a stop
reason. For void coroutines, `co_await Stopped(reason)` records the stopped outcome.

An AsyncError normally owns its payload inline. Explicit `AsyncError::Retain` accepts immutable shared
error storage (including an alias into an externally owned concurrent state); lvalue inspection does not
consume move-only payloads. Keep the whole AsyncError when forwarding a retained error. Never store an
owning alias back into the state it retains, which would create a reference cycle.

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
such as `YieldNow()` and `Delay(...)` observe it automatically. Ordinary sequential helpers copy the
inherited token without linking sources or registering callbacks. Cannot-stop tokens have no cancellation
state to allocate, register, lock, or update atomically.

Token equality compares source identity. Linking a context to its own token or to an empty token preserves
its existing cancellation boundary. Explicit linked sources register each distinct stoppable parent once;
empty parents need no registration. `Race` owns one linked cancellation source per competitor and
passes its token to descendants, so loser cancellation does not require an additional source/link pair.

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
            co_await NGIN::Async::Stopped();
            co_return;
        }

        co_await ctx.YieldNow();
    }
}
```

## Async synchronization

Include `<NGIN/Async/AsyncSemaphore.hpp>` (also included by `<NGIN/Async.hpp>`)
and link the Execution component. Unlike `NGIN::Sync::Semaphore`, pending
acquisition suspends the coroutine instead of blocking an executor thread.

```cpp
NGIN::Async::Task<void> Update(NGIN::Async::AsyncSemaphore& semaphore)
{
    auto permit = co_await semaphore.AcquireAsync();
    // Exclusive access if semaphore was constructed as AsyncSemaphore(1, 1).
    // The permit remains owned across co_await and releases at scope exit.
    co_await NGIN::Async::YieldNow();
}
```

`AsyncSemaphore(initialCount, maximumCount)` requires a positive maximum and an
initial count no greater than that maximum. The maximum defaults to the largest
`UIntSize`. `AcquireAsync()` is a cold `Task<Permit>`; its move-only guard returns
one permit on destruction or explicit `Permit::Release()`. Moving transfers
ownership; releasing an empty guard is harmless. Keep the guard in a variable
for the full critical section. For manual ownership, `co_await WaitAsync()`
acquires one permit and `Release()` returns one. `TryAcquire()` attempts manual
acquisition immediately; `CurrentCount()` snapshots available permits. Releasing
above the maximum is a programmer error. The semaphore itself is neither copyable
nor movable; guard access must be serialized, but separate guards and semaphore
operations may run on different threads.

Inside `Task<T, E>` with a domain-error type, use `WaitAsync<E>()` or
`AcquireAsync<E>()` to match that type. Semaphore failures remain infrastructure
faults; cancellation preserves its stop reason.

Wait tasks inherit their caller's executor, cancellation token, and allocation
resource. An available permit requires no pending-state allocation or extra
delivery reservation. Pending waits reserve delivery before queueing, use the
inherited PMR resource, and resume on that executor. Pending waiters receive
permits FIFO; cancellation removes a waiter without consuming a permit. Release
and cancellation race to select exactly one outcome. Cancellation after a grant
does not revoke it, even before its queued continuation runs. Scheduling,
allocation, or cancellation-registration failure returns an AsyncFault and leaves
the permit count unchanged. Deadlines are not enforced implicitly; use the
existing cancellation/timeout composition APIs when needed.

The semaphore must outlive every returned task, in-flight call, and guard.
Cancel and join pending work before destruction; destruction does not cancel
waiters. The executor and PMR resource must outlive pending delivery. The complete
[AsyncSemaphore example](../examples/AsyncSemaphore/main.cpp) demonstrates two
tasks protecting shared state across suspension.

## Combinators

`WhenAll` consumes child tasks into an internal scope. Pass freshly created tasks or move existing
task objects into it. It joins every admitted child before returning, including when later admission
or value consumption fails. Child errors request sibling stop and outrank stopped children; errors
are selected in argument order. Admission or value-consumption faults are reported after cleanup.
Successful values remain in argument order.

```cpp
auto both = co_await NGIN::Async::WhenAll(ctx, Child(ctx), Child(ctx));
```

```cpp
auto result = co_await NGIN::Async::Race(
        ctx,
        [](NGIN::Async::TaskContext& child) { return Child(child); },
        [](NGIN::Async::TaskContext& child) { return Child(child); }).AsCompletion();
if (result.Succeeded()) {
    const auto index = result.Value().index;
    // result.Value().value is a variant indexed by the input position.
}
```

`Race` replaces `WhenAny`. Factories share their domain-error type E, while their successful value types
may differ. The result is `Task<RaceWinner<Ts...>, RaceFailure<E, Ts...>>`. `RaceWinner` contains the
input `index` and variant `value`; void uses `std::monostate`, and duplicate types remain separate
index-addressed alternatives.

The owner prepares every child record, cancellation source, execution reservation, cleanup reservation,
and retained winner slot before invoking any factory. Admission failure invokes no factories and releases
all partial reservations. Startup then closes admission and invokes the factories with distinct owned
child contexts linked to the parent. A factory exception or empty Task becomes that competitor's error.

An atomic claim at producer terminal publication selects the winner, before queued observation or final
frame retirement. Ready factory results use input order. The winner requests `OwnerClosed` stop through
the losing contexts; all competitors must actually finish and retire before Race returns. A losing task
may therefore borrow its enclosing caller's resources. Slow or noncooperative losers delay completion.
The final join uses the awaiting task's reserved continuation even after ordinary executor admission closes.

An error winner remains primary. Otherwise any loser error overrides a successful or stopped winner.
`RaceFailure::winner` retains the selected index/kind, `value` retains a successful `RaceWinner` through
an immutable shared reference, and `stopped` retains a stopped winner's reason. Its bounded `errors`
array contains an optional immutable error for each input, including losers. `primaryIndex` and
`Primary()` select the error winner or the lowest input index with an error. Moving this failure does
not move the retained successful value or move-only child errors. The environment's allocation resource
must outlive retained failure data. With no errors, the winner's value or stop reason is returned.

The previous WhenAny header and symbol are removed; migrate includes to `<NGIN/Async/Race.hpp>`,
read `RaceWinner::index` instead of a bare index, and inspect child failures through `RaceFailure`.
Use `FirstCompleted` for non-consuming observation of independently owned operations.

## Frame Lifetime and Publication

Task frames use explicit owner, execution, queued-work, and continuation
references. Resume callbacks release their temporary frame holds before
publishing completion or invoking a parent continuation. Active execution keeps
the task alive during its next resume; delivery does not keep a failed child's
locals alive past destruction of parent locals they borrow. Dropping a scoped `Operation`
releases result interest; its scope or supervisor still owns execution through retirement.
A running task remains alive until execution and all retained continuations or executor
work have released their references. The final
release is the only operation that destroys the frame.

Concurrent continuation installation and terminal completion use CAS-controlled state; sequential
children install their continuation before starting.
Completion release-publishes its payload before an awaiting reader observes the
terminal state with acquire semantics. A child continuation retains its parent
frame until completion either transfers directly to the parent or delivers its queued continuation.
Failure forwarding retires the child's retained frame before completing the parent, using the root slot;
this keeps deep failed chains from recursively destroying all child frames during root destruction. Cancellation-aware `YieldNow()` and
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
storage, including while the executors are stopping. At a yield or terminal
boundary, delivery may resume the consumer inline when its executor is current
and the shared dispatch budget permits it. Otherwise the reserved continuation
queues delivery. Producer execution ownership is released before consumer code runs.

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

- Creating a root `Task` and never admitting it to a scope/supervisor or passing it to `SyncWait`.
- Calling `TakeResult()` before an operation is complete.
- Calling `TakeResult()` more than once.
- Awaiting the same running operation from multiple consumers.
- Passing lvalue tasks to `WhenAll` instead of moving/creating tasks.
- Passing preconstructed tasks to `Race`; use child-context factories so
  loser cancellation reaches the task body.
- Mixing incompatible error types across composed tasks.
- Treating cancellation like a domain error.
- Using faults for normal operation failures that should be represented by `E`.

## Reference Notes

- `Task<T, E>` is the coroutine return object.
- `Operation<T, E>` singly consumes a scoped child result; the scope owns execution.
- `Completion<T, E>` is the value-owned terminal-state type.
- Use `TaskScope::Spawn` for concurrent children and `WithScope` for asynchronous closure.
- `SyncWait` and `IO::RunTask` own and join root execution.
- Global `Spawn` and naked `Detach` are removed. Use an explicit scope or supervisor for concurrency.


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
error type. `Spawn(Task<T, E>)` also accepts a cold task directly. Both overloads return
`std::expected<Operation<T, E>, ScheduleError>`. Rejected cold tasks are destroyed without starting.

The returned move-only handle singly consumes the child's `Completion<T, E>` through `co_await`
or, after completion, `TakeResult()`. A second consumption reports `InvalidTaskUsage`. Dropping it
leaves the scope responsible for execution, errors, and joining. Result readiness follows final child
frame retirement, including outstanding frame leases. A pending consumer resumes on its own executor
using its existing task continuation reservation. Pending awaits require a tracked NGIN task.

Errors remain immutable and retained independently of consumption, so awaiting a move-only child error
does not remove it from the scope report. A result handle may outlive a joined scope; its environment's
allocation resource must outlive the handle and any retained error payloads.

Admission reserves the child record, result state, terminal notification, and child execution
continuation before invoking the factory. Startup uses that reserved continuation directly, so it
requires no later admission to the executor's general work queue. The
configurable child limit defaults to 256 and includes completed children retained
before Join. Saturation returns `ScheduleError::ResourceExhausted`. A child
factory or task fault after admission is reported by Join. Check Spawn's result:
a rejected child is not part of the scope.

`RequestStop()` requests cancellation without waiting; `Close()` only closes admission.
`CancellationSource::Cancel(reason)` and `TaskScope::RequestStop(reason)` accept a `StopReason`,
defaulting to Requested. The first request wins atomically. Tokens expose it through `GetStopReason()`;
linked tokens, task checkpoints, and generator cancellation preserve it. WithScope closure and sibling
failure request OwnerClosed. File, socket, and resolver adapters preserve the reason when delivering cancellation; file/socket
runtime stop uses RuntimeShutdown, and socket close uses OwnerClosed. Automatic deadline policy and
root ownership migration are still being integrated. A child error
requests sibling cancellation promptly. A stopped child alone does not request sibling cancellation.
`co_await scope.Join()` waits for every admitted child to retire and releases factory captures before
returning an owning `ScopeReport<E>`. Errors are ordered by spawn index, regardless of completion order;
errors outrank stopped outcomes. `Errors()` exposes immutable error payloads, `StoppedReason()` retains
stopped information even when an error is primary, and `RetainError(index)` forwards an error without
copying move-only payloads. Sequential joins inspect the same report. `TakeResult()` obtains it once.

The scope also accepts `AsyncEnvironment` and a child capacity; child contexts retain its metadata,
allocator, deadline, and priority while observing the scope-owned cancellation token. Scope state,
child records, result storage, cancellation ownership, and the reserved child/report tables use that
resource. It must outlive escaping handles, reports, errors, and cancellation tokens. Allocation failure
while reserving child records returns ResourceExhausted before invoking the factory. Join builds its
report within the capacity reserved at construction.

Use `WithScope<E>(environment, capacity, body)` for asynchronous closure. The owned body receives
`TaskScope<E>&` and returns `Task<T, E>`. The helper returns `Task<T, ScopeFailure<E>>`: it closes admission,
requests stop on body exit, and joins all children before propagating the outcome. A body error is primary;
otherwise the lowest-spawn-index child error is primary. ScopeFailure retains secondary child errors.
If no error occurred, a stopped body/child yields Stopped; otherwise the body's value is returned.

```cpp
auto work = NGIN::Async::WithScope(ctx.GetEnvironment(), 4,
    [](NGIN::Async::TaskScope<>& scope) -> NGIN::Async::Task<void> {
        auto admitted = scope.Spawn([](NGIN::Async::TaskContext& child) -> NGIN::Async::Task<void> {
            co_await child.YieldNow();
        });
        if (!admitted) throw std::runtime_error("child admission failed");
        (void) co_await scope.Join();
    });
```

Scope-owned result handles and the SyncWait/RunTask root entry points use structured ownership.
Global Spawn and naked Detach are removed; public Operation handles cannot own producer frames.

`SyncWait` admits a cold root through a scope and blocks until its frame leases retire and its owner
notification ends resource access. It then joins and releases the scope before returning. Its executor
must be driven independently; do not block an executor thread needed by the root. Ready inputs still
require no executor or ownership allocation. A rejected cold input never starts.

Join uses its awaiting task's existing continuation reservation, so it remains
available during runtime shutdown. Canceling its context does not abandon the
join. Only one Join may be pending. Destroying a scope with unfinished children
or a pending join terminates; destruction never blocks or detaches work. Completed
children may be destroyed without a join, but their failures are then unobserved.

Capture child-owned data by value, or keep borrowed data alive through Join.
In particular, children borrowing locals from a root coroutine must be joined
before those locals leave scope. The root helper can join remaining children
whose data belongs to their retained factories or the caller above RunTask.

Running roots on tracked executors retain an active continuation slot, shared with sequential helpers.
Transitions acquire a replacement before releasing that guarantee; ContinuesOn also holds its explicit
terminal reservation. Sequential child delivery uses the active chain executor after ordinary admission
closes. Independent Operation observation uses the observer's executor. A new
cold task still requires admission. WhenAll joins already-started children without
starting helper tasks between waits. Executors without reservation support can
run standalone tasks, but a suspended child wait requires tracked delivery.

For standalone I/O applications, `IO::RunTask` owns a root task, TaskScope, and
runtime shutdown; see [I/O runtime](IORuntime.md).

Runtime PollOnce now charges eligible sequential task entry/return transfers against its batch allowance,
in addition to dequeued callbacks. An exhausted allowance queues the chain through its retained delivery
slot. Eligible task, timer, I/O, generator, and final structured-join delivery uses the same dispatch
check. Nested cooperative, thread-pool, and runtime dispatch shares a depth guard. Internal observer and
frame-retirement notifications remain queued to serialize their lifetime protocol; their final user join
may run inline. Stage 1 budget choices are recorded in the AsyncRuntime measurements; final performance acceptance remains open. This accounting cannot
preempt arbitrary synchronous callback instructions.


## Non-consuming completion observation

Include `<NGIN/Async/FirstCompleted.hpp>` to observe scope-owned operations:

```cpp
std::array refs {first.Observe(), second.Observe()};
auto notification = co_await FirstCompleted(refs, waitToken).AsCompletion();
if (notification.Succeeded()) {
    const auto index = notification.Value().index;
    const auto kind = notification.Value().kind;
    // The selected operation's value/error is still available to its consumer.
}
// The existing scopes still own their operations and must join them.
```

`OperationRef` is copyable and retains notification storage without owning execution. Creating or
copying it does not allocate. `Kind()` returns an optional `CompletionKind`, available after final
producer retirement and preserved after result consumption or scope destruction. The environment's
allocation resource must outlive retained operation references, just as it must outlive result handles.
Invalid or moved-from operations produce invalid references.
Operation handles are produced only by scoped admission; use `TaskScope::Spawn` or `TaskSupervisor::Spawn`.

`FirstCompleted` returns an input index and terminal kind without consuming any result or stopping any
operation. Already-completed inputs use input order. Pending inputs arbitrate atomically when their
notifications are published, before observer-executor dispatch. A supplied token cancels only this wait
and preserves its stop reason. An already-completed input takes precedence over a pre-canceled token.
Empty spans or invalid references fail with `InvalidTaskUsage`.

Keep the span alive until the cold observation task starts. A pending wait retains its own references
and allocates one bounded registration array and state from the observing environment's resource.
Cancellation registration uses the token's resource. Allocation rejection returns a scheduling fault.
Every registration is removed, including callbacks already running on another thread, before the caller
resumes. The observing executor must remain driven through that delivery; cancellation never abandons
cleanup. Ordinary sequential Tasks do not allocate this observer state.


## Owned deadlines and timeouts

Include `<NGIN/Async/WithDeadline.hpp>` for a monotonic absolute deadline, or
`<NGIN/Async/WithTimeout.hpp>` for a relative duration:

```cpp
Task<void> Work() {
    TaskContext current(co_await CurrentEnvironment());
    co_await current.Delay(NGIN::Units::Seconds(60));
}

auto outcome = co_await WithTimeout(NGIN::Units::Seconds(2), Work()).AsCompletion();
if (outcome.IsStopped()) {
    const auto reason = outcome.StopReason(); // Deadline when expiry caused the stop.
}
```

`WithTimeout(duration, task)` computes its monotonic deadline once when the cold wrapper starts.
`WithDeadline(timePoint, task)` creates a local boundary only when the deadline is earlier than the
inherited one. Equal/later deadlines share the existing owner's token and timer; helper depth does not
multiply timers or extend the original deadline. A stricter boundary installs its deadline and linked
stop token in the current environment while preserving executor, resource, and other properties.
Inner Tasks use `CurrentEnvironment()` to observe those inherited properties; existing caller-owned
TaskContext references are not rewritten by a wrapper.

A boundary allocates its state and cancellation linkage from the environment resource and admits a
removable executor timer before starting inner work. Unsupported timer removal or admission exhaustion
returns a scheduling fault without starting that work. Already-completed inputs preserve their result
without timer admission. Zero/negative durations request immediate deadline stop; non-finite durations
fail admission, and large finite durations saturate at the latest representable monotonic tick.

Expiry requests cooperative stop with `StopReason::Deadline`. It does not force the result to stopped:
a value or domain error produced by the inner task is preserved. The wrapper awaits actual inner
completion, including final frame retirement, removes pending timer work, and joins a timer callback
already claimed by an executor. Cleanup may therefore finish after the deadline. Parent cancellation
preserves its first reason; discarded timer delivery requests `RuntimeShutdown` stop. All timer cleanup
uses the awaiting task's existing continuation reservation. Executors remain alive and driven through it.

For a strict limit on how long an observer waits, use `FirstCompleted` with a separately owned operation
and an observation-cancellation token; that operation's existing owner still joins its execution.


## Application-lifetime supervision

Include `<NGIN/Async/TaskSupervisor.hpp>` for an explicit owner of application work. The application
keeps this owner and its executor alive until its asynchronous shutdown completes:

```cpp
TaskSupervisor<> background(co_await CurrentEnvironment(), 64);
auto admitted = background.Transfer([](TaskContext& child) -> Task<void> {
    co_await child.Delay(NGIN::Units::Seconds(1));
});
// Handle a rejected admission before assuming the work started.
auto report = co_await background.Shutdown();
```

`Spawn`/`SpawnOn` return ordinary scope-owned Operation handles. `Transfer` admits a cold task or owned
factory and relinquishes result interest while keeping execution and diagnostics supervised. The owner
reuses retired successful/reported child slots; capacity bounds active or retained child records rather
than the total number of tasks over the application's lifetime. Each child uses the same execution and
cleanup admission as TaskScope. Result handles can survive slot reuse and owner destruction after join.

The optional `TaskSupervisor<E>::ErrorSink` contains a noexcept function pointer and borrowed context.
It receives a retained immutable `AsyncError<E>` by value after producer frame retirement and outside
owner locks. Return true only after reporting succeeds or another error owner accepts it; return false
when reporting fails or its bounded queue is full. The sink must be nonblocking and support concurrent
calls when the owner uses a multithreaded executor. Move-only errors can be retained without copying.
The sink context and allocation resource must outlive their uses and retained error references.

A missing or rejecting sink retains the error, closes new admission, and requests `OwnerClosed` stop.
The shutdown report exposes every unhandled error in admission order. Successfully reported errors do
not force sibling cancellation and are not duplicated in that report. Retained errors cannot exceed
admitted child capacity. A missing sink is therefore an observable fallback, not silent error loss.

`TryJoin()` closes admission and returns an optional report only after cleanup finishes, without
installing a pending waiter. Event-loop roots can poll it while continuing to drive work; do not combine
it with an outstanding asynchronous join. `Join()` closes admission and waits for all admitted work. `Shutdown()` also requests
`RuntimeShutdown` stop; the first stop reason is preserved. Callback reporting, factory release, and
record deallocation finish before the owner publishes joined readiness. Destruction with live work
fails fast instead of blocking or detaching. Transferred tasks must own their captures or borrow only
resources that outlive the supervisor join. Remaining legacy UI detach callers are still being migrated;
new application work should use explicit transfer to this owner.
