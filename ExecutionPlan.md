# Execution (Thread/Fiber) – API & Implementation Plan

Scope: `include/NGIN/Execution/Thread.hpp` and `include/NGIN/Execution/Fiber.hpp` (and the platform fiber runtime under `src/Async/Fiber/`).

Goals:
- **State-of-the-art performance**: no avoidable allocations, low overhead per resume/schedule, and predictable behavior.
- **Usability**: safe defaults, explicit lifecycle, and a coherent “NGIN-native” surface (fits `WorkItem`/`ExecutorRef` patterns).
- **Minimal std footprint**: avoid heavyweight headers (`<Windows.h>`, `<functional>`, `<string>`) in hot/public headers; prefer narrow, layered headers.
- **Breaking changes allowed**: prioritize the right API over preserving current surface.

Non-goals (for this plan):
- Redesigning all schedulers (`ThreadPoolScheduler`, `FiberScheduler`) end-to-end, except where required to align with the new Thread/Fiber APIs.

---

## Current State Summary

### `NGIN::Execution::Thread`
Current API is a thin wrapper around `std::thread`:
- Construction/`Start` takes `std::function<void()>`.
- Destructor calls `std::terminate()` if still joinable (std::thread semantics).
- `SetName(const std::string&)` is Windows-only and includes `<Windows.h>` in the public header.
- Provides `SleepFor/SleepUntil` helpers, duplicating `NGIN::Time` functionality.
- Not used by the schedulers (they use `std::thread` directly).

### `NGIN::Execution::Fiber`
Public API:
- `Fiber(Job, stackSize)` where `Job = NGIN::Utilities::Callable<void()>`.
- `Assign`, `Resume`, `Yield`, `EnsureMainFiber`.

Implementation:
- Out-of-line (`src/Async/Fiber/FiberCommon.cpp` + `Fiber.*.cpp`) and uses a PIMPL (`Scoped<Impl>`) which adds an extra heap allocation per `Fiber`.
- Windows uses OS fibers (`ConvertThreadToFiber`, `CreateFiberEx`, `SwitchToFiber`).
- POSIX uses `ucontext` with a heap-allocated stack.

---

## Current Flaws / Risks

### Thread – API and Semantics
- **High std overhead in the API**: `std::function` (type-erased + often heap allocating) is used for the entry point; this is inconsistent with `WorkItem`/`Callable`.
- **Heavy header dependency**: `<Windows.h>` is included by `Thread.hpp` on `_WIN32`, impacting compile times and macro pollution for all consumers.
- **Surprising RAII**: destructor terminates if joinable (like `std::thread`) which is hostile to usability in a library wrapper.
- **Move assignment footgun**: `Thread::operator=(Thread&&)` blindly move-assigns the `std::thread`; if `*this` is joinable, `std::thread` move assignment terminates.
- **`SetName` encoding**: Windows path converts `std::string` to `std::wstring` by byte widening, which is not UTF-8 safe.
- **Duplicated concerns**: `SleepFor/SleepUntil` belong in `NGIN::Time` (or as free functions), not on a thread handle type.
- **Inconsistent adoption**: schedulers use `std::thread` directly, so `Thread` does not centralize naming/affinity/priority or policy.

### Fiber – API and Semantics
- **Macro hack**: `#undef Yield` at the end of `Fiber.hpp` is dangerous to consumers (it mutates preprocessor state outside NGIN).
- **Yield-to-main limitation**: both POSIX and Windows implementations yield back to “main fiber context”, not to the *resuming* context. This prevents safe nested resumes and makes `Yield()` semantics non-local.
- **Unspecified thread affinity**: fibers are effectively thread-affine, but the API does not document/enforce this.
- **Extra allocation per fiber**: `Scoped<Impl>` + `new FiberState` + stack allocation; at least two allocations on POSIX, one extra on Windows.
- **Stack allocation not configurable**: no guard pages, no alignment/commit/reserve controls, no allocator selection (despite NGIN having allocator infrastructure).
- **POSIX `ucontext` portability**: `ucontext` is obsolete/deprecated on some platforms; long-term viability is questionable.
- **Diagnostics**: POSIX `ThrowErrno("...")` does not include `errno`; Windows uses `FormatMessage` (good), POSIX should match (use `std::system_error`).
- **Tests are stale**: `tests/Async/Fiber.cpp` claims exception propagation is “not supported” on POSIX, but the implementation *does* propagate via `exception_ptr`.

---

## Target API (Proposed End State)

### Design Principles
- Prefer **value types** with explicit lifecycle and predictable behavior.
- Avoid imposing heavy std headers from public headers; use **layering**:
  - tiny forward/traits headers (`ThreadFwd.hpp`, `FiberFwd.hpp`),
  - public API headers (`Thread.hpp`, `Fiber.hpp`),
  - platform/impl headers only included where needed (or compiled `.cpp` if the project is not strictly header-only).
- Use **NGIN-native callable types** (`NGIN::Utilities::Callable` / `WorkItem`) for entry points.
- Treat misuse as programmer error: **assert in debug**, avoid exceptions on hot paths unless part of the contract.
-Prefer explicit contracts over “helpful” behavior on hot paths (e.g., don’t silently no-op on misuse).

### Replacement for `std::this_thread` (Proposed)
Add a dedicated “current thread” API mirroring the standard library split between `std::thread` (handle) and `std::this_thread`
(free functions for “the calling thread”).

Proposed header(s):
- `include/NGIN/Execution/ThisThread.hpp` (small, ubiquitous, minimal includes)

Proposed surface:
- `namespace NGIN::Execution::ThisThread`
  - `ThreadId GetId() noexcept`
  - `void YieldNow() noexcept`
  - `void RelaxCpu() noexcept` (thin wrapper over `NGIN_CPU_RELAX()` for spin-wait loops)
  - `template<Units::QuantityOf<TIME>> void SleepFor(duration) noexcept` (delegates to `NGIN::Time::SleepFor`)
  - `void SleepUntil(TimePoint) noexcept` (delegates to `NGIN::Time` helpers)
  - Optional (NGIN extension): `void SetName(std::string_view) noexcept`, `void SetAffinity(UInt64 mask) noexcept`,
    `void SetPriority(int) noexcept`

Rationale:
- Keeps “current thread” utilities usable without pulling in the heavier `Thread` handle type.
- Avoids `<thread>` and avoids platform headers in most translation units.
- Mirrors standard library ergonomics (readability and familiarity) while staying NGIN-native (`Units`, `TimePoint`).

### Thread – Proposed Surface
Introduce a modern thread primitive usable by schedulers and end users:

- `struct ThreadOptions`:
  - `ThreadName name` (recommended) or `std::string_view name` (only if applied during `Start` and not retained)
  - `UInt64 affinityMask` (optional)
  - `int priority` (optional)
  - `UIntSize stackSize` (optional, platform-dependent)
  - `enum class OnDestruct { Join, Detach, Terminate }` (default should favor usability: `Join` for worker threads)

- `class Thread` (move-only):
  - `Thread()` default
  - `template<std::invocable F> explicit Thread(F&& entry, ThreadOptions = {})`
  - `void Start(...)` overloads mirroring ctor
  - `void Join()`, `void Detach()`, `[[nodiscard]] bool Joinable() const noexcept`
  - `void SetName(...)`, `void SetAffinity(...)`, `void SetPriority(...)`
  - `[[nodiscard]] ThreadId GetId() const noexcept` where `ThreadId` is an NGIN type (avoid leaking `std::thread::id` into the API)
  - `[[nodiscard]] NativeThreadHandle NativeHandle() const noexcept` (opaque, platform-specific typedef)

Notes:
- If `std::thread` remains the backing implementation, hide it behind a `.cpp` or a small `detail` layer so `<thread>` isn’t forced into every include chain.
- If OS threads are used directly (pthreads/Win32), `Thread` can avoid `<thread>` entirely and gain stack size + affinity/priority control.
-If `ThreadOptions` exposes features that are not portable (stack size), codify “guaranteed vs best-effort” semantics per platform.

### “Current Fiber” API (Parallel to `ThisThread`)
Even though fibers are an NGIN primitive (not a standard primitive), having a “current fiber” namespace makes call sites uniform
and keeps the `Fiber` handle class focused.

Proposed header(s):
- `include/NGIN/Execution/ThisFiber.hpp`

Proposed surface:
- `namespace NGIN::Execution::ThisFiber`
  - `bool IsInFiber() noexcept`
  - `void YieldNow() noexcept` (yields to the *resumer*; see below)
  - `FiberId GetId() noexcept` 

Important: avoid naming any public API `Yield` to prevent Windows macro collisions without resorting to `#undef` in NGIN headers.

### Fiber – Proposed Surface
Reframe Fiber as a low-level, thread-affine, stackful primitive:

- `struct FiberOptions`:
  - `UIntSize stackSize` (required/default)
  - optional: `UIntSize guardSize`, `bool guardPages`
  - optional: allocator hook for stack/state (`AllocatorRef` or allocator template)

- `enum class FiberResumeResult { Yielded, Completed, Faulted }` (recommended)
  - Prefer “strict” behavior: `Resume()` requires a job; “no job assigned” is a debug assert/programmer error.

- `class Fiber` (move-only):
  - `explicit Fiber(FiberOptions = {})`
  - `bool TryAssign(Job) noexcept` (fast-path, no throw; returns false if running)
  - `void Assign(Job)` (validated/throwing wrapper if desired)
  - `FiberResumeResult Resume() noexcept` (recommended) + `std::exception_ptr TakeException() noexcept` for the `Faulted` case
  - `[[nodiscard]] bool IsRunning() const noexcept`, `[[nodiscard]] bool HasJob() const noexcept`
  - `static void YieldNow()` – yields to the *resumer*, not a global “main”.
  - `static bool IsInFiber() noexcept`

- `class FiberThreadContext` (optional RAII):
  - `static FiberThreadContext AttachCurrentThread()` for platforms that require it (Windows fibers).
  - Optionally supports detach on thread exit where legal.

Key invariant: `Yield()` always returns execution to the most recent `Resume()` call (“stack discipline”), enabling nested resume patterns and composability.

### Contracts (Make These Explicit and Testable)
- `Fiber` is thread-affine: a `Fiber` created on thread A must only be assigned/resumed/yielded on thread A (debug assert).
- `Resume()` is not re-entrant for the same fiber: calling `Resume()` while that fiber is already running is a programmer error.
- `ThisFiber::YieldNow()` returns to the most recent active resumer of the current fiber (stack discipline).
- Calling `ThisFiber::YieldNow()` when not in a fiber is a programmer error (debug assert). Decide whether release is no-op or terminate; prefer terminate for “zero surprise”.

---

## Recommended Refactors / Changes (Phased)

### Phase 0 – Hygiene + Documentation (low risk)
1. Add `include/NGIN/Execution/README.md` documenting invariants:
   - thread-affinity rules, allowed call patterns, and failure modes.
2. Fix fiber test expectations:
   - update POSIX “exception propagation not supported” test to reflect actual behavior (or explicitly disable propagation and document it).
3. Remove macro pollution from `Fiber.hpp`:
   - rename `Fiber::Yield()` to `Fiber::YieldNow()` (or similar), **or**
   - use `#pragma push_macro("Yield") / pop_macro("Yield")` on MSVC/Clang-cl compatible toolchains (still less desirable than renaming).
4. Add a “call patterns” section (docs):
   - Thread: Create → Start → Join/Detach → Destroy (and destructor policy).
   - Fiber: Assign → Resume ↔ YieldNow … → Completed/Faulted.
5. Add a platform capability table (docs):
   - thread backend, fiber backend, guard pages, notes per platform.

### Phase 1 – Thread API Redesign (breaking)
1. Replace `std::function<void()>` with:
   - `template<std::invocable F> Start(F&&)` and/or `NGIN::Utilities::Callable<void()>`.
2. Introduce `NGIN::Execution::ThisThread` as the replacement for `std::this_thread` (`YieldNow`, `SleepFor`, `SleepUntil`, `GetId`).
3. Remove `SleepFor/SleepUntil` from `Thread` (or deprecate), keeping sleep APIs under `NGIN::Time`/`ThisThread`.
4. Remove `<Windows.h>` from `include/NGIN/Execution/Thread.hpp`:
   - move naming/affinity/priority into a platform detail header or `.cpp`.
5. Define destructor policy (`OnDestruct`) and fix move-assignment semantics to never terminate unexpectedly.
6. Add `ThreadOptions` and allow naming/affinity/priority at creation so schedulers can configure workers without races.
7. Make the “name lifetime” rule explicit:
   - if `string_view`, it is consumed during `Start` only (no retention), or
   - prefer `ThreadName` (fixed-size inline buffer) to avoid lifetime footguns.

### Phase 2 – Fiber Core Redesign (breaking, medium/high risk)
1. Make yield/resume **return-to-resumer**:
   - POSIX: store a pointer to the caller `ucontext_t` in `FiberState` and swap back to it on yield.
    - Windows: store the caller fiber handle (`GetCurrentFiber()`) per resume and yield back to it.
2. Remove the extra PIMPL allocation:
   - store `FiberState*` directly (opaque pointer) and keep ownership in `Fiber`.
3. Integrate NGIN allocator hooks for stack/state where possible; add optional guard pages.
4. Decide and commit to an exception model (recommended for predictable schedulers):
   - `Resume()` is `noexcept` and reports `Faulted`; exception is retrieved via `TakeException()`.
   - Provide a thin wrapper helper `ResumeOrThrow()` (optional) for user code that prefers exceptions.
5. Add `NGIN::Execution::ThisFiber` so call sites can use `ThisFiber::YieldNow()` without needing a `Fiber&` handle in scope.
6. Address “state of the art” context switching:
   - Keep `ucontext` as tier-2 fallback.
   - Add an internal backend point for a custom context switch implementation (platform/arch-specific, likely assembly), gated by a capability macro.

### Phase 3 – Adoption in Schedulers (breaking/behavioral)
1. Update `ThreadPoolScheduler` and `FiberScheduler` to use `Execution::Thread` (or the new thread primitive):
   - centralize worker naming, affinity, priority, and teardown policy.
2. Ensure schedulers do not pull heavy headers into unrelated translation units.
3. Gate Windows-only components (`FiberScheduler`) behind platform macros in the public headers, or split into platform-specific headers.

### Phase 4 – Performance + Benchmarking (required for “state of the art”)
Add dedicated benchmarks:
- `benchmarks/FiberBenchmarks.cpp`
  - `Resume/Yield` latency (median/p95), with/without exception propagation, with/without assigned job.
  - cost of `TryAssign + Resume` with different callable sizes.
- `benchmarks/ThreadBenchmarks.cpp` (optional)
  - thread creation/join costs, name/affinity application costs.

Add profiler hooks (opt-in):
- lightweight counters for resumes/yields and scheduler queue lengths under a compile-time flag.

### Phase 5 – Performance Targets (make “state of the art” measurable)
Record and track at least:
- `Fiber Resume+Yield` latency (median/p95) per backend/platform (baseline vs new).
- allocations per `Fiber` creation (goal: 0 beyond stack; or explicitly “1 for stack” if unavoidable).
- `ThreadPoolScheduler` dispatch overhead for `WorkItem` under light and heavy contention.

---

## Breaking Changes Summary (Expected)
- `Thread` entry-point type and destructor semantics change.
- `Thread::GetId()` returns an NGIN `ThreadId` instead of `std::thread::id` (if we choose to remove `<thread>` exposure).
- `Fiber::Yield()` renamed (or macro behavior removed).
- `Fiber::Yield()` semantics change to yield to the resumer (behavioral breaking change but fixes composability).
- Possible change from throwing APIs to `Try*`/status-return APIs on hot paths.

---

## Open Questions / Decisions to Make Early
1. **Backing implementation for Thread**:
   - keep `std::thread` (simpler, less code) vs OS threads (better control + fewer std dependencies).
2. **Fiber portability strategy**:
   - continue with `ucontext` as fallback, but introduce a “fast path” context switch implementation for supported architectures, or
   - deprecate stackful fibers on platforms where `ucontext` is unreliable.
3. **Exception policy for fibers**:
   - propagate exceptions (current) vs explicit error channels for `noexcept` resume.
4. **Allocator integration depth**:
   - just use allocator for stack memory, or also for `FiberState` and internal bookkeeping.
5. **Destruction policy defaults**:
   - is `OnDestruct::Join` safe for the intended usage (scheduler-owned workers), or should default force explicit lifecycle?
6. **Release-mode behavior on programmer error**:
   - for `YieldNow()` outside a fiber, `Resume()` without a job, resume-while-running, wrong-thread usage.

---

## Suggested “Definition of Done”
- Thread and Fiber headers no longer include heavyweight platform headers (`<Windows.h>`) or `std::function`/`std::string` on the hot path.
- `Fiber::Yield()` no longer mutates consumer macro state and yields to the resumer.
- Schedulers use the same thread primitive so naming/affinity/priority are coherent across the library.
- Benchmarks exist for fiber resume/yield and at least one scheduler scenario; regression guard numbers recorded.
