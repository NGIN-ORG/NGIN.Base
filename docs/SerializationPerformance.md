# Serialization performance refactor

Measured 2026-09-08 against NGIN.Base checkpoint `9a5df89` (workspace `c5ccbee`).
The checkpoint predates this refactor and includes the simplified owning `Parse`
API. Measurements compare the same inputs and operations, including destruction
of each parsed document. No external-parser comparison or SIMD-width claim is made.

## Method

`SerializationWorkloads` uses `std::chrono::steady_clock`, two warm-up samples
and nine timed samples per process, reporting the median. Small parsing cases use
1,000 repetitions per sample, larger cases one; lookups use 10,000. The table
below takes the median across five alternating before/after process pairs pinned
to CPU 2. Both binaries were built with GCC 15.2, C++23, `RelWithDebInfo` (`-O2`),
on an Intel Core i7-13700KF under Linux/WSL. Differences around 1–3% should be
regarded as effectively flat, not dependable wins. These synthetic workloads do
not establish production latency percentiles or adversarial-input guarantees.

Build and run from the workspace root:

```sh
cmake -S Dependencies/NGIN/NGIN.Base -B build/ngin-base-bench -DNGIN_BASE_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/ngin-base-bench --target SerializationWorkloads -j4
taskset -c 2 build/ngin-base-bench/benchmarks/SerializationWorkloads
```

CPU pinning is optional and Linux-specific. Output values are nanoseconds except
`/memory/` rows, which are document `MemoryCommitted()` bytes. Full aggregate
results are in [SerializationPerformance.csv](SerializationPerformance.csv).
The earlier baseline harness also printed lookup rows for non-object fixtures;
those rows were misses and are deliberately excluded from these comparisons.

## CPU time

| Workload | Before (ns) | After (ns) | Change |
|---|---:|---:|---:|
| JSON/parse/tiny | 476.6 | 443.5 | -6.9% |
| JSON/parse/object-8 | 610.6 | 605.6 | -0.8% |
| JSON/parse/object-32 | 2,557.5 | 2,575.2 | +0.7% |
| JSON/parse/object-1000 | 741,758.0 | 80,210.0 | -89.2% |
| JSON/parse/object-8000 | 42,914,565.0 | 721,652.0 | -98.3% |
| JSON/parse/array-100KiB | 670,096.0 | 674,799.0 | +0.7% |
| JSON/parse/array-1MiB | 11,244,161.0 | 10,371,336.0 | -7.8% |
| JSON/parse/escapes | 370.8 | 352.4 | -5.0% |
| XML/parse/tiny | 288.9 | 292.6 | +1.3% |
| XML/parse/attributes-8 | 468.5 | 473.0 | +1.0% |
| XML/parse/attributes-32 | 2,404.4 | 1,688.6 | -29.8% |
| XML/parse/attributes-1000 | 1,203,949.0 | 90,054.0 | -92.5% |
| XML/parse/elements-100KiB | 493,498.0 | 483,905.0 | -1.9% |
| XML/parse/elements-1MiB | 5,621,524.0 | 5,424,811.0 | -3.5% |
| JSON/find-last/object-1000 | 1,479.9 | 11.5 | -99.2% |
| XML/attribute-last/attributes-1000 | 2,171.4 | 12.7 | -99.4% |
| JSON/chunks-4096/tiny | 1,051.0 | 729.7 | -30.6% |
| JSON/chunks-4096/object-1000 | 1,632,831.0 | 150,093.0 | -90.8% |
| JSON/chunks-4096/array-100KiB | 1,580,850.0 | 1,193,839.0 | -24.5% |
| XML/chunks-4096/tiny | 575.6 | 448.6 | -22.1% |
| XML/chunks-4096/attributes-1000 | 1,936,032.0 | 91,459.0 | -95.3% |
| XML/chunks-4096/elements-100KiB | 1,109,927.0 | 864,460.0 | -22.1% |

## Retained document memory

This counts source bytes, table capacity, indexes, and arena blocks. It excludes
fixed object headers, stack frames, allocator metadata, and transient reallocation
peaks. It is not process RSS. Both sides use the same definition for the retained
document tables; the new budget also checks temporary parser allocations.

| Workload | Before (bytes) | After (bytes) | Change |
|---|---:|---:|---:|
| JSON/memory/tiny | 776 | 552 | -28.9% |
| JSON/memory/object-32 | 5,469 | 4,469 | -18.3% |
| JSON/memory/object-1000 | 125,949 | 101,973 | -19.0% |
| JSON/memory/array-100KiB | 2,053,665 | 1,488,801 | -27.5% |
| JSON/memory/array-1MiB | 16,944,909 | 12,197,021 | -28.0% |
| XML/memory/tiny | 250 | 250 | +0.0% |
| XML/memory/attributes-32 | 1,427 | 1,723 | +20.7% |
| XML/memory/attributes-1000 | 45,619 | 53,851 | +18.0% |
| XML/memory/elements-100KiB | 626,712 | 626,712 | +0.0% |

## Accepted tradeoffs

- Small containers scan names without allocating indexes. The threshold is 16;
  wide containers use four-byte hash slots referring to existing names. An index
  adds about 18–21% retained memory in the measured wide XML attribute cases, in
  exchange for much faster duplicate checking and repeated attribute lookup.
  XML documents without wide attribute lists keep their existing table layout.
- JSON removes the eager `ValueView` array, stores source identity once, reuses a
  small pending-member stack, and links array children during parsing. Numeric
  arrays benefit mostly in retained memory; their CPU result should not be sold
  as a uniform speedup. The small pending-member buffer uses stack storage.
- Indexed queries first locate a wide container's index by binary search, then
  probe its hash table. Hashing depends on key length, and collisions can degrade
  performance. Iteration remains in source order. No worst-case O(1) claim is made.
- XML child iteration still follows compact sibling IDs. Removing child
  subscripting prevents repeated indexed traversal from accidentally becoming
  quadratic. Existing range-for traversal stays essentially unchanged and does
  not pay for another per-child pointer table.
- Incremental input is tokenized once and processed without DOM validation or
  retaining completed source. Tokens crossing chunk boundaries are copied and
  scanned incrementally. A token is subsequently decoded/validated by the shared
  lexical routines; this is not a claim that every byte is inspected only once.
  Tests feed over 100 KiB of JSON and 200 KiB of XML through a 4 KiB retained-memory
  budget while asserting callbacks during every feed.
- Streaming memory scales with the largest unfinished token, scratch capacity,
  nesting, and keys of open JSON objects. A huge string, text run, or XML start tag
  can therefore still require substantial memory. `KeepLast` JSON buffers the
  document until `Finish()` and parses it once through the DOM path. Later keys
  can replace earlier values, which prevents immediate irreversible callbacks.
- Early callbacks are observable behavior: a later syntax error does not retract
  them. Consumers needing atomic updates must stage changes until `Finish()`.
  The old transactional validation pass is removed rather than hidden elsewhere.

## Validation

Focused JSON/XML, corpus, incremental, core serialization, and token tests pass.
Serialization tests also pass with AddressSanitizer and UndefinedBehaviorSanitizer.
Coverage includes every split point in corpus fixtures, randomized partitions,
2,000 mutated documents, wide duplicate policies and builders, global diagnostics,
handler stops, and retained-memory limits. CLI and reflection metagen build.
The focused CLI suite retains one unrelated pre-existing failure: its hardcoded
manifest count expects 53, while 54 authored manifests are checked in.
