# Synchronization

`NGIN::Sync` provides small C++ Lockable-compatible wrappers and low-level
coordination primitives. Use the standard method names with generic standard
algorithms and the PascalCase names in NGIN-style code.

## Choosing a primitive

| Need | Type |
| --- | --- |
| General exclusive lock | `Mutex` |
| Recursive exclusive lock | `RecursiveMutex` |
| Multiple readers or one writer | `SharedMutex` or `ReadWriteLock` |
| A bounded permit count | `Semaphore<MaxCount>` |
| Very short non-blocking critical section | `SpinLock` |
| FIFO-ordered spinning | `TicketLock` |
| Generation-based wake-up without a predicate mutex | `AtomicCondition` |

Prefer `Mutex` unless measurement or a concrete ownership requirement supports
another primitive. Spinning wastes CPU while waiting and is appropriate only
for short, bounded critical sections.

## Lockable compatibility

`Mutex`, `RecursiveMutex`, `SharedMutex`, `ReadWriteLock`, `Semaphore`,
`SpinLock`, and `TicketLock` expose `lock`, `unlock`, and, where applicable,
`try_lock`. Shared mutex types also expose `lock_shared`, `unlock_shared`, and
`try_lock_shared`.

```cpp
#include <NGIN/Sync/Mutex.hpp>

#include <mutex>

NGIN::Sync::Mutex mutex;

void Update()
{
    std::scoped_lock guard {mutex};
    // Protected work.
}
```

The PascalCase `TryLock`, `TryStartRead`, and `TryStartWrite` methods return
`true` only when the caller acquired the lock. A successful try operation must
be paired with the corresponding unlock operation.

## Guards

`LockGuard` and `SharedLockGuard` acquire in their constructors and release in
their destructors. Moving a guard transfers the sole release responsibility.

```cpp
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/SharedMutex.hpp>

NGIN::Sync::SharedMutex mutex;

void Read()
{
    NGIN::Sync::SharedLockGuard guard {mutex};
    // Shared read access.
}
```

Do not destroy a lock while a guard or waiting thread can still refer to it.

## AtomicCondition

`AtomicCondition` maintains a generation counter. Capture the generation before
testing the state that may require a wait, then pass that generation to `Wait`
or `WaitFor`. This prevents a notification between the state check and the wait
from being lost.

```cpp
#include <NGIN/Sync/AtomicCondition.hpp>

auto observed = condition.Load();
while (!WorkAvailable())
{
    condition.Wait(observed);
    observed = condition.Load();
}
```

`NotifyOne` and `NotifyAll` increment the generation before waking waiters.
Timed waits return `false` for timeout, non-positive durations, and NaN
durations. They return `true` after the observed generation changes.

`AtomicCondition` does not own a predicate or protect shared data. The caller
must publish predicate state with suitable synchronization before notifying and
must re-check that predicate after waking.

## Testing guidance

- Coordinate tests with observed generations or explicit latches instead of
  assuming that a sleeping thread has started waiting.
- Test failed try-lock operations from a different thread when the primitive is
  not recursive.
- Keep contention tests bounded.
- Run concurrency changes under ThreadSanitizer on a supported toolchain.
