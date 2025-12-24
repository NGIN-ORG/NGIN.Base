# Execution (Thread/Fiber) – API & Implementation Plan

Scope: `include/NGIN/Execution/Thread.hpp` and `include/NGIN/Execution/Fiber.hpp` (and the platform fiber runtime under `src/Async/Fiber/`).

## Progress Tracking
- **Done**
  - Fiber API hygiene: `Fiber::YieldNow()` rename and remove header macro hack.
  - Fiber semantics: yield-to-resumer (stack discipline) implemented and validated with nested-resume test.
  - Calling-context helpers: `include/NGIN/Execution/ThisThread.hpp` and `include/NGIN/Execution/ThisFiber.hpp`.
  - Header hygiene: removed `<Windows.h>` from `include/NGIN/Time/Sleep.hpp`.
  - OS-thread backend: `include/NGIN/Execution/Thread.hpp` now uses Win32/pthreads (no `std::thread`).
  - Capabilities scaffolding: `include/NGIN/Execution/Config.hpp` and initial `Execution_ThreadTests`.
  - Scheduler adoption: `ThreadPoolScheduler` and `FiberScheduler` now use `WorkerThread` (no `std::thread` workers/timer threads).
  - Docs: `include/NGIN/Execution/README.md` added (call patterns + capability overview).
  - Best-effort controls: `ThisThread::{SetAffinity,SetPriority}` added (Windows + Linux) returning `bool`.
  - Debuggability: schedulers assign indexed worker names (`NGIN.TPW.<i>`, `NGIN.FW.<i>`).
  - Fiber error model: `Resume() noexcept` returning `FiberResumeResult` + `TakeException()`; FiberScheduler/tests updated accordingly.
  - Fiber compile-time gating: `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` stubbed in `Fiber.hpp` (errors only when used on unsupported builds).
  - Capability polish: documented `ThisThread::SetPriority` semantics (Windows priority vs Linux nice) in `include/NGIN/Execution/README.md`.
  - ThisFiber semantics: `ThisFiber::IsInFiber()` is strict; added `ThisFiber::IsInitialized()` for thread initialization state.
  - ThreadId hardening: `Thread::GetId()` returns an OS id captured inside the thread proc (with a POSIX fallback only if not yet observed).
  - Fiber gating controls: added `NGIN_EXECUTION_FIBER_HARD_DISABLE` (opt-in hard-disable on unsupported builds).
  - Fiber allocation reduction: removed the extra Fiber PIMPL allocation (no `Scoped<Impl>`; `Fiber` owns a `FiberState*`).
  - Dependency trimming: removed `iostream` logging from `FiberScheduler` and removed `<functional>` from `ThisThread` fallback code.
  - Fiber allocators: `FiberOptions` has a type-erased `FiberAllocatorRef` used for `FiberState` + POSIX stack allocations.
  - Fiber assignment ergonomics: added `Fiber::TryAssign(Job) noexcept` (idle-only assignment fast-path).
  - Fiber guard pages (best-effort): POSIX `ucontext` stack can be `mmap`-backed with a low guard page (`FiberOptions.guardPages`).
  - Fiber backend identification: added `NGIN_EXECUTION_FIBER_BACKEND` macro in `include/NGIN/Execution/Config.hpp` (WinFiber/ucontext; custom backend reserved).
  - Fiber backend selection layer: `src/Async/Fiber/Fiber.posix.cpp` / `src/Async/Fiber/Fiber.win32.cpp` now compile-time dispatch on `NGIN_EXECUTION_FIBER_BACKEND` (ucontext/winfiber routed; custom backend stubbed).
  - Benchmarks: added `benchmarks/FiberBenchmarks.cpp` and `FiberBenchmarks` build target.
  - Benchmarks: extended `benchmarks/SchedulerBenchmarks.cpp` with repeated yield/reschedule workload (`Task<void> Yield x8 2k`) across schedulers.
  - CUSTOM_ASM backend (Linux x86_64/aarch64): added an internal context switch routine + backend implementation, selectable via `NGIN_EXECUTION_FIBER_BACKEND` (CMake: `-DNGIN_BASE_FIBER_BACKEND=custom_asm`).
  - CUSTOM_ASM contract test: validates `mxcsr` + x87 control word are preserved across `YieldNow()`/`Resume()` (enabled only on Linux x86_64 CUSTOM_ASM builds).
  - CUSTOM_ASM hardening: context switch clears DF (`cld`) and integrates ASan fiber stack switch annotations when AddressSanitizer is enabled.
  - CUSTOM_ASM docs: clarified signal handling expectations (`sigaltstack`) and tooling constraints in `include/NGIN/Execution/README.md`.
  - Default backend (Linux x86_64/aarch64): `NGIN_EXECUTION_FIBER_BACKEND` now defaults to `CUSTOM_ASM` (override via `-DNGIN_BASE_FIBER_BACKEND=ucontext` if needed).
- **Current**
  - (Deferred) Benchmark `ucontext` vs `CUSTOM_ASM` baselines.
- **Next**
  - (Optional) Decide whether to block signals around context switch (or keep `sigaltstack` as the documented requirement).

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
Current API is an OS-thread backed handle:
- Backed by Win32 threads / pthreads (no `std::thread`).
- `Thread::Options` supports best-effort naming/affinity/priority + stack size.
- Destructor policy is explicit (`OnDestruct`), with `WorkerThread` joining on destruction for scheduler-owned threads.
- Schedulers use `WorkerThread` (no direct `std::thread`).

### `NGIN::Execution::Fiber`
Public API:
- `Fiber(Job, stackSize)` where `Job = NGIN::Utilities::Callable<void()>`.
- `Assign`, `Resume() noexcept -> FiberResumeResult`, `TakeException()`, `YieldNow() noexcept`, `EnsureMainFiber`.
- `FiberOptions` supports a stack size and a type-erased allocator hook (used for state + POSIX stack).

Implementation:
- Out-of-line (`src/Async/Fiber/FiberCommon.cpp` + `Fiber.*.cpp`).
- No extra PIMPL allocation: `Fiber` owns a `detail::FiberState*`.
- Windows uses OS fibers (`ConvertThreadToFiber`, `CreateFiberEx`, `SwitchToFiber`).
- POSIX uses `ucontext` with a heap-allocated stack.
- Yield-to-resumer semantics are implemented (nested resume works).
- Compile-time gated via `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` (stubbed on unsupported builds).

---

## Current Flaws / Risks

### Thread – API and Semantics
Remaining items after the OS-thread backend migration:
- **Stack size semantics**: `ThreadOptions::stackSize` is platform-dependent; define “best-effort vs guaranteed” per platform in docs and tests.
- **Lifecycle sharp edges**: ensure `OnDestruct::Terminate` is consistently used for general `Thread` and `WorkerThread` is scheduler-only (join on destruction).
- **Best-effort controls**: naming/affinity/priority should remain observable (`bool`) and never throw.

### Fiber – API and Semantics
- **Guard pages are best-effort**: `FiberOptions::{guardPages,guardSize}` are implemented on POSIX via an `mmap`-backed stack with a low guard page; other backends may ignore.
- **POSIX `ucontext` portability**: `ucontext` is obsolete/deprecated on some platforms; long-term viability is questionable.
- **Tier-1 backend is partial**: `CUSTOM_ASM` is implemented on Linux x86_64/aarch64; other POSIX targets use `ucontext` fallback for now.

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
- Prefer explicit contracts over “helpful” behavior on hot paths (e.g., don’t silently no-op on misuse).

### Decisions to Lock Early (to Avoid Churn)
1. **Thread backend**: OS threads by default (Win32 + pthreads) for control and header hygiene; allow an opt-in guarded `std::thread` fallback for niche platforms.
2. **Fiber backend**: introduce an internal backend interface now; treat `ucontext` as tier-2 fallback; plan a tier-1 custom context switch backend (platform/arch-specific).
3. **Thread name ownership**: use a fixed-size inline `ThreadName` (truncate rule) and copy from call-site inputs immediately (no lifetime footguns).
4. **Destructor defaults**: do not silently block in destructors by default; make “join on destroy” an explicit opt-in for scheduler-owned workers.
5. **Programmer error handling**: debug assert with a clear message; release terminate (consistent, predictable).
6. **Fiber error model**: `Resume() noexcept` + status result + explicit exception retrieval (`TakeException()`).
7. **Capabilities**: naming/affinity/priority are best-effort with an observable result; stack size/guard pages are platform-dependent; stackful fibers are compile-time gated.

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
  - `ThreadName name` (copied/owned; constructed from call-site inputs and may be truncated)
  - `UInt64 affinityMask` (optional)
  - `int priority` (optional)
  - `UIntSize stackSize` (optional, platform-dependent)
  - `enum class OnDestruct { Join, Detach, Terminate }`

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
- If `ThreadOptions` exposes features that are not portable (stack size), codify “guaranteed vs best-effort” semantics per platform.
- Prefer the “two type” pattern to avoid destructor surprises:
  - `Thread`: general-purpose thread handle with `OnDestruct::Terminate` default (forces explicit lifecycle).
  - `WorkerThread`: scheduler-owned worker handle with `OnDestruct::Join` default (stop/join is part of scheduler teardown).
- Backend selection policy:
  - Default: OS threads (Win32 + pthreads).
  - Optional fallback: `std::thread` behind a build macro (for niche platforms only).

### “Current Fiber” API (Parallel to `ThisThread`)
Even though fibers are an NGIN primitive (not a standard primitive), having a “current fiber” namespace makes call sites uniform
and keeps the `Fiber` handle class focused.

Proposed header(s):
- `include/NGIN/Execution/ThisFiber.hpp`

Proposed surface:
- `namespace NGIN::Execution::ThisFiber`
  - `bool IsInFiber() noexcept`
  - `void YieldNow() noexcept` (yields to the *resumer*; see below)
  - Optional: `FiberId GetId() noexcept` (debug/telemetry only; can be omitted initially)

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
  - `FiberResumeResult Resume() noexcept` (required) + `std::exception_ptr TakeException() noexcept` for the `Faulted` case
  - `[[nodiscard]] bool IsRunning() const noexcept`, `[[nodiscard]] bool HasJob() const noexcept`
  - `static void YieldNow()` – yields to the *resumer*, not a global “main”.
  - `static bool IsInFiber() noexcept`

- `class FiberThreadContext` (optional RAII):
  - `static FiberThreadContext AttachCurrentThread()` for platforms that require it (Windows fibers).
  - Optionally supports detach on thread exit where legal.

Key invariant: `YieldNow()` always returns execution to the most recent `Resume()` call (“stack discipline”), enabling nested resume patterns and
composability.

### Contracts (Make These Explicit and Testable)
- `Fiber` is thread-affine: a `Fiber` created on thread A must only be assigned/resumed/yielded on thread A (debug assert).
- `Resume()` is not re-entrant for the same fiber: calling `Resume()` while that fiber is already running is a programmer error.
- `ThisFiber::YieldNow()` returns to the most recent active resumer of the current fiber (stack discipline).
- Calling `ThisFiber::YieldNow()` when not in a fiber is a programmer error (debug assert). Decide whether release is no-op or terminate; prefer terminate for “zero surprise”.
- Yield-to-resumer implementation detail/invariant: store the caller context *per Resume()*, and clear it on return to prevent stale pointers/handles.

### Fiber State Model (Recommended)
Define a minimal, scheduler-friendly state machine:
- `Idle` (no job) → `TryAssign` succeeds
- `Ready` (job assigned) → `TryAssign` fails
- `Running` → `TryAssign` fails; `Resume()` is in progress
- On `Resume()` return:
  - `Yielded`: transitions back to `Ready` (job still present)
  - `Completed` / `Faulted`: transitions to `Idle` (job cleared; fiber reusable)

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
6. Add a short rationale section in docs:
   - “Why not just use `std::thread` / `std::this_thread`?” (compile-time hygiene, consistent naming/affinity/priority, allocator hooks, unified contracts).

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
   - Add a compile-time capability macro (proposed): `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` (0/1).
   - (Internal) track backend choice for benchmarks/logging: `enum class FiberBackend { WinFiber, UContext, CustomAsm }`.

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
- scheduler-loop microbenchmark: enqueue N work items that each do minimal work and yield/reschedule (captures cache/queue effects).
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

## Locked Decisions (Implementation Policy)
These are intended to be “go implement” constraints (avoid churn):
1. **Thread backend**: OS threads (Win32 + pthreads) by default; optional `std::thread` fallback behind a build macro for niche platforms.
2. **Fiber backend**: internal backend interface; tier-1 custom context switch backend planned; tier-2 `ucontext` fallback where available; otherwise compile out stackful fibers.
3. **Fiber errors**: `Fiber::Resume()` is `noexcept` and reports `Yielded/Completed/Faulted`; exceptions are retrieved with `TakeException()` and cleared on take.
4. **Allocators**: first redesign includes allocator hooks for fiber stack + state; guard pages/tuning can be added later without changing the surface.
5. **Destruction defaults**: `Thread` defaults to terminate-on-destroy; `WorkerThread` defaults to join-on-destroy (scheduler-owned).
6. **Programmer errors**: debug assert with a clear message; release terminate (wrong-thread usage, yield outside fiber, resume without job, resume-while-running).
7. **Capabilities**:
   - Best-effort (observable): thread naming/affinity/priority return `bool`/status.
   - Platform-dependent: thread stack size, fiber guard pages.
   - Compile-time gated: stackful fibers (`NGIN_EXECUTION_HAS_STACKFUL_FIBERS`).

## Capability Classification (Recommended)
- Guaranteed:
  - Thread creation/join/detach; `ThisThread::{GetId,YieldNow,SleepFor,SleepUntil,RelaxCpu}`.
  - Fiber yield-to-resumer semantics when `NGIN_EXECUTION_HAS_STACKFUL_FIBERS == 1`.
- Best-effort (return `bool`/status so callers can log/ignore):
  - `ThisThread::SetName`, `Thread::SetName`, `Thread::SetAffinity`, `Thread::SetPriority`.
- Platform-dependent / potentially unsupported:
  - Thread stack size at creation; fiber guard pages/guard size; tier-1 custom-asm backend availability.

## Remaining Open Questions (Smaller / Safe to Defer)
1. Which architectures to support first for `CustomAsm` (x86_64 first, arm64 next).
2. Exact stack/guard page API surface in `FiberOptions` (keep minimal initially: `stackSize` + `guardPages` boolean).
3. Whether to hard-disable `Fiber.hpp` on unsupported builds or provide a stub type that fails with a targeted `static_assert`.

---

## Suggested “Definition of Done”
- Thread and Fiber headers no longer include heavyweight platform headers (`<Windows.h>`) or `std::function`/`std::string` on the hot path.
- `Fiber::Yield()` no longer mutates consumer macro state and yields to the resumer.
- Schedulers use the same thread primitive so naming/affinity/priority are coherent across the library.
- Benchmarks exist for fiber resume/yield and at least one scheduler scenario; regression guard numbers recorded.
