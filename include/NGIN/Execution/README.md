# NGIN::Execution

This module contains low-level execution primitives used by `NGIN::Async`:

- Executors/schedulers (`ExecutorRef`, `CooperativeScheduler`, `ThreadPoolScheduler`, `FiberScheduler`, `InlineScheduler`)
- Stackful fibers (`Fiber`) and calling-context helpers (`ThisThread`, `ThisFiber`)
- OS-thread wrapper (`Thread`, `WorkerThread`)

## Call Patterns

### Thread / WorkerThread

- `Thread` (general-purpose):
  - `Thread::Options::onDestruct` defaults to terminate (forces explicit lifecycle).
  - Use `Join()` or `Detach()` explicitly.

- `WorkerThread` (scheduler-owned):
  - Always join-on-destroy (safe for scheduler teardown).

Pattern:
- Create → `Start(...)` → `Join()`/`Detach()` → Destroy

### Fiber / ThisFiber

Pattern:
- `Fiber::Assign(job)` / `TryAssign(job)` → `Resume()` ↔ `ThisFiber::YieldNow()` → `Completed/Faulted`

Invariants:
- Yield-to-resumer (“stack discipline”): `YieldNow()` returns to the most recent active `Resume()`.
- Thread-affine: a `Fiber` must be resumed on the thread that owns it.
- `Resume()` is `noexcept` and returns `FiberResumeResult`; failures are observed via `TakeException()`.

## Capabilities (Guaranteed vs Best-effort)

The goal is “predictable behavior”: unsupported features do not silently succeed.

### Guaranteed (all supported platforms)
- `ThisThread::{GetId, YieldNow, RelaxCpu, SleepFor, SleepUntil, HardwareConcurrency}`
- `Thread` creation + `Join()`/`Detach()`
- `WorkerThread` join-on-destroy

### Best-effort (returns `bool` so callers can log/ignore)
- `ThisThread::SetName(...)`
- `ThisThread::SetAffinity(mask)` (platform-dependent)
- `ThisThread::SetPriority(value)` (Windows thread priority; Linux uses best-effort nice value for current TID)
- `Thread::{SetName, SetAffinity, SetPriority}`

### Platform-dependent
- Thread stack size at creation (`Thread::Options::stackSize`)
- Fiber guard pages (future)

### Compile-time gating
- Stackful fibers: `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` in `include/NGIN/Execution/Config.hpp`
