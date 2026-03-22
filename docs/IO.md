# IO

`NGIN::IO` is the filesystem and low-level file access layer in `NGIN.Base`.

It provides:

- lexical path handling through `NGIN::IO::Path`
- synchronous filesystem access through `NGIN::IO::IFileSystem`
- async filesystem access through `NGIN::IO::IAsyncFileSystem`
- local/native filesystem access through `NGIN::IO::LocalFileSystem`
- mount-based composition through `NGIN::IO::VirtualFileSystem`
- convenience helpers such as `ReadAllText`, `WriteAllText`, and `EnsureDirectory`

This document is the practical API overview. The broader design and migration roadmap lives in
[`FileSystemPlan.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/FileSystemPlan.md).

## Mental Model

There are two different layers in `NGIN::IO`:

- lexical path manipulation
  - string-level path operations that do not touch the real filesystem
- physical filesystem operations
  - metadata queries, file open, directory creation, copy, remove, enumeration, and related work

`Path` belongs to the lexical layer.
`IFileSystem` and its implementations belong to the physical layer.

Do not assume that a lexical path operation tells you whether a path exists or whether it is a file or a directory.
That comes from filesystem metadata.

## Key Types

### `Path`

`NGIN::IO::Path` is a lightweight lexical path value type.

Main operations:

- `Normalize()`
- `LexicallyNormal()`
- `LexicallyRelativeTo(base)`
- `Filename()`
- `Stem()`
- `Extension()`
- `Parent()`
- `Join(...)`
- `Append(...)`
- `ReplaceExtension(...)`
- `RemoveFilename()`
- `StartsWith(...)`
- `EndsWith(...)`
- `FromNative(...)`
- `ToNative()`

Important current behavior:

- The public separator is `/`.
- Input may contain `/` or `\\`; normalization canonicalizes separators.
- Trailing separators are treated as lexical noise for decomposition.
  - `"a/b/c"` and `"a/b/c/"` both report filename `"c"`.
- `Path` is lexical only.
  - It does not resolve symlinks.
  - It does not check existence.
  - It does not know whether a path is actually a directory.

### `IFileSystem`

`IFileSystem` is the synchronous filesystem contract.

Main categories of operations:

- metadata
  - `Exists`
  - `GetInfo`
  - `SameFile`
- path resolution
  - `Absolute`
  - `Canonical`
  - `WeaklyCanonical`
  - `ReadSymlink`
- mutation
  - `CreateDirectory`
  - `CreateDirectories`
  - `CreateSymlink`
  - `CreateHardLink`
  - `SetPermissions`
  - `RemoveFile`
  - `RemoveDirectory`
  - `RemoveAll`
  - `Rename`
  - `ReplaceFile`
  - `CopyFile`
  - `Move`
- open and enumeration
  - `OpenFile`
  - `OpenDirectory`
  - `OpenFileView`
  - `Enumerate`
- process/temp/space helpers
  - `CurrentWorkingDirectory`
  - `SetCurrentWorkingDirectory`
  - `TempDirectory`
  - `CreateTempDirectory`
  - `CreateTempFile`
  - `GetSpaceInfo`

### `FileHandle`

`FileHandle` is a move-only RAII wrapper returned by `OpenFile`.

It exposes:

- `Read`
- `Write`
- `ReadAt`
- `WriteAt`
- `Flush`
- `Seek`
- `Tell`
- `Size`
- `SetSize`
- `Close`
- `IsOpen`

The public API no longer exposes `Expected<std::unique_ptr<...>>` for synchronous file handles.

### `DirectoryHandle`

`DirectoryHandle` is a move-only RAII wrapper returned by `OpenDirectory`.

It allows relative operations scoped to an opened directory:

- `Exists`
- `GetInfo`
- `OpenFile`
- `OpenDirectory`
- `CreateDirectory`
- `RemoveFile`
- `RemoveDirectory`
- `ReadSymlink`

This is the preferred API when code should operate relative to a directory rather than constantly joining strings.

### `DirectoryEnumerator`

`DirectoryEnumerator` is a move-only RAII wrapper returned by `Enumerate`.

It exposes:

- `Next()`
- `Current()`

The enumeration model is explicit pull-based iteration.

## Implementations

### `LocalFileSystem`

`LocalFileSystem` is the native local-disk backend.

Current direction:

- uses native OS facilities rather than `std::filesystem` in the production backend
- supports Linux/POSIX and Windows backends with separate platform implementation files
- exposes capability reporting through `GetCapabilities()`

On POSIX-like systems, current metadata coverage includes:

- regular files
- directories
- symlinks
- hard-link count
- FIFOs
- Unix domain sockets
- ownership
- POSIX mode bits
- inode/device identity

### `VirtualFileSystem`

`VirtualFileSystem` composes one or more mounts under virtual prefixes.

Typical uses:

- overlaying multiple roots
- tests and controlled mount layouts
- routing code through a filesystem abstraction instead of the process filesystem directly

Current features:

- virtual prefix resolution
- forwarding of most core filesystem operations
- directory handles scoped to virtual paths

Important note:

- `VirtualFileSystem` is useful now, but it is still evolving.
- Read the filesystem plan before depending on advanced mount semantics.

## Metadata

`GetInfo` returns `FileInfo`.

Depending on backend and capability support, it may include:

- entry type
- size
- permissions
- created / modified / accessed / changed timestamps
- ownership
- file identity
- hard-link count
- symlink target existence information

Use `MetadataOptions` when you need explicit symlink-follow behavior.

Example:

```cpp
NGIN::IO::MetadataOptions options;
options.symlinkMode = NGIN::IO::SymlinkMode::DoNotFollow;

auto info = fs.GetInfo(path, options);
if (!info)
{
    return;
}

if (info->type == NGIN::IO::EntryType::Symlink)
{
    // inspect the link itself, not the target
}
```

## Convenience Helpers

`NGIN::IO::FileSystemUtilities` contains common helpers:

- `ReadAllBytes`
- `ReadAllText`
- `WriteAllBytes`
- `WriteAllText`
- `AppendAllText`
- `EnsureDirectory`
- async variants for common read/write flows

These helpers are useful when you do not need fine-grained control over handle lifetime or partial IO.

## Basic Examples

### Read a text file

```cpp
NGIN::IO::LocalFileSystem fs;

auto text = NGIN::IO::ReadAllText(fs, NGIN::IO::Path {"config.json"});
if (!text)
{
    return;
}

// use text.Value()
```

### Open and write through a file handle

```cpp
NGIN::IO::LocalFileSystem fs;

NGIN::IO::FileOpenOptions options;
options.access = NGIN::IO::FileAccess::Write;
options.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

auto file = fs.OpenFile(NGIN::IO::Path {"out.txt"}, options);
if (!file)
{
    return;
}

const std::string_view payload = "hello";
file->Write({
    reinterpret_cast<const NGIN::Byte*>(payload.data()),
    payload.size()
});
file->Flush();
```

### Enumerate a directory

```cpp
NGIN::IO::LocalFileSystem fs;

NGIN::IO::EnumerateOptions options;
options.populateInfo = true;

auto entries = fs.Enumerate(NGIN::IO::Path {"assets"}, options);
if (!entries)
{
    return;
}

while (true)
{
    auto next = entries->Next();
    if (!next || !next.Value())
    {
        break;
    }

    const auto& entry = entries->Current();
    // use entry.name / entry.type / entry.info
}
```

### Use a directory handle

```cpp
NGIN::IO::LocalFileSystem fs;

auto dir = fs.OpenDirectory(NGIN::IO::Path {"workspace"});
if (!dir)
{
    return;
}

auto file = dir->OpenFile(NGIN::IO::Path {"manifest.json"}, {
    .access = NGIN::IO::FileAccess::Read,
    .disposition = NGIN::IO::FileCreateDisposition::OpenExisting
});

if (!file)
{
    return;
}
```

## Error Model

Most synchronous filesystem operations return:

- `NGIN::IO::Result<T>`
- `NGIN::IO::ResultVoid`

These are `Expected<..., IOError>` aliases.

`IOError` carries:

- stable `IOErrorCode`
- native system error code when available
- primary and secondary path context
- message text

This means callers can branch on stable library-level errors without losing native error data.

## Async

The async surface exists through `IAsyncFileSystem` and `IAsyncFileHandle`.

Async filesystem APIs now follow the same typed async model as the rest of `NGIN.Base`:

- `Task<T, IOError>` for domain IO failures
- canceled as a distinct async completion state
- fault as a distinct async runtime completion state

If your code only needs synchronous filesystem access, prefer `IFileSystem`, `FileHandle`, `DirectoryHandle`, and the
utility helpers.

## Basic Async Start

The easiest way to get started is:

1. use `LocalFileSystem` as an `IAsyncFileSystem`
2. create a `TaskContext` on an executor
3. use the async convenience helpers from `FileSystemUtilities`
4. schedule the root task and run the executor

### Smallest useful example

This is the recommended starting point because it avoids dealing with async file handles directly:

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

NGIN::Async::Task<void, NGIN::IO::IOError> LoadConfig(
        NGIN::Async::TaskContext& ctx,
        NGIN::IO::IAsyncFileSystem& fs)
{
    auto bytes = co_await NGIN::IO::ReadAllBytesAsync(
            fs,
            ctx,
            NGIN::IO::Path {"config.json"});

    // use bytes
    co_return;
}

int main()
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext ctx(scheduler);
    NGIN::IO::LocalFileSystem fs;

    auto task = LoadConfig(ctx, fs);
    task.Start(ctx);
    scheduler.RunUntilIdle();

    auto result = task.Get();
    if (!result)
    {
        return 1;
    }

    return 0;
}
```

### What is happening here

- `LoadConfig(...)` is a coroutine returning `Task<void, IOError>`
- `ReadAllBytesAsync(...)` performs the file open/read flow asynchronously
- `TaskContext` carries the executor and cancellation token
- `task.Start(ctx)` schedules the root task
- `scheduler.RunUntilIdle()` pumps the cooperative executor until work is finished
- `task.Get()` returns `TaskOutcome<void, IOError>`

### Async IO error model

Async filesystem APIs no longer require double-unwrapping nested async and domain result layers.

Use this mental model:

- ordinary filesystem failures are `IOError`
- cancellation is `TaskOutcome::IsCanceled()`
- runtime failures are `TaskOutcome::IsFault()`

Inside a coroutine, `co_await Task<T, IOError>` yields `T` on success and propagates non-success automatically into the
awaiting task.

### Direct handle example

If you do need lower-level control, use `OpenFileAsync` and then call methods on the returned `IAsyncFileHandle`:

```cpp
NGIN::Async::Task<void, NGIN::IO::IOError> WriteLog(
        NGIN::Async::TaskContext& ctx,
        NGIN::IO::IAsyncFileSystem& fs)
{
    NGIN::IO::FileOpenOptions options;
    options.access = NGIN::IO::FileAccess::Write;
    options.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto file = co_await fs.OpenFileAsync(
            ctx,
            NGIN::IO::Path {"app.log"},
            options);

    constexpr std::string_view payload = "hello\n";
    co_await file->WriteAsync(
            ctx,
            {
                reinterpret_cast<const NGIN::Byte*>(payload.data()),
                payload.size()
            });
    co_await file->FlushAsync(ctx);

    co_return;
}
```

### Current recommendation

Use this rule of thumb:

- for basic async file reads and writes
  - start with `ReadAllBytesAsync`, `WriteAllBytesAsync`, and `CopyFileAsync`
- for specialized IO flows
  - use `OpenFileAsync` and `IAsyncFileHandle`
- for most other filesystem work
  - prefer the synchronous API for now unless async is clearly needed

If your code depends heavily on async filesystem semantics, read:

- [`Async.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/Async.md)
- [`FileSystemPlan.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/FileSystemPlan.md)

## Current Status

`NGIN::IO` is already useful and increasingly test-covered, but it is still an evolving subsystem.

What is already solid enough to rely on:

- lexical `Path` operations
- local filesystem read/write/copy/remove flows
- metadata queries
- directory handles
- virtual filesystem basics
- utility helpers

What still needs continued evolution:

- async handle ergonomics
- broader Windows validation
- more virtual filesystem policy coverage
- more complete API docs for backend-specific behavior
- continued test growth, especially around edge cases and failure modes

If you are extending the filesystem subsystem itself, start with:

- this document for the practical overview
- [`FileSystemPlan.md`](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/FileSystemPlan.md) for design direction
