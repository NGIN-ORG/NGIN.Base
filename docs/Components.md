# NGIN.Base Components

NGIN.Base ships six independently compiled ownership components. The component
model is enforced at configure time and is shared by build-tree and installed
package exports.

## Ownership

| Component | Public areas |
| --- | --- |
| Foundation | Containers, exceptions, hashing, math, memory, metadata, SIMD, synchronization, text, time, utilities, primitives, units, and benchmarking |
| Execution | Async tasks, cancellation, threads, fibers, executors, and schedulers |
| IO | Paths, files, directories, filesystem drivers, the VFS, and dynamic libraries |
| Serialization | Serialization core plus JSON and XML |
| Crypto | Crypto providers, algorithms, keys, certificates, tokens, and secure memory |
| Net | Addressing, sockets, network drivers, buffers, and transport adapters |

The source of truth for installed-header classification is
`cmake/NGINBaseComponentHeaders.cmake`. Adding an unowned header or assigning a
header to multiple components is a configuration error. Headers below a
lowercase `detail/` directory are owned and installed with their component but
are not standalone public contracts.

## Dependency direction

```text
Foundation
   |
Execution
   |
IO ------> Serialization
|               |
+-------> Crypto+
|
+-------> Net
```

In explicit terms:

- Foundation has no NGIN.Base component dependency.
- Execution depends on Foundation.
- IO depends on Foundation and Execution.
- Serialization depends on Foundation and IO.
- Crypto depends on Foundation, IO, and Serialization.
- Net depends on Foundation, IO, Execution, and Crypto.

Net depends on Crypto for the provider-neutral TLS credential contract. It does
not introduce a Crypto-to-Net dependency.

The 2026-08-05 public-header include audit found no dependency outside these
allowed directions. The compiled targets enforce the same direction through
their public target links.

## CMake targets

Each component provides `NGIN::Base::<Component>::Static`,
`NGIN::Base::<Component>::Shared`, and a preferred-form
`NGIN::Base::<Component>` alias. `NGIN::Base::Static`, `NGIN::Base::Shared`,
and `NGIN::Base` are interface aggregates over the corresponding components;
they do not compile a second copy of component sources.

## Public-surface conventions

- Central entry points are the documented subsystem umbrellas and the focused
  headers used by their examples.
- A header or type explicitly marked experimental, currently including
  `Memory/EpochReclaimer.hpp`, may evolve without the compatibility expectation
  of a central API.
- Lowercase `detail/` directories are installed implementation dependencies,
  not standalone public contracts. The legacy `Text/Unicode/Detail.hpp` path is
  also implementation-only despite its historical capitalization.
- Public non-template compiled APIs use their owning component's
  `NGIN_<COMPONENT>_API` macro; `NGIN_BASE_API` remains a source-compatibility
  declaration macro. Private symbols use `NGIN_BASE_LOCAL`. Templates and small
  value operations remain inline in headers.
- New tests belong in the focused `tests/<Area>/` file or a narrowly named new
  file. Performance workloads belong in `benchmarks/` and must not leak their
  optional comparison dependencies into production targets.
- New headers and sources must be assigned in
  `cmake/NGINBaseComponentHeaders.cmake`; configure must fail rather than infer
  ambiguous ownership.

## Verification

When tests are enabled, `NGINBasePublicHeaderChecks` compiles every public
contract header in an independent translation unit. This catches accidental
reliance on transitive includes. Detail headers are compiled through their
owning public header. Component-focused tests link the narrow owning component.
The external-consumer matrix links and runs the aggregate plus all six
components against both build-tree and installed package exports.
