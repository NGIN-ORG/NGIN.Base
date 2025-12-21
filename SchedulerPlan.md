# SchedulerPlan.md

Performance-first refactor plan for NGIN schedulers + async runtime.

This plan assumes breaking changes are allowed and targets a design where scheduling is useful both:
1) for C++ stackless coroutines (`co_await` / `std::coroutine_handle<>`), and
2) for non-coroutine work (jobs/lambdas), without forcing `Task` usage.

Second priority is ergonomics (a .NET-like async experience) built on top of the runtime.

---

## Progress

- [x] **Time module**: added `NGIN::Time::TimePoint` + `NGIN::Time::MonotonicClock` + `NGIN::Time::SleepFor` (no public `std::chrono`).
- [x] **Timer refactor**: updated `NGIN::Timer` to use `NGIN::Time` internally (no `<chrono>` include).
- [x] **Scheduler/Async API time migration**: replaced `ScheduleDelay(std::chrono::...)` with `ScheduleAt(NGIN::Time::TimePoint)` and updated `TaskContext::Delay` / `Task::Delay` to take `NGIN::Units` time units.
- [x] **Benchmark stability**: fixed Release `SchedulerBenchmarks` crashes by correcting coroutine lifetime in the benchmark harness and making POSIX `FiberScheduler` fibers thread-affine (no cross-thread `ucontext` reuse) + safe `makecontext` argument passing.
- [x] **Timer subsystem**: removed per-delay detached threads in `ThreadPoolScheduler::ScheduleAt` by adding an internal timer queue + timer thread.
- [x] **ExecutorRef (coroutine path)**: introduced `NGIN::Execution::ExecutorRef` and migrated `TaskContext`/`Task` to use it.
- [x] **WorkItem execution**: added `NGIN::Execution::WorkItem` (coroutine or job) and extended `ExecutorRef` + schedulers to execute jobs without coroutines.
- [x] **Executor layer (core)**: schedulers support `Execute(WorkItem)` / `ExecuteAt(WorkItem, TimePoint)` and `ExecutorRef` can dispatch both jobs and coroutines.
- [x] **Concepts + type erasure**: added `include/NGIN/Execution/Concepts.hpp` and removed the `IScheduler` vtable interface in favor of concrete executors + `ExecutorRef`.
- [x] **Thread pool rewrite**: per-worker queues + work stealing + `NGIN::Sync::AtomicCondition` wakeups (spinlock-based queues; can be upgraded to lock-free later).
- [x] **Task continuation cleanup (partial)**: removed detached threads from `Task::Then` and scheduled task continuations via `ExecutorRef` instead of resuming inline.
- [x] **Task completion cleanup (partial)**: replaced `std::condition_variable` waits with `NGIN::Sync::AtomicCondition` + atomic completion flag.
- [x] **SchedulerBenchmarks coverage**: added job enqueue+run baselines for both `FiberScheduler` and `ThreadPoolScheduler`.
- [x] **Benchmark harness semantics**: `BenchmarkContext::start/stop` now control what is measured (no implicit start/stop in the runner).
- [x] **Phase 0 scheduler microbenchmarks**: added contended (multi-producer) job scheduling and `ExecuteAt` timer enqueue baselines.
- [x] **Task cancellation (cooperative)**: `CancellationToken` on `TaskContext` + cancellation-aware `Yield/Delay` (incl. cancellation registrations to wake delays), and `Task::IsCanceled()` tracks `TaskCanceled`.
- [x] **Task combinators (baseline)**: added `WhenAll` and `WhenAny` built on `ExecutorRef` for composing lazy tasks.
- [x] **CancelAfter/CancelAt (baseline)**: added `CancellationSource::CancelAfter(exec, duration)` and `CancelAt(exec, timepoint)` for cancellation-aware timeouts.
- [x] **Token linking**: added `CreateLinkedTokenSource` / `LinkedCancellationSource` to cancel when any input token cancels.
- [ ] **Task cleanup**: implement cancellation propagation rules across composed tasks (beyond pre-cancel checks) and consider `TaskContext`-level token linking for child tasks.

## Goals

- **Fast scheduling**: minimize allocations, syscalls, locking, and context switches.
- **Predictable latency**: timers and wakeups with bounded overhead.
- **Composable execution**: schedulers/executors usable without coroutines.
- **Minimal std exposure**: do not expose `std::chrono` in public APIs; prefer `NGIN::Units` and an NGIN time/clock surface.
- **Pluggable instrumentation**: profiling hooks must be optional and near-zero overhead when disabled.
- **Clear ownership & lifetime**: no detached threads capturing `this`; shutdown is safe and deterministic.

## Non-goals (for the first iteration)

- Full async IO integration (epoll/IOCP/kqueue). Keep as a later extension point.
- Perfect .NET feature parity (SynchronizationContext, ConfigureAwait, etc.).
- Cross-process scheduling.

---

## Current issues (what’s worth fixing)

### Architecture/API coupling

- Schedulers are currently tightly coupled to coroutines via `std::coroutine_handle<>` and `TaskContext`.
- There is no “executor” API that can schedule generic work without using coroutines/`Task`.

### Safety & scalability

- Removed per-delay detached timer threads in `ThreadPoolScheduler` by adding a timer heap + timer thread.
- Removed detached OS threads from `Task::Then`; continuations are scheduled via `ExecutorRef`.
- Remaining: verify `noexcept` guarantees on hot-path `Execute` and keep shutdown deterministic under contention.

### Over-wide interface

- Removed `IScheduler`; optional capabilities are intended to become opt-in concepts/interfaces instead of required vtable methods.

### Time model

- Public scheduler/async APIs no longer use `std::chrono`; time is expressed as `NGIN::Time::TimePoint` + `NGIN::Units`.
- Remaining work: ensure timer handling is allocation-free and does not spawn per-delay threads.

---

## Target architecture (end state)

### 1) Core concept: **Executor**

Define a minimal “executor” surface that can run work, independent of coroutines:

- `Execute(WorkItem)`
- `ExecuteAt(WorkItem, TimePoint)` (or `ExecuteAfter(WorkItem, Duration)`)

`WorkItem` must support:
- coroutine handle execution
- job/function execution

Important: scheduling a work item should be allocation-free in the common case.

### 2) Two-layer API: Concepts + Type-erasure

To balance performance and ergonomics:

- **Concepts** for compile-time composition (fast path, inlining, no vtable).
- **`ExecutorRef`** (lightweight type-erased reference) for runtime selection without templating everything.

This replaces a heavyweight `IScheduler` vtable with a small function-pointer table (or `std::function_ref`-like approach).

### 3) Time: NGIN monotonic clock + Units

Introduce a dedicated time module (names are placeholders):

- `NGIN::Time::MonotonicClock`
  - `static TimePoint Now() noexcept;`
- `NGIN::Time::TimePoint` (opaque monotonic tick value)
- `NGIN::Units`-based durations, e.g. `NGIN::Units::Nanoseconds/Milliseconds/Seconds`

Public APIs use `NGIN::Units` and `NGIN::Time::TimePoint` only.

Implementation uses platform APIs directly:
- Linux: `clock_gettime(CLOCK_MONOTONIC[_RAW])`
- Windows: `QueryPerformanceCounter`

### 4) Scheduler runtime (thread pool)

Make `ThreadPoolScheduler` the default runtime.

Core design goals:

- Per-worker local queue (single-producer/single-consumer ring buffer).
- Global injection queue for external producers (MPMC).
- Work stealing for load balancing.
- A single timer subsystem per scheduler (no per-delay threads).
- Wake-up mechanism based on:
  - `std::atomic::wait/notify` (preferred, already used by `NGIN::Sync::AtomicCondition`)
  - or platform futex/eventfd/WaitOnAddress behind a thin abstraction.

### 5) `Task` becomes a library on top

`NGIN::Async::Task<T>` should be layered on top of `ExecutorRef` (or templated `Executor`), not fused to one scheduler type.

Key behaviors:

- No detached threads.
- `Delay` uses the scheduler’s timer queue.
- `Then` is implemented as coroutine composition (continuation scheduled via executor), not OS threads.

---

## Proposed public API sketch (directional)

### Execution

- `include/NGIN/Execution/ExecutorRef.hpp`
  - `void Execute(std::coroutine_handle<>) noexcept;`
  - `void Execute(NGIN::Utilities::Callable<void()>) noexcept;` (or a cheaper job wrapper)
  - `void ExecuteAt(WorkItem, NGIN::Time::TimePoint) noexcept;`

### Async

- `TaskContext` holds an `ExecutorRef` (not a raw `IScheduler*`).
- `co_await ctx.Yield()` posts continuation via `Execute(handle)`.
- `co_await ctx.Delay(Units::Milliseconds x)` uses `ExecuteAt`.

### Optional capabilities (compile-time)

Instead of forcing everything into one interface, support optional features:

- `Cancellation`
- `Priority`
- `Affinity`
- `Instrumentation`

Expose them via `requires` checks / separate small capability concepts.

---

## WorkItem representation (performance-critical)

Avoid `std::variant<std::coroutine_handle<>, std::function<...>>` due to size/overhead.

Plan:

- A tiny type-erased callable:
  - `{ void (*invoke)(void*) noexcept; void (*destroy)(void*) noexcept; void* storage; }`
- For coroutines, store the handle directly and invoke with `handle.resume()`.
- For jobs, use SBO storage or pool-allocated nodes.
- Provide a freelist/pool per worker for job nodes (no heap on hot path).

---

## Refactor phases

### Phase 0 — Baseline & constraints

- Add scheduler microbenchmarks:
  - enqueue/dequeue throughput
  - contended scheduling with N producers / M workers
  - timer throughput (N timers per second)
  - tail latency distribution
- Document invariants:
  - executor lifetime requirements
  - thread-safety guarantees for `Execute*`
  - shutdown semantics

### Phase 1 — Time module (remove `std::chrono` from public APIs)

- Add `include/NGIN/Time/MonotonicClock.hpp` and `include/NGIN/Time/TimePoint.hpp`.
- Update `NGIN::Timer` to use `MonotonicClock` internally and return `NGIN::Units` without including `<chrono>`.
- Deprecate or remove any public use of `std::chrono` in `Execution` and `Async`.

### Phase 2 — Replace `IScheduler` with `ExecutorRef` + concepts

- Introduce `ExecutorConcept` + `ExecutorRef`.
- Update `TaskContext` to store `ExecutorRef`.
- Update existing schedulers to provide adapters (`MakeExecutorRef(scheduler)`).
- Remove the “wide” base interface and move optional features into separate opt-in interfaces/concepts.

### Phase 3 — ThreadPoolScheduler rewrite (core performance)

- Replace `std::queue` + `std::mutex` with:
  - per-thread SPSC ring buffers (fast path)
  - global MPMC injection queue (fallback)
- Replace `std::condition_variable` with `AtomicCondition` or a platform wait primitive wrapper.
- Ensure `ExecuteAt` is handled via a single timer heap per scheduler.
- Ensure shutdown is safe (no detached threads, no `this` capture after dtor begins).

### Phase 4 — Task/awaitable cleanup

- Remove thread-spawning in `Task::Then` and `Task::Delay`.
- Make `Then` compile into a continuation coroutine or an awaitable that schedules via executor.
- Define cancellation propagation rules (token model).

### Phase 5 — Ergonomics improvements (after perf)

- Default context (optional): a thread-local “current executor”.
- Add helper APIs similar to .NET:
  - `WhenAll`, `WhenAny`
  - `Run` overloads for jobs and coroutines
  - structured cancellation (`CancellationSource/Token`)
- Provide instrumentation hooks behind compile-time flags (or a policy type).

### Phase 6 — Optional: Fiber/stackful lane (only if justified)

If fibers remain:
- Make it a separate “stackful job” module with explicit rules (not the default path for stackless coroutines).
- Ensure it is not required for the Task runtime.

---

## Minimal std dependency strategy

What we can realistically minimize (without being dogmatic):

- No public `<chrono>` usage; route time through `NGIN::Time` + `NGIN::Units`.
- Prefer `NGIN::Sync` primitives for blocking/wakeups:
  - Use `NGIN::Sync::AtomicCondition` as the default park/unpark primitive (built on atomic wait/notify).
  - If predicate waiting is needed, add `NGIN::Sync::Condition` (atomic wait/notify based) rather than `std::condition_variable`.
- Keep `<thread>` in implementation; wrap thread primitives in `NGIN::Execution::Thread` where useful.
- Avoid `std::function` in hot paths; use NGIN job wrappers.

---

## Verification checklist (per phase)

- Unit tests:
  - shutdown safety (no UAF under delayed scheduling)
  - timer correctness (ordering, drift bounds)
  - scheduling fairness basics
- Stress tests:
  - schedule from many threads while destroying scheduler
  - large number of timers
- Benchmarks:
  - throughput + tail latency comparisons against baseline

---

## Open decisions (need answers before implementation)

- Default runtime: **always-running background workers** (thread pool) as the default experience; also ship a **first-class single-thread cooperative executor** for tests/determinism/embedded.
- Runtime polymorphism: **yes**; prefer a lightweight type-erased `ExecutorRef` (function-pointer table) over a wide virtual `IScheduler`.
- `Task` start semantics: **lazy by default**; provide explicit eager entry points (`Start/Run/Spawn`) for .NET-like ergonomics without hidden scheduling.
- Cancellation: **cooperative only**; implement tokens + cancellation-aware waits/timers and explicit checkpoints, no “forced stop” semantics.
