# Optimization Plan (JSON + XML Parsers)

Goal: state-of-the-art parsing throughput (MB/s), low allocations, and predictable latency for DOM and streaming APIs.
Breaking changes are acceptable if they deliver clear wins.

## Scope

- JSON: `include/NGIN/Serialization/JSON/JsonParser.hpp`, `src/NGIN/Serialization/JSON/JsonParser.cpp`, `include/NGIN/Serialization/JSON/JsonTypes.hpp`
- XML: `include/NGIN/Serialization/XML/XmlParser.hpp`, `src/NGIN/Serialization/XML/XmlParser.cpp`, `include/NGIN/Serialization/XML/XmlTypes.hpp`
- Shared: `include/NGIN/Serialization/Core/InputCursor.hpp`

## Top 3 Optimizations (Detailed)

### 1) Switch DOM to a compact tape / indexed arena layout

Current behavior allocates `JsonArray`/`JsonObject`/`XmlElement` plus per-node `Vector` growth in tight loops.
This causes reallocation and pointer chasing (especially for large arrays/objects).

Plan:
- Introduce a compact, contiguous representation ("tape" or SoA blocks) for nodes and child/member lists.
- Two-pass parse for DOM:
  1) Pass 1 counts element counts/attributes/array sizes using a structural scan.
  2) Pass 2 allocates exact-size blocks from the arena and fills them without `PushBack` growth.
- Expose a stable view API while allowing internal storage to change.

Breaking impact:
- `JsonArray`/`JsonObject` and `XmlElement` storage changes (index-based or pointer-to-slice).
- Likely new types like `JsonArrayView`/`JsonObjectView` or `Span`-based members.

Expected wins:
- Fewer allocations and cache misses (large documents benefit most).
- Better SIMD-friendly parsing pipeline (aligns with structural scan).

### 2) Add in-situ XML parsing + lazy entity/whitespace decoding

JSON already has in-situ parsing, but XML always allocates in `DecodeEntities` and `NormalizeWhitespace`.
On typical data (few entities), this is pure overhead.

Plan:
- Add `XmlParseOptions::inSitu` and a `Parse(std::span<Byte>)` overload.
- Implement in-situ decoding with two-pointer compaction (read/write) so decoded text lives in the input buffer.
- Add a lazy decode mode:
  - Store raw slices in `XmlNode`/`XmlAttribute`.
  - Decode only on demand (exposed via API or explicit `Decode()` call).

Breaking impact:
- `XmlParser` overloads and `XmlParseOptions` change.
- `XmlNode`/`XmlAttribute` may store lazy or raw/decoded variants.

Expected wins:
- Large reduction in allocations and copy cost for text-heavy XML.
- Improved locality when parsing from memory-mapped buffers.

### 3) Replace per-char loops with bulk scanning + fast number paths

Hot loops currently use per-char `Peek/Advance` and `std::is*` checks.
This is correct but leaves performance on the table.

Plan:
- Use `memchr`/SIMD scans for common delimiters:
  - JSON strings: find `"` or `\\` quickly.
  - XML text: find `<` or `&` quickly.
- Replace `std::isalpha/isdigit` with ASCII tables (branchless, predictable).
- JSON numbers:
  - Implement fast integer/short-decimal parsing to `int64/uint64/double`.
  - Fall back to `from_chars` only when needed.

Breaking impact:
- Internal parser logic only; public API can stay stable.

Expected wins:
- Fewer branches and better instruction throughput.
- Substantial speedups for large documents and number-heavy payloads.

## Additional High-Impact Optimizations

- Streaming parser with persistent state to support chunked input without full buffering.
- Optional string interning for JSON object keys and XML names to reduce memory/compare cost.
- Optional hash-index build for XML attributes (parallel to JSON object index) for fast lookup.
- Pre-sized arena based on heuristic or a pre-scan to reduce arena growth failures.
- Minimize `ParseError` construction in fast paths (only build on failure).

## Proposed Phases

1) Baseline benchmarks (current state) for JSON/XML DOM + SAX APIs.
2) Implement compact DOM layout (Top 1).
3) Add XML in-situ + lazy decoding (Top 2).
4) Bulk scans + fast number path (Top 3).
5) Optional streaming/incremental parsing and key interning.

## Benchmarks to Track

- Parse throughput (MB/s) for small/medium/large documents.
- Allocation count and total bytes from arena.
- Latency distribution (p50/p95) for large files.
- Correctness tests for escapes/entities, malformed input, deep nesting.

## Risks / Tradeoffs

- Tape/indexed layout is a breaking change; requires adapter views for existing API users.
- In-situ parsing requires mutable input buffers; document API must communicate this clearly.
- SIMD/bulk scans require careful fallback paths and exact error location accounting.

## Next Actions

- Decide on DOM layout strategy (tape vs indexed arrays + spans).
- Decide on XML lazy decode API shape and opt-in flags.
- Add benchmark cases in `benchmarks/JsonBenchmarks.cpp` and `benchmarks/XmlBenchmarks.cpp` to capture wins.
