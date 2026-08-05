# NGIN.Base documentation

- [Foundation](Foundation.md): primitives, ownership boundaries, and the Foundation umbrella
- [Utilities](Utilities.md): expected/optional values, type erasure, symbols, and typed errors
- [Meta and hashing](MetaAndHashing.md): compile-time identity, traits, FNV, CRC, and checksums
- [Math and units](MathAndUnits.md): dimensioned quantities, ratios, and large integers
- [Async](Async.md): tasks, contexts, cancellation, and combinators
- [Execution](Execution.md): schedulers, threads, fibers, and execution ownership
- [Synchronization](Sync.md): mutexes, guards, semaphores, spin locks, and conditions
- [SIMD](SIMD.md): portable vector and scanning contracts
- [Containers](Containers.md): allocator-aware and concurrent containers
- [I/O](IO.md): paths, files, directories, and filesystem abstractions
- [Process execution](Process.md): direct child processes, streams, cancellation, and isolation
- [Network](Network.md): sockets and the explicit async network driver
- [Memory](Memory.md): allocators, arenas, and allocator-aware containers
- [Crypto](Crypto.md): providers, keys, hashes, encryption, and tokens
- [Text](Text.md): text primitives and Unicode-related utilities
- [Serialization](../include/NGIN/Serialization/README.md): strict JSON and XML
- [Components](Components.md): public-header ownership and dependency direction
- [Public header and documentation style](CodeStyle.md): filename ownership and Doxygen conventions
- [Completion baseline](Baseline.md): header/test inventory, library sizes, and performance reference
- [Platform support](PlatformSupport.md): supported systems, capability differences, and known limitations
- [Release readiness](ReleaseReadiness.md): verification evidence and remaining approval gates

Public headers are the API source of truth. Implementation planning is tracked
outside this documentation tree; Git history preserves completed designs.
