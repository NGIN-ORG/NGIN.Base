# IO

`NGIN::IO` is the filesystem and file-access layer in `NGIN.Base`.

Use it when you want:

- a lexical path type that is separate from real filesystem access
- sync file and directory operations through a consistent interface
- scoped directory-relative operations
- utility helpers for whole-file reads and writes
- async filesystem APIs in code that already uses the async runtime

Most users should start with:

- `Path` for lexical path manipulation
- `LocalFileSystem` for sync filesystem access
- `FileSystemUtilities` for whole-file helpers such as `ReadAllText`

## When To Use It

Use `NGIN::IO` when:

- you want the same path/file abstraction across your own code
- you want filesystem work behind an interface instead of raw platform calls
- you need directory-scoped operations through `DirectoryHandle`
- you already use `TaskContext` and need async file work

## When Not To Use It

You probably do not need it when:

- you only need a couple of trivial path joins and the STL already solves the problem
- you do not need an abstraction layer for filesystem work
- your code does not use the async runtime and async file access would add complexity without benefit

## Stability

- Stable and central:
  - `Path`
  - sync `IFileSystem` / `LocalFileSystem`
  - `FileHandle`
  - `DirectoryHandle`
  - whole-file utility helpers
- Usable and maturing:
  - async filesystem surface
  - advanced `VirtualFileSystem` semantics
  - deeper Windows validation and edge-case coverage

## Which API Should I Use?

- Need to manipulate a path string only:
  - use `Path`
- Need to read or write an entire file:
  - use `ReadAllText`, `WriteAllText`, `ReadAllBytes`, or `WriteAllBytes`
- Need general synchronous filesystem operations:
  - use `IFileSystem` or `LocalFileSystem`
- Need operations relative to an open directory:
  - use `DirectoryHandle`
- Need fine-grained file IO:
  - use `FileHandle`
- Need async filesystem operations:
  - use `IAsyncFileSystem` together with `FileSystemDriver` or another suitable executor-backed `TaskContext`
  - use `AsyncDirectoryHandle` when async code needs directory-relative operations

## Sync-First Recommendation

Most code should start with the synchronous APIs.

Use async filesystem APIs only when:

- your surrounding code already uses `TaskContext` and an executor
- avoiding blocking is materially important to the design

If you want a straightforward worker-backed setup, start with `FileSystemDriver`.

Current async local-file execution is driver-backed: filesystem work is dispatched onto the `FileSystemDriver`
backend and resumes your task on the `TaskContext` executor when the operation completes.

For straightforward tools and ordinary application startup code, the sync APIs are usually the better first choice.

## Smallest Useful Examples

### Read a text file

```cpp
NGIN::IO::LocalFileSystem fs;

auto text = NGIN::IO::ReadAllText(fs, NGIN::IO::Path {"config.json"});
if (!text)
{
    return;
}

Use(text.Value());
```

### Write a text file

```cpp
NGIN::IO::LocalFileSystem fs;

auto wrote = NGIN::IO::WriteAllText(
    fs,
    NGIN::IO::Path {"output.txt"},
    "hello\n");

if (!wrote)
{
    return;
}
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
    Use(entry.name);
}
```

## Common Workflows

### Use `Path` for lexical work

`Path` only manipulates path strings.

Use it for:

- normalization
- extracting filename, extension, and parent
- joining or appending path components
- lexical relative-path computation

Do not use it to answer:

- does this path exist?
- is it a file or a directory?
- where does this symlink resolve?

Those are filesystem questions, not lexical path questions.

### Work through `IFileSystem` or `LocalFileSystem`

Use the filesystem object for:

- metadata queries
- file open
- directory creation/removal
- copy, move, rename
- canonicalization
- symlink-aware operations

`LocalFileSystem` is the concrete local-disk backend most users should start with.

### Use `DirectoryHandle` for relative operations

If your code should stay rooted inside a directory, prefer `DirectoryHandle` over repeated string joins.

```cpp
NGIN::IO::LocalFileSystem fs;

auto dir = fs.OpenDirectory(NGIN::IO::Path {"assets"});
if (!dir)
{
    return;
}

auto info = dir->GetInfo(NGIN::IO::Path {"textures/logo.png"});
if (!info)
{
    return;
}
```

This is the right API for:

- scoped relative operations
- reducing accidental path escape logic
- code that naturally works “inside this directory”

### Use metadata explicitly

When you need real filesystem facts, call `GetInfo`.

`FileInfo` can expose:

- entry type
- size
- permissions
- timestamps
- ownership
- file identity
- hard-link count

Use `MetadataOptions` when symlink-follow behavior matters.

```cpp
NGIN::IO::MetadataOptions options;
options.symlinkMode = NGIN::IO::SymlinkMode::DoNotFollow;

auto info = fs.GetInfo(path, options);
```

### Use async only when it fits the rest of your program

If your code already uses `TaskContext`, the async helpers are the easiest entry point:

```cpp
NGIN::Async::Task<void, NGIN::IO::IOError> LoadConfig(
    NGIN::Async::TaskContext& ctx,
    NGIN::IO::IAsyncFileSystem& fs)
{
    auto bytes = co_await NGIN::IO::ReadAllBytesAsync(
        fs,
        ctx,
        NGIN::IO::Path {"config.json"});

    Use(bytes);
    co_return;
}
```

For lower-level control, use `OpenFileAsync` and the async file handle surface directly.

If you need scoped async operations relative to an opened directory, use `OpenDirectoryAsync` and
`AsyncDirectoryHandle`.

Recommended async types:

- `IAsyncFileSystem` for async filesystem entry points
- `AsyncFileHandle` for lower-level async file reads and writes
- `AsyncDirectoryHandle` for directory-relative async filesystem work
- `FileSystemDriver` when you want a ready-made worker-backed executor for filesystem tasks

## `Path` Versus `std::filesystem::path`

`Path` is not a drop-in replacement for `std::filesystem::path`.

What `Path` is:

- a lightweight lexical path value type
- intended for normalization and path composition
- explicit about the difference between path strings and filesystem queries

What `Path` is not:

- an implicit filesystem query API
- a type that silently resolves symlinks or existence

Why this split exists:

- lexical operations and filesystem operations are different jobs
- code is easier to reason about when string manipulation does not pretend to know filesystem facts
- real filesystem access stays behind `IFileSystem`

## Practical Caveats

### Path normalization is lexical only

`Path::Normalize()` does not resolve symlinks and does not check whether the path exists.

### Trailing separators are lexical noise

`"a/b/c"` and `"a/b/c/"` decompose the same way for operations such as `Filename()`.

### Symlink behavior should be explicit

If symlink-follow behavior matters, pass `MetadataOptions` instead of relying on defaults you have not checked.

### Text helpers do not solve encoding policy for you

`ReadAllText` and `WriteAllText` are convenient whole-file helpers. They do not define your application’s text
encoding policy beyond the bytes and string types you pass through them.

### Metadata richness can vary by backend and platform

Linux/POSIX backends currently expose richer metadata than some other environments. If your code depends on ownership,
inode/device identity, or advanced file types, verify those expectations on the target platforms you care about.

## Common Mistakes

- Using `Path` as if it answered filesystem questions.
- Reaching for async file APIs when simple sync helpers would be clearer.
- Repeatedly hand-joining path strings instead of opening a `DirectoryHandle`.
- Forgetting to make symlink behavior explicit when metadata correctness matters.

## Reference Notes

Core types:

- `Path`
- `IFileSystem`
- `LocalFileSystem`
- `VirtualFileSystem`
- `FileHandle`
- `AsyncFileHandle`
- `AsyncDirectoryHandle`
- `DirectoryHandle`
- `DirectoryEnumerator`

Use the practical guide above first. For design direction and follow-up work, read
[FileSystemPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/FileSystemPlan.md).
