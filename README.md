# NGIN.Base

`NGIN.Base` is a modern C++23 foundation library for systems-level code.

Use it when you want explicit behavior in core parts of your program: tasks and cancellation, files and paths,
networking, allocators, containers, text, hashing, serialization, and other building blocks that are normally spread
across the STL, platform APIs, and project-specific infrastructure.

It is the foundation layer of the broader NGIN stack, but it is also intended to be useful on its own.

## Who It Is For

`NGIN.Base` is a good fit for:

- library authors building reusable C++ infrastructure
- engine, runtime, tooling, and platform code
- applications that care about explicit control over async, IO, and memory
- cross-platform code that wants a consistent core library instead of ad hoc wrappers

It is especially useful when you prefer:

- typed async errors over exception-driven async control flow
- explicit runtimes instead of hidden worker threads
- low-level filesystem and networking APIs with practical wrappers on top
- modular subsystems that can be adopted one piece at a time

## Who It Is Not For

You probably do not need `NGIN.Base` if:

- the STL already covers your needs comfortably
- you want a batteries-included application framework
- your project is a tiny one-off tool with no real infrastructure needs
- you want a single high-level networking or async framework to dictate the whole application shape

## What Makes It Different

`NGIN.Base` is biased toward explicit systems code.

Compared with a typical STL-only stack, it gives you:

- typed coroutine tasks with domain errors, cancellation, and fault separation
- filesystem APIs that separate lexical paths from real filesystem access
- low-level non-blocking sockets plus an explicit async driver
- allocator-aware containers and memory utilities that do not depend on the standard allocator model

Compared with larger framework-style libraries, it stays closer to the metal:

- no hidden global runtime
- no “just throw and recover later” async model
- no attempt to hide platform realities behind magic

## Start Here

If you are new to the library, use this path:

1. [Documentation Index](docs/README.md)
2. [Async Guide](docs/Async.md) if you need coroutine-based async work
3. [IO Guide](docs/IO.md) if you need files, paths, or directory operations
4. [Network Guide](docs/Network.md) if you need sockets or explicit async networking
5. [Memory Guide](docs/Memory.md) if you need allocator-aware containers and memory utilities

## Smallest Useful Example

This example shows the basic async style used across the library.

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

    auto task = Compute(ctx);
    task.Start(ctx);
    scheduler.RunUntilIdle();

    auto result = task.Get();
    if (!result)
    {
        return 1;
    }

    return result.Value();
}
```

What to notice:

- tasks are cold until you start or schedule them
- inside a coroutine, write normal success-path code
- at the root, inspect the finished task with `Get()`

## Main Subsystems

These are the parts most users adopt first.

### Async

- `Task<T, E>`, `TaskContext`, `WhenAll`, `WhenAny`, `AsyncGenerator`
- best for coroutine-based code with typed domain errors and explicit cancellation
- start here: [docs/Async.md](docs/Async.md)

### IO

- `Path`, `IFileSystem`, `LocalFileSystem`, `DirectoryHandle`, `FileHandle`, utility helpers
- best for path handling, file reads/writes, metadata, and directory-scoped operations
- start here: [docs/IO.md](docs/IO.md)

### Network

- `TcpSocket`, `TcpListener`, `UdpSocket`, `NetworkDriver`, transport adapters
- best for non-blocking sockets and explicit coroutine-driven networking
- start here: [docs/Network.md](docs/Network.md)

### Memory and Containers

- allocator utilities, tracking/thread-safe wrappers, arenas, vectors, strings, hash maps
- best for performance-sensitive infrastructure and projects that want allocator control
- start here: [docs/Memory.md](docs/Memory.md)

## Stability Snapshot

The library is broad, so it helps to know where to start.

- Stable and central:
  - core async task model
  - sync filesystem APIs
  - `Path`
  - containers and allocator utilities
- Usable and maturing:
  - async filesystem surface
  - virtual filesystem advanced semantics
  - networking higher-level adapters and usage guidance
- Design-note / contributor material:
  - `docs/*Plan.md`

For user-facing work, start with the non-`Plan` docs first.

## Build Targets

`NGIN.Base` can be consumed in multiple forms:

- `NGIN::Base::Static` with `-DNGIN_BASE_BUILD_STATIC=ON`
- `NGIN::Base::Shared` with `-DNGIN_BASE_BUILD_SHARED=ON`

The convenience alias `NGIN::Base` resolves to the shared target when available, otherwise the static target.

## Platform Source Naming

Platform-specific implementation files follow one default pattern:

- `<BaseName>.<platform>.cpp`

Use these platform tokens:

- `win32`
- `linux`
- `macos`
- `posix`

Additional rules:

- Use `posix` only when one implementation is intentionally shared across multiple Unix-like targets.
- Use `<BaseName>.<arch>.cpp` for architecture-specific helpers such as `x86_64` and `aarch64`.
- Add another suffix only when that extra axis is a first-class build or source-layout concept. Format: `<BaseName>.<platform>.<variant>.cpp`.
- Do not encode hidden implementation details in the filename. For example, prefer `NetworkDriver.win32.cpp` over `NetworkDriver.win32.iocp.cpp`.

## Build Options

Main CMake options:

- `NGIN_BASE_BUILD_STATIC` default `ON`
- `NGIN_BASE_BUILD_SHARED` default `OFF`
- `NGIN_BASE_BUILD_TESTS` default `ON`
- `NGIN_BASE_BUILD_EXAMPLES` default `ON`
- `NGIN_BASE_BUILD_BENCHMARKS` default `ON`
- `NGIN_BASE_ENABLE_ASAN` default `OFF`
- `NGIN_BASE_ENABLE_TSAN` default `OFF`
- `NGIN_BASE_ENABLE_LTO` default `OFF`
- `NGIN_BASE_STRICT_WARNINGS` default `ON`

## Typical Local Build

```bash
cmake -S . -B build \
  -DNGIN_BASE_BUILD_TESTS=ON \
  -DNGIN_BASE_BUILD_EXAMPLES=ON \
  -DNGIN_BASE_BUILD_BENCHMARKS=OFF

cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Relationship To The Broader NGIN Stack

Inside the broader NGIN platform, `NGIN.Base` is the library other components assume exists.

In practice:

- `NGIN.Reflection` builds on it
- `NGIN.Core` builds on it
- the native `ngin` CLI uses it for XML parsing and low-level infrastructure

That makes it foundational inside the stack, but it does not require you to adopt the rest of the NGIN platform.

## Read Next

- [Documentation Index](docs/README.md)
- [Contribution Guide](AGENTS.md)
