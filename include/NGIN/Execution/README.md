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
- `ThisFiber::IsInFiber()` is a strict check (true only while executing inside a running fiber); `ThisFiber::IsInitialized()` checks thread initialization.

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

## Fault Policy

- `WorkItem::Invoke()` is `noexcept` and terminates on exceptions escaping a job/coroutine.
- `FiberScheduler` captures job exceptions in the fiber trampoline; it terminates if a fiber reports `FiberResumeResult::Faulted` to keep behavior consistent.

### Platform-dependent
- Thread stack size at creation (`Thread::Options::stackSize`)
- Fiber guard pages (`FiberOptions::{guardPages,guardSize}`): best-effort (POSIX uses an `mmap`-backed stack with a low guard page; Windows fibers use OS-managed stacks)
  - Note: when guard pages are enabled on POSIX, the fiber stack uses OS virtual memory; `FiberOptions::allocator` is still used for `FiberState`.

### Compile-time gating
- Stackful fibers: `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` in `include/NGIN/Execution/Config.hpp`
- Fiber backend selection: `NGIN_EXECUTION_FIBER_BACKEND` in `include/NGIN/Execution/Config.hpp` (currently WinFiber on Windows, ucontext on POSIX)
- Optional hard-disable: define `NGIN_EXECUTION_FIBER_HARD_DISABLE=1` to make including fiber headers an error when unsupported.
  - Repo convenience: top-level CMake supports `-DNGIN_BASE_FIBER_BACKEND=default|ucontext|winfiber|custom_asm`.

## CUSTOM_ASM backend policy (Linux x86_64)

When `NGIN_EXECUTION_FIBER_BACKEND == NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM`, fibers use an internal x86_64 context switch routine.

Guarantees:
- The CPU integer callee-saved registers and FP control state (`mxcsr`, x87 control word) are preserved across `Resume()`/`YieldNow()`.
- Job exceptions still do not cross the fiber boundary: they are captured in the fiber trampoline and retrieved via `Fiber::TakeException()`.

Debugging / unwinding / profiling:
- Stack unwinding is **not supported across** `YieldNow()`/`Resume()` (treat it like `setjmp/longjmp`).
- Stack traces **within** the currently-running fiber should work normally.
- External sampling profilers/stack walkers may stop at the context-switch boundary (backend-dependent).
