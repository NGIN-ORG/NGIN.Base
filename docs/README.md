# NGIN.Base Docs

This is the user-facing documentation entry point for `NGIN.Base`.

If you are new to the library, start with the practical guides first. Use the `*Plan.md` files later if you want
design history, roadmap context, or contributor-facing notes.

## Start Here

Choose the path that matches what you need today.

### I need async tasks or coroutine support

Read [Async.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Async.md).

Use it for:

- `Task<T, E>`
- `TaskContext`
- cancellation
- `WhenAll` / `WhenAny`
- `AsyncGenerator`

### I need paths, files, or directories

Read [IO.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/IO.md).

Use it for:

- `Path`
- `LocalFileSystem`
- `FileHandle`
- `DirectoryHandle`
- whole-file utility helpers

### I need sockets or explicit async networking

Read [Network.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Network.md).

Use it for:

- `TcpSocket`
- `TcpListener`
- `UdpSocket`
- `NetworkDriver`
- transport adapters such as `TcpByteStream`

### I need allocators, memory utilities, or allocator-aware containers

Read [Memory.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Memory.md).

Use it for:

- `SystemAllocator`
- `LinearAllocator`
- tracking and thread-safe allocator wrappers
- allocator-aware containers

## Which Docs Are For Users?

Start with these:

- [Async.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Async.md)
- [IO.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/IO.md)
- [Network.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Network.md)
- [Memory.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Memory.md)

These are practical guides. They explain when to use each subsystem, the normal workflow, and the main types you
actually need.

## Which Docs Are Design Notes?

These are for contributors or readers who want implementation direction and roadmap context:

- [AsyncPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/AsyncPlan.md)
- [FileSystemPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/FileSystemPlan.md)
- [NetworkPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/NetworkPlan.md)
- [SerializationPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/SerializationPlan.md)

They are useful, but they are not the best place to start if your goal is “make progress quickly.”

## Stability Snapshot

- Stable and central:
  - `Task<T, E>` basics
  - sync filesystem APIs
  - `Path`
  - allocator utilities and containers
- Usable and maturing:
  - async filesystem surface
  - advanced VFS behavior
  - networking higher-level guidance and examples
- Design-note material:
  - the `*Plan.md` documents

## Recommended Reading Order

For a first pass:

1. [../README.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/README.md)
2. the subsystem guide you need right now
3. the corresponding plan doc only if you are extending the subsystem itself
