# FileSystemPlan

Status: draft planning/specification document for evolving `NGIN.Base` IO into a production-grade filesystem subsystem.

## Purpose

This document defines the target shape, constraints, and migration plan for the filesystem layer in `NGIN.Base`.

Today `NGIN.Base` already contains:

- `NGIN::IO::Path`
- `NGIN::IO::IFileSystem`
- `NGIN::IO::LocalFileSystem`
- `NGIN::IO::VirtualFileSystem`
- `NGIN::IO::File`
- `NGIN::IO::FileView`
- `NGIN::IO::FileSystemUtilities`

That is a strong architectural starting point, but it is not yet a production-grade alternative to `std::filesystem`.
The implementation still relies heavily on `std::filesystem` and `std::fstream`, the async surface is a facade rather
than a true async backend, several option fields are not honored, and coverage is too narrow for a core foundation
library.

This plan defines how to close that gap.

## Goals

The filesystem subsystem should become:

- a first-class `NGIN.Base` API, not a thin wrapper around `std::filesystem`
- deterministic and explicit in its path, error, and async behavior
- portable across Linux and Windows, with clear encoding and path rules
- suitable for engine/runtime/tooling code, including virtual mounts and test doubles
- better than direct `std::filesystem` usage in error handling, composability, and async integration

## Non-Goals

This plan does not require:

- full POSIX parity on day one
- instant removal of every `std::filesystem` use in the repository
- network/distributed filesystems in v1
- file watching in the first migration wave

Those can be added later, but they are not required to make the local filesystem abstraction solid.

## Current State

## Strengths

- The public API shape is already good enough to build on.
- The error model is explicit via `IOError` and `Expected`.
- The design already allows dependency injection through `IFileSystem`.
- `VirtualFileSystem` is the right direction for mounts, overlays, and tests.
- `FileView` provides a useful zero-copy or buffered file access abstraction.

## Weaknesses

- `LocalFileSystem` is backed by `std::filesystem` and `std::fstream`, which limits control over semantics and options.
- `FileOpenOptions::share` and most `FileOpenFlags` are not honored by the current implementation.
- Async operations yield and then perform blocking work instead of using a real backend or explicit worker offload.
- `Path` is currently a minimal lexical helper rather than a fully specified path contract.
- `VirtualFileSystem` has partial semantics only; several declared mount options are not enforced.
- Metadata reporting is incomplete.
- Windows path and encoding support is not yet production-grade.
- Test coverage is far too small for a foundational IO layer.

## Core Design Principles

1. No fake semantics

If the API exposes a share mode, async option, or mount policy, the implementation must honor it or reject it
explicitly with a defined error.

2. Own the contract

The public behavior of `NGIN::IO` must be defined by `NGIN.Base`, not by whatever `std::filesystem` happens to do on a
given platform or standard library implementation.

3. Clear lexical vs physical behavior

Lexical path manipulation and filesystem-backed resolution must be separate concepts.

4. UTF-8 public surface

The public `Path` representation should use UTF-8 text. Platform-native conversion must be handled internally.

5. Honest async

Async IO must either be backed by a native async implementation or by explicit worker offload. It must never silently
block the caller while pretending to be asynchronous.

6. Strong error data

Filesystem APIs should return structured errors with stable NGIN error codes, native codes, and relevant path context.

7. Testability first

All important behaviors must be testable through `IFileSystem` and virtual mounts without requiring the real process
working directory or global state.

## Target Architecture

The long-term filesystem stack should look like this:

1. `Path`

- lexical path type
- UTF-8 public representation
- platform-aware normalization and comparison rules
- conversion to and from native platform path representations

2. Native local backend

- OS-handle based file and directory operations
- no `std::fstream` in the production backend
- minimal dependence on `std::filesystem`, ideally limited to transitional helpers only

3. `IFileSystem`

- sync contract for local, virtual, and test filesystems
- explicit support for metadata, links, canonicalization, temp objects, and mutation operations

4. `IAsyncFileSystem`

- worker-backed or native async implementation
- explicit backend selection
- shared error model with sync APIs

5. `VirtualFileSystem`

- mount table
- overlay rules
- read-only mounts
- controlled cross-mount semantics

6. Utility layer

- `ReadAllText`, `WriteAllText`, `CopyTree`, `EnsureDirectory`, atomic helpers, temp helpers

7. Capability and extension model

- portable core contract
- backend capability reporting
- backend-native extension surface where portability is not possible

## Contract Layers

This plan should be implemented in explicit layers rather than as one undifferentiated abstraction.

## Portable Core Contract

This is the API surface that all supported backends must implement in a stable and documented way.

The portable core should include:

- lexical `Path`
- file and directory metadata queries
- regular file open/read/write/seek/flush/resize
- directory creation and deletion
- regular file deletion
- copy, move, and rename with defined same-filesystem and cross-filesystem semantics
- symlink-aware metadata and explicit symlink-follow policy
- directory iteration
- temp directory and temp file helpers
- filesystem space queries where supported
- structured errors and stable error mapping
- capability reporting for optional features

The portable core must be strong enough for repository tooling and runtime code to depend on without reaching into
backend-specific APIs.

## Backend-Specific Contracts

The portable core is not sufficient by itself. The plan must explicitly define:

- Linux/POSIX backend contract
- Windows backend contract
- optional extension contracts for functionality that cannot be represented honestly as fully portable

The implementation must never silently claim portability for behavior that only exists on one backend.

## Backend Capability Model

The filesystem abstraction should be capability-based rather than pretending that every backend supports all operations.

Examples of capability groups:

- symlink creation
- hard link creation
- ownership reporting
- ownership mutation
- POSIX mode bits
- setuid, setgid, sticky bits
- descriptor-relative `*at` operations
- advisory locking
- record/range locking
- nonblocking descriptors
- memory mapping
- nanosecond timestamps
- durable replace semantics
- backend-native security metadata
- file identity reporting

The backend should expose a capability query API so callers can distinguish:

- fully supported
- supported with degradation
- unsupported

The library should prefer explicit feature detection over undocumented best-effort behavior.

## Portable vs Native Extensions

The API should be structured into:

- portable core
- portable optional features
- native extension namespaces or types

That allows the repository to rely on the common contract while still exposing Linux- or Windows-specific strength where
needed.

## Handle Model

The plan should define first-class handle abstractions, not only path-based APIs.

Required handle categories:

- `FileHandle`
- `DirectoryHandle`
- `FileMapping` or `MappedFile`

The distinction matters for:

- secure relative operations
- reducing TOCTOU exposure
- descriptor-level metadata operations
- async integration
- duplication and inheritance control

Path-only APIs are useful convenience helpers, but they should not be the only way to express filesystem work.

## Path APIs vs Handle APIs

The plan should clearly separate:

- path-based convenience operations
- handle-based operations
- directory-handle-relative operations

The handle-based contract should cover:

- open, close, duplicate
- metadata from handle
- read, write, seek
- `pread` / `pwrite` style positional access
- resize/truncate
- sync and durability control
- locking
- mapping

The directory-handle-relative contract should reserve space for operations equivalent to:

- `openat`
- `mkdirat`
- `renameat`
- `unlinkat`
- `fstatat`
- `readlinkat`

## Path Specification

## Path Specification

## Public Representation

- `NGIN::IO::Path` stores UTF-8 text.
- The canonical separator in the public representation is `/`.
- Native separators are accepted at input boundaries and normalized according to the relevant operation.

## Path Categories

The API must distinguish these categories:

- empty path
- relative path
- absolute path
- rooted path
- drive-qualified path on Windows
- UNC path on Windows

The contract must define each category precisely instead of treating all strings as generic slash-delimited segments.

## Required Path Operations

The `Path` type should support:

- lexical normalization
- filename, stem, extension, parent, root name, root directory, root path
- component iteration
- append and join
- prefix and suffix tests that operate on path components, not raw string prefixes
- lexical relative path computation
- lexical absolute path composition against a base
- conversion to native platform path type

## Path Operations That Require Filesystem Access

These should not live on the lexical `Path` type itself. They belong on `IFileSystem` or helper functions:

- canonical / weakly-canonical
- exists checks
- symlink resolution
- current working directory
- temp directory discovery

Filesystem-backed path resolution should also define:

- same-file identity checks
- mount/filesystem boundary awareness
- directory-handle-relative resolution
- explicit symlink-following vs no-follow resolution

## Path Identity and Comparison

The plan should distinguish:

- lexical equality
- normalized lexical equality
- filesystem identity equality

Two paths being textually different but referring to the same inode/file ID must be modeled separately from lexical path
comparison.

## Windows Rules

The implementation must explicitly support:

- UTF-16 Win32 interop
- drive letters
- UNC paths
- verbatim or extended-length path handling
- correct distinction between `C:foo` and `C:/foo`
- case-insensitive comparisons where appropriate at the filesystem layer
- reparse points, symlinks, and junction semantics

Using narrow `CreateFileA` style APIs is not sufficient for the target design.

## Linux/POSIX Rules

The implementation must explicitly support:

- `stat`, `lstat`, `fstat`, and `*at` families where applicable
- `open(2)`-style descriptor semantics
- inode and device identity
- `umask` interaction
- symlink-following vs no-follow behavior
- same-filesystem atomic `rename(2)` semantics
- cross-device rename failure semantics

## Portable Metadata Model

Metadata should be modeled in structured groups rather than as a loosely defined flat bag.

Recommended metadata groups:

- `FileType`
- `Permissions`
- `Ownership`
- `Timestamps`
- `Identity`
- `LinkInfo`
- `StorageInfo`

## File Type Reporting

The metadata model should be able to report at least:

- regular file
- directory
- symlink
- hard-link-aware regular entry metadata
- block device
- character device
- FIFO
- Unix domain socket
- other / unknown

The portable contract should define which of these are required to report on Linux and how Windows maps its native file
types into the portable model.

## Local File Backend Specification

## Native Handle Model

The production `LocalFileSystem` backend should use OS-native handles:

- POSIX file descriptors on Unix-like systems
- Win32 handles on Windows

The standalone `File` abstraction should be upgraded or replaced so that it becomes the core handle implementation used
by `LocalFileSystem`, rather than a separate side utility.

The local backend should also define:

- file-handle open semantics
- directory-handle open semantics
- duplication semantics
- inheritance / close-on-exec semantics
- descriptor-level metadata access
- descriptor-level mutation APIs

## Required Open Semantics

`FileOpenOptions` must have well-defined behavior:

- `FileAccess`
  - `Read`
  - `Write`
  - `ReadWrite`
  - `Append`
- `FileCreateDisposition`
  - `OpenExisting`
  - `CreateAlways`
  - `CreateNew`
  - `OpenAlways`
  - `TruncateExisting`
- `FileShare`
  - must be honored on platforms that support it
  - unsupported share combinations must fail explicitly rather than being ignored
- `FileOpenFlags`
  - `Sequential`
  - `RandomAccess`
  - `WriteThrough`
  - `Temporary`
  - `DeleteOnClose`
  - `AsyncPreferred`

Each flag must be documented as:

- fully supported
- partially supported with defined degradation
- unsupported and rejected

Silently ignoring these flags is not acceptable in the target design.

The plan should also reserve space for flags or semantics equivalent to:

- `O_NOFOLLOW`
- `O_CLOEXEC`
- `O_NONBLOCK`
- `O_SYNC`
- `O_DSYNC`
- `O_DIRECTORY`
- `O_NOCTTY`

Where exact parity is not portable, the backend contract must document the mapping or rejection behavior.

## Descriptor and Low-Level IO Semantics

The low-level handle contract should explicitly cover:

- `read`
- `write`
- `close`
- seek
- `pread`
- `pwrite`
- `ftruncate`
- `fsync`
- `fdatasync`
- descriptor duplication
- descriptor control where applicable

The plan should also define:

- partial read/write behavior
- interruption and retry behavior
- nonblocking behavior
- append-mode semantics
- short write semantics

These details are important for Linux correctness and for honest async design.

## Metadata Contract

`FileInfo` should be expanded and documented to include, where available:

- entry type
- size
- mode bits
- ownership
- created time
- modified time
- accessed time
- change time where supported
- permissions
- existence
- symlink status
- link target where requested
- hard link count
- inode and device identity or platform equivalents
- mount/filesystem identity where practical

Recommended metadata fields:

- file type
- raw mode bits
- uid
- gid
- atime
- mtime
- ctime
- size
- inode or file ID
- device or volume identity
- link count
- symlink target where queried

Timestamp precision should preserve nanoseconds where supported.

Missing metadata should be marked invalid explicitly rather than left ambiguous.

## Permissions and Ownership

The plan should explicitly cover:

- full POSIX mode bits on Linux
- setuid
- setgid
- sticky bit
- ownership reporting
- ownership mutation
- process umask interaction

Required mutation APIs to account for in the backend contracts:

- path-based permission changes
- handle-based permission changes
- path-based ownership changes
- symlink ownership changes where supported
- handle-based ownership changes
- timestamp mutation

Windows cannot map all of this directly to ACLs and SIDs. The plan should explicitly define:

- what the portable ownership/permission model guarantees on Windows
- what is exposed only through native extensions

## Symlink Policy

The plan needs a dedicated symlink policy matrix.

For each relevant operation, the contract should specify:

- follow symlink by default
- do not follow symlink by default
- caller-selectable follow mode
- unsupported

This should be defined for:

- metadata query
- open
- delete
- rename
- permission mutation
- ownership mutation
- timestamp mutation
- canonicalization
- existence checks

Dangling symlink handling must also be specified explicitly.

## Directory Enumeration

Enumeration should become a streaming operation rather than a full eager materialization by default.

Required behaviors:

- recursive and non-recursive enumeration
- symlink handling policy
- stable ordering when requested
- optional metadata prefetch
- defined behavior for permission-denied entries
- correct handling of `.` and `..`
- explicit lifetime and invalidation rules for iteration entries

The current vector-backed enumerator may remain as an adapter or convenience layer, but should not be the only model.

The plan should reserve space for directory-stream semantics equivalent to:

- `opendir`
- `readdir`
- `closedir`

and for directory-handle-backed enumeration where that is the preferred backend model.

## Filesystem API Additions

The current `IFileSystem` surface is a good start but should grow to include the missing operations needed by tools and
runtime systems.

Proposed additions:

- `Canonical(path)`
- `WeaklyCanonical(path)`
- `Absolute(path, base)`
- `ReadSymlink(path)`
- `CreateSymlink(target, linkPath)`
- `CreateHardLink(target, linkPath)`
- `SetPermissions(path, permissions)`
- `SetTimes(path, created/modified/accessed as supported)`
- `CreateTempFile(directory, prefix, options)`
- `CreateTempDirectory(directory, prefix, options)`
- `ReplaceFile(source, destination, options)` for atomic or best-effort replacement
- `EnsureDirectory(path)` as a core convenience
- `SameFile(pathA, pathB)`
- `ReadMetadata(path, followMode)`
- `ReadMetadata(handle)`
- `ReadMetadataAt(directoryHandle, relativePath, followMode)`
- `OpenDirectory(path, options)`
- `OpenAt(directoryHandle, relativePath, options)`
- `RenameAt(fromDirectory, fromName, toDirectory, toName, options)`
- `UnlinkAt(directoryHandle, relativePath, options)`
- `ReadSymlinkAt(directoryHandle, relativePath)`
- `CreateSymlinkAt(directoryHandle, target, linkName)`
- `CreateHardLinkAt(fromDirectory, fromName, toDirectory, toName)`
- `SetPermissions(handle, permissions)`
- `SetOwnership(path, owner, group, followMode)`
- `SetOwnership(handle, owner, group)`
- `SetTimes(handle, times)`
- `DuplicateHandle(handle, options)`
- `MapFile(handle, options)`
- optional `Watch(path, options)` in a later phase

Not all of these must land in the first iteration, but the plan should reserve space for them.

## Durability and Atomicity

The plan should define durability separately from ordinary correctness.

Required topics:

- what `Flush` guarantees
- what `fsync`-style sync guarantees
- what data-only sync guarantees
- file-mapping flush semantics
- directory sync expectations after rename or replace where relevant
- same-filesystem atomic rename guarantees
- cross-filesystem move degradation
- replace semantics when destination exists

The library should avoid vague language around "atomic" unless the same-filesystem and cross-filesystem cases are
explicitly separated.

## Error Mapping Contract

The portable error model should map native backend errors into stable NGIN codes while preserving the native code.

At minimum, the plan should account for errors equivalent to:

- `ENOENT`
- `EEXIST`
- `ENOTDIR`
- `EISDIR`
- `ENOTEMPTY`
- `EXDEV`
- `EACCES`
- `EPERM`
- `EMLINK`
- `EINVAL`
- `ENOSPC`
- `EROFS`
- `ELOOP`
- `ENAMETOOLONG`
- `EBUSY`
- `EINTR`

Windows-native errors should be mapped into the same stable categories where possible, with the original system code
preserved for diagnostics.

## Async Filesystem Specification

## Required Async Semantics

Async operations must not merely yield and then run blocking IO on the same thread.

Acceptable implementations:

- native async backend
  - IOCP on Windows
  - io_uring or another explicit async mechanism on Linux
- explicit worker-backed backend
  - blocking operations dispatched to a thread pool owned by `FileSystemDriver`

The backend may start as worker-backed if that is the fastest path to correctness, but the behavior must be explicit and
honest.

## Driver Responsibilities

`FileSystemDriver` should become the owner of async execution policy:

- thread count
- queue depth hint
- backend selection
- cancellation propagation
- shutdown semantics

`IAsyncFileSystem` implementations should either be constructed with a driver or clearly document their scheduling and
execution model.

## Cancellation

Cancellation should be cooperative but predictable:

- if an operation has not started, cancellation should prevent execution
- if an operation is already in progress and cannot be interrupted, the API must document that behavior
- error returns for cancellation must use the structured `IOError` model

## Async Design Constraints

The async design should account for:

- descriptor-backed operations
- directory iteration and directory-handle usage
- file mapping lifecycle
- durable flush operations
- cancellation during worker-offloaded blocking IO
- backend capability differences between Linux and Windows

If Linux begins with worker-backed async rather than native kernel async, the plan should state that explicitly as an
intermediate backend choice rather than implying parity with `io_uring`.

## Linux/POSIX Backend Contract

The Linux backend section should explicitly define support expectations for:

- `stat`, `lstat`, `fstat`
- `open`, `openat`
- `mkdir`, `mkdirat`
- `rename`, `renameat`
- `unlink`, `unlinkat`
- `fstatat`
- `readlink`, `readlinkat`
- `symlink`
- `link`
- `rmdir`
- `mkfifo`
- `mmap`
- `msync`
- `fcntl`-style locking and descriptor control
- `readv` / `writev` / `preadv` / `pwritev` if adopted later
- `statvfs` style filesystem information if included in the portable contract

The plan should also define how Linux-only file types and mode bits surface through the portable model.

## Windows Backend Contract

The Windows backend section should explicitly define support expectations for:

- UTF-16 path handling
- drive, UNC, and extended-length paths
- handle-based open and duplication
- share-mode behavior
- delete-pending behavior
- file replacement and rename semantics
- file identity reporting
- reparse points, symlinks, and junctions
- memory mapping
- inheritance and close-on-exec-equivalent behavior
- security descriptor and ACL stance

The plan should not imply that POSIX ownership, POSIX mode bits, or inode semantics map 1:1 to Windows. Where they do
not, the backend contract should document whether the portable API degrades, emulates, or rejects those operations.

## Virtual FileSystem Specification

`VirtualFileSystem` should be kept and completed. It is one of the clearest reasons to prefer `NGIN::IO` over direct
`std::filesystem`.

## Required Mount Semantics

Each mount must define:

- virtual prefix
- backing filesystem
- priority
- read-only behavior
- case sensitivity policy
- shadowing policy

The implementation must enforce those policies rather than merely storing the fields.

## Cross-Mount Operations

The behavior of copy, move, and rename across mounts must be defined:

- rename across mounts should fail with a stable cross-device style error
- copy across mounts should be implemented
- move across mounts should degrade to copy-plus-remove where permitted
- read-only destination or source restrictions must be enforced consistently

## Virtual CWD

The virtual filesystem should either:

- support a virtual current working directory with explicit rules, or
- continue to reject the operation, but document that choice as a deliberate design decision

The current implicit partial behavior should be avoided.

## Conformance Matrix

The final version of this plan should include a backend conformance matrix.

For each major operation or metadata field, the matrix should classify support as:

- portable required
- portable optional
- Linux-specific
- Windows-specific
- unsupported

This matrix should become the reference used during implementation and testing.

## Testing Requirements

The filesystem subsystem needs significantly stronger testing before it can replace broad repository usage of
`std::filesystem`.

Minimum required coverage:

- path normalization edge cases
- Windows-style and POSIX-style path parsing
- open/create/truncate/append behavior
- share mode and open flag semantics
- read, write, seek, set size
- file mapping and fallback behavior
- metadata and timestamps
- recursive copy and remove
- cross-device or cross-mount move behavior
- symlink and hard link handling
- dangling symlink handling
- file type reporting beyond regular files and directories
- ownership and permission mutation where supported
- descriptor-relative operations
- durable rename and replace semantics
- partial IO and interruption behavior
- directory-handle flows
- permission failures
- async success, failure, and cancellation
- virtual mount precedence, read-only enforcement, and shadowing
- Unicode paths

Tests should use both:

- real local temp directories
- custom fake filesystems implementing `IFileSystem`

## Migration Strategy

## Phase 0: Document the Contract

- create this plan
- decide UTF-8 and platform path policy
- decide async backend policy
- decide which open flags are required for v1

## Phase 1: Stabilize `Path`

- expand `Path` from minimal lexical helper to a fully specified path value type
- add missing tests for component logic and platform edge cases
- add clear native conversion helpers

## Phase 2: Replace the Local File Handle Backend

- remove `std::fstream` from `LocalFileSystem`
- unify around OS-native file handles
- make `FileOpenOptions` semantics real
- improve metadata reporting

## Phase 3: Make Async Honest

- wire `FileSystemDriver` into async file operations
- implement worker-backed async first if needed
- add cancellation and failure-path tests

## Phase 4: Complete `VirtualFileSystem`

- enforce `caseSensitive`
- enforce `allowShadowing`
- implement cross-mount copy and move
- define virtual cwd behavior

## Phase 5: Expand Utility and Mutation Surface

- add temp helpers
- add canonicalization helpers
- add link operations
- add atomic replace helpers

## Phase 6: Migrate Repository Consumers

Only after the above phases are complete enough should high-level tooling migrate away from direct
`std::filesystem` usage.

Suggested order:

1. CLI model structs that only store paths
2. CLI helper functions for path normalization and directory checks
3. CLI manifest loading and staging logic
4. broader workspace and build tooling

## CLI Migration Notes

The CLI currently uses `std::filesystem` broadly for path storage and resolution. That is not just a `Model.hpp`
concern; it also appears in support helpers, authoring logic, build logic, and resolution logic.

The correct migration path is therefore:

- do not replace `std::filesystem` in the CLI first
- make `NGIN::IO` strong enough first
- then migrate the CLI in phases with tests around canonicalization, staging, and path containment checks

Changing `NGIN::CLI::fs` aliases before the underlying subsystem contract is ready would only move the missing behavior
into ad hoc CLI code.

## Acceptance Criteria

The filesystem subsystem can be considered production-grade for repository-wide adoption when all of the following are
true:

- portable core contract is specified separately from Linux and Windows backend contracts
- capability reporting exists for non-portable features
- `LocalFileSystem` no longer depends on `std::fstream`
- public path behavior is specified and tested
- handle semantics are specified and tested
- open/share/flag semantics are real or explicitly rejected
- symlink-follow behavior is explicitly defined across APIs
- async behavior is honest and backed by a defined execution policy
- virtual mounts enforce their declared policies
- metadata coverage is documented and consistent
- durability and atomicity semantics are documented
- backend conformance is documented
- tests cover success paths, error paths, platform edge cases, and cancellation
- the CLI can migrate without reintroducing `std::filesystem`-specific helpers for missing features

## Recommended First Deliverables

If implementation starts immediately, the recommended first work items are:

1. formalize the `Path` contract and add the missing tests
2. refactor `LocalFileSystem` to an OS-handle backend
3. wire `FileSystemDriver` into async file execution
4. finish `VirtualFileSystem` mount policy enforcement
5. migrate the CLI path model once the subsystem is ready

This sequence gives the fastest path to a usable and defensible `NGIN::IO` filesystem alternative.
