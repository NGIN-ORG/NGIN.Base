# Execution, threads, and fibers

`NGIN/Execution.hpp` is the focused umbrella for schedulers, native threads,
fibers, and the Async task model.

Use:

- `InlineScheduler` when queued work should run immediately
- `CooperativeScheduler` for caller-driven task progress
- `ThreadPoolScheduler` for explicitly owned worker threads
- `Thread` and `ThisThread` for native thread control
- `FiberScheduler`, `Fiber`, and `ThisFiber` for cooperative stackful work
- `Task<T, E>` and `TaskContext` for coroutine composition

Schedulers and drivers are explicit owners. Creating a task does not start it,
and no global scheduler or worker pool is created behind the caller's back.
Cancellation is a terminal async state, distinct from a domain error and from
an unexpected exception/fault.

See [Async](Async.md) for task composition and [Synchronization](Sync.md) for
cross-thread coordination.
