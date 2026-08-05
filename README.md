# NGIN.Base

`NGIN.Base` is the foundational C++23 library used across NGIN. It provides
low-level building blocks that can also be adopted independently.

> [!WARNING]
> NGIN.Base is experimental. Prefer the documented central APIs and avoid
> depending on headers under implementation-detail namespaces.

## Main areas

| Area | Start here |
| --- | --- |
| Foundation and utilities | [Foundation](docs/Foundation.md) |
| Async tasks and cancellation | [Async](docs/Async.md) |
| Threads, fibers, and schedulers | [Execution](docs/Execution.md) |
| Synchronization | [Sync](docs/Sync.md) |
| SIMD | [SIMD](docs/SIMD.md) |
| Paths and files | [I/O](docs/IO.md) |
| Sockets and async networking | [Network](docs/Network.md) |
| TLS streams | [Network TLS](docs/Network.md#tls-streams) |
| Allocators | [Memory](docs/Memory.md) |
| Containers | [Containers](docs/Containers.md) |
| JSON and XML | [Serialization](include/NGIN/Serialization/README.md) |
| Cryptography and secure tokens | [Crypto](docs/Crypto.md) |
| Text | [Text](docs/Text.md) |
| Math and units | [Math and units](docs/MathAndUnits.md) |

The design favors explicit runtimes, typed results, deterministic ownership,
and allocator control. There is no hidden global scheduler or required
application framework.

## Small async example

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

NGIN::Async::Task<int> Compute(NGIN::Async::TaskContext& context) {
    co_await context.YieldNow();
    co_return 7;
}
```

Tasks are cold until they are spawned, detached, or synchronously awaited. See
the async guide for completion, error, and cancellation handling.

## CMake targets

- `NGIN::Base::<Component>::Static`
- `NGIN::Base::<Component>::Shared`
- `NGIN::Base::<Component>`, which selects the available preferred form
- `NGIN::Base::Static`
- `NGIN::Base::Shared`
- `NGIN::Base`, the aggregate convenience target

`<Component>` is one of `Foundation`, `Execution`, `IO`, `Serialization`,
`Crypto`, `Net`, or `NetTLS`. Prefer the narrowest component that owns the APIs a target
uses; the aggregate remains available for applications that need several areas.

Source builds may select a component and its transitive dependencies with, for
example, `-DNGIN_BASE_BUILD_COMPONENTS=Net`. The default value, `all`, builds
all seven components. Tests, examples, benchmarks, and fuzzers intentionally
enable the full graph.

## Build and test

```bash
cmake -S . -B build \
  -DNGIN_BASE_BUILD_TESTS=ON \
  -DNGIN_BASE_BUILD_EXAMPLES=ON \
  -DNGIN_BASE_BUILD_BENCHMARKS=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

See the [documentation index](docs/README.md) and
[public header style](docs/CodeStyle.md), and the [contribution guide](AGENTS.md).
