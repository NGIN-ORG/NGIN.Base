# NGIN.Base Correctness Hardening Plan

## Purpose

This plan turns the review of `NGIN.Base` at commit `c0215e5` into a staged
implementation program. It prioritizes correctness in asynchronous execution,
cancellation, allocators, containers, smart pointers, and work-item storage
before further low-level feature expansion.

The existing seven-component dependency graph remains in place. This is a
hardening and consolidation effort, not a repository rewrite or physical
component split.

## Scope

The plan covers:

- allocator ownership and allocation-failure contracts;
- exception-safe container construction, relocation, and mutation;
- allocator-aware smart-pointer construction and destruction;
- work-item storage and scheduler submission failures;
- cancellation registration ownership and concurrent unregistration;
- coroutine continuation, completion, detachment, and destruction races;
- structured lifetime management in `WhenAny`;
- consolidation of overlapping foundational APIs;
- functional component packaging and standalone correctness gates.

The plan assumes source-breaking cleanup is acceptable before release. New
low-level features should remain frozen until the correctness phases are
complete.

## Deferred Work

The following release-oriented work is intentionally excluded:

- ABI symbol snapshots;
- ABI compatibility guarantees or readiness reviews;
- compatibility or deprecation shims solely for unreleased APIs;
- shared-library symbol-visibility hardening;
- ABI/API compatibility policies;
- release-readiness documentation and release gates;
- independent repositories or release cadences for the components.

## Cross-Cutting Contracts

Before changing the affected implementations, establish and document these
common rules:

1. Allocator `Allocate()` operations are non-throwing and return `nullptr` on
   exhaustion.
2. Owning factories and containers translate allocation failure into
   `std::bad_alloc` when their public API is exception-based.
3. Recoverable domain failures use `std::expected`.
4. Scheduler rejection and resource exhaustion use an explicit scheduling
   result.
5. Programmer contract violations use assertions or documented preconditions.
6. Unexpected exceptions crossing an asynchronous task boundary become an
   `AsyncFault`.
7. Functions that allocate or invoke user-supplied hash, equality, callable,
   or allocator operations are not unconditionally `noexcept`.
8. Every concurrent ownership transfer has a documented linearization point
   and happens-before relationship.

## Performance Guardrails

Correctness takes priority, but hardening should not move exceptional-case
costs into normal hot paths. Use the reviewed `c0215e5` tree as the performance
baseline and compare each affected phase against it. Build the baseline and
candidate with the same optimized configuration, compiler, standard library,
benchmark inputs, and machine settings. Run enough repetitions to distinguish
a regression from normal measurement noise.

Track at least:

- median and tail latency;
- throughput under single-threaded and contended workloads;
- allocations per operation;
- peak memory during transactional rebuilds;
- object and control-block sizes where they affect dense storage;
- generated code size for heavily instantiated templates when practical.

Benchmark these operations:

- task creation, spawn, await, completion, cancellation, and detachment;
- cancellation registration, reset, and firing;
- inline and heap-backed `WorkItem` construction and execution;
- immediate and timed scheduler submission;
- `Vector` append, indexed insertion, growth, and self-aliasing paths;
- `FlatHashMap` lookup, insertion, removal, growth, and rehash;
- `Shared` creation, copy, weak lock, and final release.

Apply these implementation constraints:

1. Keep fast paths allocation-free where they are allocation-free today.
2. Use acquire/release ordering for lifecycle publication and ownership. Do not
   default to sequential consistency without a demonstrated requirement.
3. Verify that any atomic lifecycle representation used on a required platform
   is lock-free, or document and benchmark the fallback.
4. Preserve in-place container paths for bitwise-relocatable and genuinely
   nothrow-movable types. Use slower transactional paths only when required by
   the element type's exception behavior.
5. Avoid extra hash and equality calls during `FlatHashMap` relocation by using
   stored hashes and empty-slot placement.
6. Do not introduce a global locked work pool without benchmark evidence. If a
   pool is required, evaluate sharding or thread-local caches before accepting
   a single contended mutex.
7. Keep scheduler success results trivial and allocation-free.
8. Avoid copies at `char8_t`, byte, `char`, and platform API boundaries where a
   safe explicit view can represent the same storage.

The following costs are accepted only when documented and measured:

- transactional `FlatHashMap` rehash may temporarily retain both old and new
  bucket arrays;
- enforcing the `FlatHashMap` load ceiling may grow one insertion earlier and
  must distinguish replacement from insertion before committing growth;
- explicit scheduler rejection checks add bounded work to each submission;
- race-safe task frame and continuation ownership adds atomic lifecycle work at
  await, completion, cancellation, and detachment boundaries;
- structured `WhenAny` completion includes loser cancellation and drain time.

Any material regression outside these deliberate tradeoffs requires either a
more selective implementation path or benchmark-backed justification in the
change description. Performance thresholds should be set after the baseline
is recorded rather than chosen without measurements. Run focused benchmarks at
the boundary of each affected phase; correctness-only edits do not require a
full benchmark sweep.

Record benchmark environments, results, and accepted tradeoffs separately from
this implementation plan. The current hardening measurement record is
[`HardeningPerformance.md`](HardeningPerformance.md).

## Phase 1: Test Infrastructure and Failure Injection

Add reusable test support for all subsequent phases:

- throw-on-Nth construction, copy, move, and assignment payloads;
- live-object, construction, destruction, and deallocation counters;
- failure-injection allocators;
- stateful and move-only allocator fixtures;
- deterministic coroutine barriers and controlled executors;
- frame-lifetime probes that detect leaks and duplicate destruction.

Capture the performance baseline described in `Performance Guardrails` from
the reviewed `c0215e5` tree before accepting allocator, container, execution,
or async changes. Keep performance workloads under `benchmarks/`, separate
from correctness tests. Add focused workloads for any listed hot path that the
existing benchmark targets do not cover.

Keep behavioral coverage in the existing focused test files. Shared fixtures
may live in a test-support header, but should not become production API.

### Acceptance criteria

- Tests can deterministically fail each construction and allocation step.
- Fixtures detect leaked or untracked live objects after an exception.
- Async tests can control the order of completion, continuation publication,
  cancellation, and detachment without relying only on timing.

## Phase 2: Allocator Contract and Ownership

Primary files:

- `include/NGIN/Memory/AllocatorConcept.hpp`
- `include/NGIN/Memory/SystemAllocator.hpp`
- `include/NGIN/Memory/FallbackAllocator.hpp`
- `include/NGIN/Memory/LinearAllocator.hpp`
- `include/NGIN/Memory/ThreadSafeAllocator.hpp`

### Work

1. Require `Allocate(size, alignment)` to be `noexcept` in
   `AllocatorConcept`.
2. Make ownership queries return `Ownership` directly instead of adapting a
   Boolean answer.
3. Make `SystemAllocator` report `Ownership::Unknown`.
4. Require a positive ownership result before `FallbackAllocator` routes a
   deallocation. Unknown ownership must never select an allocator.
5. Give `TaggedFallbackAllocator` a strict precondition that deallocated
   pointers originated from that allocator. Do not claim that an arbitrary
   foreign pointer can be safely validated through a preceding header.
6. Centralize checked addition, multiplication, capacity growth, and alignment
   normalization in private memory helpers.
7. Reject alignment normalization and allocation-size overflow without
   wrapping.
8. Remove null-pointer arithmetic from `LinearAllocator`.
9. Make allocator move operations conditionally `noexcept`.
10. Replace the unlocked `ThreadSafeAllocator::InnerAllocator()` escape with a
    locked operation such as `WithInner(callback)`.

### Tests

Extend the focused tests under `tests/Memory/` with:

- system allocator unknown ownership;
- fallback routing with owns, does-not-own, and unknown results;
- maximum size and alignment inputs;
- zero-capacity and failed upstream linear allocators;
- throwing or non-nothrow allocator movement constraints;
- concurrent locked inner-allocator access.

### Acceptance criteria

- No allocator adapter invokes a potentially throwing allocation through a
  `noexcept` function.
- No deallocation is routed from an unknown ownership result.
- Overflow and exhaustion return failure without undefined behavior.

## Phase 3: Transactional Container Lifetimes

Introduce private RAII helpers for:

- ownership of uncommitted raw allocations;
- partially constructed element ranges;
- partially constructed key/value bucket pairs;
- commit and rollback during relocation.

### 3.1 Vector

Primary files:

- `include/NGIN/Containers/Vector.hpp`
- `tests/Containers/Vector.cpp`

Work:

1. Guard initializer-list and copy construction from the first element.
2. Track every live element created during copy and move assignment.
3. Repair indexed insertion when construction, movement, or assignment throws.
4. Stage self-referential arguments before growth so operations such as
   `PushBack(vector[i])` remain valid.
5. Apply the same aliasing analysis to indexed insertion and emplacement.
6. Avoid forming `nullptr + 0` for an empty `end()`.
7. Document the strong or basic exception guarantee for each mutating path.
8. Check growth arithmetic before calculating the next capacity.

Keep the existing in-place path for bitwise-relocatable and nothrow-movable
elements. Do not allocate a replacement buffer for every insertion merely to
obtain a stronger exception guarantee. Self-alias staging should occur only
when growth or overlap would invalidate the source.

Tests must cover copy and initializer-list failure, assignment into existing
capacity, indexed insertion failure, reallocation failure, self-aliasing, and
move-only elements.

### 3.2 FlatHashMap

Primary files:

- `include/NGIN/Containers/FlatHashMap.hpp`
- `tests/Containers/FlatHashMap.cpp`

Work:

1. Publish a bucket as occupied only after its key and value are both live.
2. Build copies and rehashed tables off to the side and commit only after
   successful construction.
3. Relocate nothrow-movable entries during rehash rather than requiring copies.
4. Make copy operations conditionally available for copyable key/value types.
5. Remove incorrect `noexcept` specifications from hash and equality paths.
6. Grow before `(size + 1)` would exceed the configured maximum load factor.
7. Check bucket-count and byte-size arithmetic for overflow.
8. Keep backward-shift deletion constrained to operations that are actually
   non-throwing.

Rehash should use stored hashes and nothrow relocation without repeating
user-supplied hash or equality work. Measure and document the temporary peak
memory caused by retaining the old table until commit.

Tests must cover throwing keys, values, hashers, and equality predicates;
move-only values; failed copy and rehash; load-factor boundaries; and object
lifetime counts after every failure.

### Acceptance criteria

- Failed mutations leave every constructed object tracked and destructible.
- Container destruction after an injected failure is leak-free under ASan.
- Supported move-only types survive growth and rehash.
- The map never advertises a load factor above its configured maximum after an
  insertion completes.

## Phase 4: Smart Pointers and Work-Item Storage

### 4.1 Smart pointers

Primary files:

- `include/NGIN/Memory/SmartPointers.hpp`
- `tests/Memory/SmartPtrTests.cpp`

Work:

1. Hold an allocation guard until the control block and managed object are
   fully constructed.
2. Destroy the control block itself before releasing its allocation.
3. Move a stateful allocator into a safe local deallocation owner before the
   control block is destroyed.
4. Check the combined control-block, alignment-padding, and object size for
   overflow.
5. Apply the same construction protocol to `MakeShared`, `MakeSharedAs`, and
   alias ownership.
6. Make allocator-related exception specifications conditional and accurate.

Tests must cover throwing constructors, derived destruction, weak-only
lifetime, alias ownership, stateful move-only allocators, and exactly-once
control-block deallocation.

### 4.2 Work items

Primary files:

- `include/NGIN/Execution/WorkItem.hpp`
- `tests/Execution/WorkItem.cpp`

Work:

1. Remove the ABA-prone untagged Treiber freelists.
2. Use direct allocation as the initial correctness baseline. Introduce a
   mutex-protected size-class pool only if benchmarks demonstrate that pooling
   remains necessary.
3. Return heap storage when callable placement construction throws.
4. Keep inline storage limited to types whose movement is genuinely
   non-throwing.
5. Define ownership for any retained pool slabs and release them safely.

The existing small-buffer path must remain allocation-free. Benchmark direct
allocation before adding synchronization, and reject a single global pool lock
if contention materially reduces scheduler throughput.

### Acceptance criteria

- Throwing callable construction does not leak.
- Concurrent work-item creation and destruction is clean under TSan.
- Every stored callable is destroyed exactly once.

## Phase 5: Explicit Scheduler Submission Failure

Introduce a scheduling error model:

```cpp
enum class ScheduleError
{
    InvalidExecutor,
    Stopped,
    Rejected,
    ResourceExhausted,
};
```

Use `std::expected<void, ScheduleError>` for immediate and timed submission.

Primary surfaces:

- `Execution::ExecutorRef`;
- `InlineScheduler`;
- `CooperativeScheduler`;
- `ThreadPoolScheduler`;
- `FiberScheduler`;
- asynchronous scheduling call sites.

### Work

1. Replace submission callbacks that promise unconditional success with
   result-returning callbacks.
2. Report stopped and rejected schedulers explicitly.
3. Catch queue or timer-storage allocation failure and return
   `ResourceExhausted`.
4. Ensure a failed coroutine submission does not leave the coroutine marked as
   started and permanently suspended.
5. Translate scheduling failures inside tasks into an `AsyncFault`.
6. Remove `noexcept` from convenience overloads that must first allocate a
   `WorkItem`, or catch and translate that allocation failure.

### Acceptance criteria

- Scheduler shutdown and allocation failure cannot terminate through a false
  `noexcept` promise.
- Rejected coroutine work reaches a terminal fault without leaking its frame.
- Timed and immediate submission share the same failure policy.

## Phase 6: Cancellation Ownership

Primary files:

- `include/NGIN/Async/Cancellation.hpp`
- `tests/Async/Cancellation.cpp`

### Work

1. Replace raw registration pointers with stable, state-owned registration
   nodes.
2. Give each node explicit registered, invoking, unregistered, and completed
   transitions.
3. Make reset/unregister coordinate with an in-flight callback.
4. Ensure moving a registration transfers a node handle rather than relocating
   callback state.
5. Return registration failure when node or collection allocation fails.
6. Keep callback invocation outside the cancellation-state lock.
7. Define and test callback reentrancy, including a callback resetting its own
   registration.

### Tests

- cancellation racing registration reset;
- registration destruction during a cancellation pass;
- moving a registration before and during cancellation;
- repeated cancel calls;
- self-unregistration from a callback;
- allocation failure while registering;
- ASan and TSan stress loops.

### Acceptance criteria

- A cancellation pass never dereferences a destroyed or moved registration.
- Reset has a documented outcome when the callback is already in flight.
- Each armed callback fires at most once.

## Phase 7: Coroutine Lifecycle Redesign

Primary files:

- `include/NGIN/Async/Task.hpp`
- `tests/Async/Task.cpp`
- `tests/Async/ContinueWith.cpp`
- `tests/Async/TaskContext.cpp`

Refactor the duplicated value and value-less task runtime around one
CAS-controlled lifecycle mechanism.

### Required lifecycle properties

1. Awaiting atomically installs its continuation or observes completion and
   declines to suspend.
2. Completion release-publishes the payload before any reader observes the
   completed state.
3. Detachment and completion atomically decide which participant owns frame
   destruction.
4. Exactly one participant can claim destruction.
5. A continuation is resumed at most once.
6. A cancellation-aware parent cannot complete or be destroyed while a child
   still retains a continuation into the parent frame.
7. Result consumption occurs only after an acquire operation observes terminal
   completion.

Use a documented transition table with states representing at least:

- running with an owner;
- continuation installed;
- detached;
- completed with an owner;
- destruction claimed.

The implementation may encode continuation ownership in a tagged atomic word,
but the representation must not depend on undocumented alignment assumptions.
Use the weakest correct acquire/release operations and keep the common
await/completion path to the minimum number of atomic read-modify-write
operations. Check lock-free behavior and false-sharing sensitivity on the
supported platforms.

### Deterministic tests

- continuation publication racing `final_suspend`;
- detach racing completion;
- operation destruction racing completion;
- cancellation racing child completion;
- result payload visibility across threads;
- duplicate await attempts;
- executor shutdown during resume;
- exactly-once frame destruction.

Run repeated ASan/UBSan and TSan passes after the deterministic cases pass.

### Acceptance criteria

- No lost wakeup is possible between `await_ready`, `await_suspend`, and
  completion.
- No detached or owned frame can be destroyed twice or leaked through a race.
- Parent completion cannot leave a child continuation pointing to a dead frame.

## Phase 8: Structured `WhenAny`

Primary files:

- `include/NGIN/Async/WhenAny.hpp`
- `tests/Async/When.cpp`
- `docs/Async.md`

Change `WhenAny` to use structured loser cleanup:

1. spawn children with child-specific cancellation state linked to the parent;
2. select and record the first terminal child;
3. request cancellation of every losing child;
4. drain every losing operation;
5. return the winner only after no child can still reference the parent scope.

This intentionally changes observable latency: completion becomes winner
selection plus loser cancellation and drain. Document that child operations
must respond cooperatively to cancellation, and benchmark both responsive and
slow-loser cases. Do not add an implicit timeout that could reintroduce unsafe
detachment.

Remove the current requirement that callers keep parent-frame state alive for
detached losers. If an explicitly detached race primitive remains desirable,
it should be a separately named API with explicit lifetime preconditions.

### Acceptance criteria

- A local loser may reference parent-local state without use-after-free.
- Winner failure and cancellation are propagated consistently.
- Loser faults are observed according to a documented policy.
- No watcher or child operation survives the combinator scope.

## Phase 9: API Consolidation

Land each item as a separate focused change after the correctness foundation is
stable.

### Expected and Optional

1. Replace the custom `Expected` implementation with `std::expected`.
2. Migrate production call sites from custom accessors such as `TakeValue()`
   and `TakeError()` to standard operations.
3. Reduce `Utilities/Expected.hpp` to aliases if the NGIN names remain useful,
   or remove it entirely.
4. Replace the custom `Optional` with `std::optional` and remove the unused
   implementation.
5. Add a configure-time check for the required C++23 standard-library support.

### Overlapping APIs

1. Remove the synchronous `AsyncResult` alias.
2. Rename `ConcurrentHashMap::SnapshotForEach` to communicate weak consistency.
3. Consolidate on `FileHandle` and the filesystem interfaces, then retire the
   legacy `IO::File` implementation after an explicit deletion review.
4. Use `char8_t` for `UTF8String`, with explicit conversion at byte and
   `std::string` boundaries.
5. Remove or replace encoding aliases that enforce no distinct contract.
6. Capture exception stack traces when an exception is constructed rather than
   when `GetStacktrace()` is first called.

Eager stack-trace capture can allocate and substantially increase exception
construction cost. Make capture configurable, or limit it to diagnostic builds
and exception categories that require origin fidelity. UTF-8 boundary helpers
should offer zero-copy views when representation and lifetime make that safe.

### Acceptance criteria

- Foundational result and optional behavior comes from the C++23 standard
  library.
- Each retained public name represents a distinct semantic contract.
- The IO documentation identifies one primary file API.

## Phase 10: Functional Packaging and Continuous Gates

Keep the current component DAG:

```text
Foundation
   `--> Execution --> IO --> Serialization --> Crypto --.
                       `--> Net ------------------------> NetTLS
```

### Packaging work

1. Install only the headers owned by the enabled component closure.
2. Make OpenSSL discovery depend on the components requested by the consumer.
3. Emit the `NGIN::Base` aggregate only when the complete aggregate is present.
   It must not silently change meaning in a subset package.
4. Continue exposing explicit component targets for subset consumers.
5. Add installed-package consumer tests for Foundation, Execution, Net,
   NetTLS, and the complete aggregate.

### Standalone correctness gates

Add standalone `NGIN.Base` CI for:

- Linux with GCC;
- Linux with Clang;
- Windows with MSVC;
- macOS with AppleClang;
- ASan/UBSan;
- TSan on a supported Linux toolchain;
- component-selective install and external-consumer tests.

These are correctness and integration gates. ABI snapshots, visibility audits,
and release-specific jobs remain deferred.

## Landing Order

Use small changes that remain independently testable:

1. Test fixtures and the common failure policy.
2. Allocator contract and ownership.
3. `Vector` lifetime and aliasing.
4. `FlatHashMap` lifetime, rehash, and load policy.
5. Smart-pointer control blocks.
6. `WorkItem` storage.
7. Scheduler submission results.
8. Cancellation registrations.
9. Task lifecycle.
10. Structured `WhenAny`.
11. `Expected` and `Optional` consolidation.
12. Remaining API consolidation.
13. Component packaging and standalone CI.

Do not combine allocator, container, and coroutine rewrites into one change.
Each layer should establish the contracts needed by the next layer.

## Verification Strategy

For each change:

1. build only the affected focused test target;
2. run its discovered Catch2 tests;
3. run ASan/UBSan for lifetime-sensitive changes;
4. run TSan for cancellation, task, work-pool, and scheduler changes;
5. run broader standalone tests only at phase boundaries.

At the boundary of a phase that affects a listed hot path, compare its focused
optimized benchmarks with the `c0215e5` baseline. Record the compiler, build
configuration, workload parameters, repetitions, and result distribution with
the comparison. Run the complete benchmark set only at final hardening
verification.

Final hardening verification should include:

```bash
cmake --preset tests
cmake --build --preset tests-debug
ctest --test-dir build/tests -C Debug --output-on-failure

cmake --preset tests-asan
cmake --build --preset tests-asan-build
ctest --test-dir build/tests-asan -C RelWithDebInfo --output-on-failure

cmake --preset tests-tsan
cmake --build --preset tests-tsan-build
ctest --test-dir build/tests-tsan -C RelWithDebInfo --output-on-failure
```

Also run the installed component-consumer matrix after packaging changes.

## Completion Criteria

The hardening program is complete when:

- deterministic tests cover the identified coroutine and cancellation races;
- ASan/UBSan reports no leaks, use-after-free, or lifetime errors in affected
  areas;
- TSan reports no races in task, cancellation, scheduler, or work-item tests;
- injected construction and allocation failures leave containers and smart
  pointers valid and leak-free;
- allocator ownership is never guessed;
- scheduler failure cannot escape through false `noexcept` contracts;
- `WhenAny` owns and drains all child work;
- the custom `Expected` and `Optional` maintenance burden is removed;
- component-selective installs expose only their intended headers and
  dependencies;
- benchmark results account for all affected hot paths, and any material
  regression outside the accepted tradeoffs documented in `Performance
  Guardrails` has an explicit, benchmark-backed justification.

These criteria intentionally make no claim about ABI stability or release
readiness.
