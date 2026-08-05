# NGIN.Base completion baseline

This baseline was captured on 2026-08-05 before the approved compiled-component
migration. It is evidence for comparison, not a portable performance promise.

## Public and build surface

- 259 installed headers, of which 257 are standalone public contracts
- ownership components: Foundation, Execution, IO, Serialization, Crypto, Net
- current aggregate forms: `NGIN::Base::Static`, `NGIN::Base::Shared`, and the
  preferred-form `NGIN::Base` alias
- transitional focused compiled targets: `NGIN::Net`, `NGIN::Serialization`,
  and `NGIN::Crypto`

Every public contract header is compiled independently by
`NGINBasePublicHeaderChecks`. Configure-time ownership validation rejects
unowned or multiply owned headers and active sources.

## Focused verification ownership

| Component | Focused test prefixes | Performance targets |
| --- | --- | --- |
| Foundation | `Base.Containers`, `Base.Memory`, `Base.Sync`, `Base.Text`, `Base.Time`, `Base.Utilities`, `Base.Math`, `Base.Meta`, `Base.SIMD` | `AllocatorBenchmarks`, `ConcurrentMapBench`, `StringBenchmarks`, `VectorBenchmarks`, `SIMDFastMathBench` |
| Execution | `Base.Async`, `Base.Execution` | `SchedulerBenchmarks`, `FiberBenchmarks` |
| IO | `Base.IO` | focused behavior tests; no synthetic IO benchmark |
| Serialization | `Base.Serialization` | `JsonBenchmarks`, `XmlBenchmarks` |
| Crypto | `Base.Crypto` | crypto random/hash/AEAD/parser/key-format/dispatch benchmarks |
| Net | `Base.Net` | focused loopback and resolver tests; no internet benchmark |

Each component also has a `Base.Include.<Component>` umbrella-contract test.
The exact generated test list is available with `ctest -N` for the configured
build tree.

## Release library-size reference

MSVC 19.44, x64 Release, static build, default Windows providers:

| Archive | Bytes |
| --- | ---: |
| `NGINBase.lib` | 11,299,506 |
| `NGINNet.lib` | 1,402,564 |
| `NGINSerialization.lib` | 2,249,246 |
| `NGINCrypto.lib` | 3,211,296 |

The transitional focused archives duplicate objects from the aggregate and
must not be summed. The component migration must replace this overlap and
capture a new baseline.

## Performance reference

MSVC x64 Release on the same host; averages include the repository benchmark
harness overhead and should only be compared on equivalent hardware/builds.

Allocator workload (1,024 64-byte allocations/frees):

| Allocator | Average |
| --- | ---: |
| System | 58.54 us |
| Fixed block | 3.25 us |
| Segregated pool, mixed small sizes | 7.07 us |
| Linear allocate/reset | 1.05 us |

Concurrent map, four threads and 500 operations per thread:

| LocalEpoch workload | Average |
| --- | ---: |
| 95% read | 0.500 ms |
| 75% write | 1.311 ms |
| 75% read mixed | 0.741 ms |
| reclamation-heavy | 1.419 ms |

The same reclamation-heavy case averaged 1.370 ms with hazard pointers and
3.172 ms with manual quiescence. These short workloads validate benchmark
coverage; longer local campaigns should increase iterations for stable
performance decisions.
