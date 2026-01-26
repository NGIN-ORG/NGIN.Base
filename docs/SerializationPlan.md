# Serialization Plan (JSON + XML)

This plan targets high-performance JSON/XML parsing aligned with NGIN.Base patterns: minimal std usage,
allocator-aware, and deterministic. It avoids std streams/filesystem and favors in-memory parsing.

## Design Targets

- Public API in `include/NGIN/Serialization/...`, non-trivial impl in `src/NGIN/Serialization/...`.
- Minimal standard library reliance: allow `std::span` and `std::string_view` where already used.
- No `std::istream`/`std::filesystem` in core parsing paths.
- Error handling via `NGIN::Utilities::Expected` + structured `ParseError`.
- Zero or low allocations on hot paths; all allocations via NGIN allocators.

## Phase 1: Core Serialization + Input Abstractions

- Create `include/NGIN/Serialization/Core/`:
  - `ParseLocation` (byte offset; optional line/col).
  - `ParseError` (code, location, message).
  - `ParseOptions` (format-specific flags, in-situ vs copy).
  - `Expected`-based API for all parse entry points.
- Create `InputCursor` for in-memory parsing:
  - Works over `std::span<const NGIN::Byte>` and `std::string_view`.
  - `Peek()`, `Advance()`, `Match()`, `SkipWhitespace()`; branch-light.
- Introduce minimal IO abstraction (no std streams):
  - `NGIN::IO::IByteReader` with `Read(ByteSpan) -> Expected<UIntSize, IOError>`.
  - `MemoryReader` for in-memory buffers.

## Phase 2: JSON Parser (Correctness Baseline)

- Public API in `include/NGIN/Serialization/JSON/`:
  - `JsonValue`, `JsonObject`, `JsonArray`, `JsonDocument`.
  - `JsonParser::Parse(span/reader) -> Expected<JsonDocument, ParseError>`.
  - `JsonReader` event API: `OnNull/OnBool/OnNumber/OnString/OnStartObject/OnEndObject/...`.
- DOM representation:
  - Arena-allocated nodes (default `NGIN::Memory::LinearAllocator`).
  - Strings are `std::string_view` when unescaped; arena copy when escaped.
  - Objects store ordered members; optional index for fast lookup.
- Parsing rules:
  - UTF-8 input; handle `\uXXXX` + surrogate pairs.
  - Numbers parsed to `F64` (optionally keep integer when exact).
  - Strict RFC JSON by default; allow comments/trailing commas via options.

## Phase 3: JSON Performance Pass

- Structural scanning (SIMD-friendly):
  - Find structural characters (`{ } [ ] : , "`) using NGIN SIMD or scalar fallback.
  - Parse via index "tape" to reduce branching.
- Fast number parsing:
  - Implement Eisel-Lemire/Grisu2-style fast float parsing with fallback.
- Fast string unescape:
  - Vectorized scan for `\\` and `"`; allocate only on escapes.
- In-situ parsing option:
  - `JsonParseOptions::InSitu = true` stores slices in mutable input buffer.

## Phase 4: XML Parser (Correctness Baseline)

- Public API in `include/NGIN/Serialization/XML/`:
  - `XmlDocument`, `XmlElement`, `XmlAttribute`, `XmlText`.
  - `XmlParser::Parse(span/reader) -> Expected<XmlDocument, ParseError>`.
  - `XmlReader` event API: `OnStartElement`, `OnAttribute`, `OnText`, `OnEndElement`, `OnCData`.
- Parsing rules:
  - Well-formed XML 1.0 subset: elements, attributes, CDATA, comments, PI.
  - Entity decoding for built-ins and numeric references.
  - Options for whitespace handling, namespace processing, DTD skipping.
- DOM representation:
  - Arena-allocated nodes and attributes; text as `std::string_view` when unescaped.

## Phase 5: XML Performance Pass

- `XmlScanner` with fast tokenization:
  - Vectorized search for `<` and `&` using SIMD or `memchr` fallback.
  - Minimize per-char branching in attribute parsing.
- Optional streaming mode:
  - `XmlReader` supports incremental input with saved parser state.

## Phase 6: Filesystem/Path/Stream Support (If Needed)

- Minimal path abstraction in `include/NGIN/IO/Path.hpp`:
  - Join, normalize, extension, filename; no `std::filesystem`.
- File handle in `include/NGIN/IO/File.hpp` + `src/NGIN/IO/File.cpp`:
  - RAII wrapper, `ReadAll`, `ReadInto(span)`, OS API only.
- Memory-mapped file view in `include/NGIN/IO/FileView.hpp`:
  - Zero-copy parsing from mapped data.
- IO adapters feed parsers (mapped view -> `MemoryReader`).

## Phase 7: Serialization Abstractions

- `include/NGIN/Serialization/Archive.hpp`:
  - `ArchiveMode { Read, Write }`, ADL `Serialize(Archive&, T&)`.
  - `JsonArchive` and `XmlArchive` on top of DOM or streaming reader.
- Keep it opt-in and minimal; avoid macros unless necessary.

## Testing & Benchmarks

- Tests under `tests/Serialization/`:
  - JSON: positive (nested, escapes, numbers), negative (invalid escape, trailing comma).
  - XML: positive (attributes, CDATA, entities), negative (mismatched tags, bad entity).
- Benchmarks under `benchmarks/`:
  - JSON: small/medium/large documents; MB/s and allocation counts.
  - XML: large document, heavy attributes, text-heavy payloads.

## Open Questions

- JSON non-standard features: default to strict RFC; enable comments/trailing commas only via `ParseOptions`.
- XML namespaces/DTD: skip DTDs and treat namespaces as raw prefixes in v1; add full processing later behind options.
- In-situ parsing: allow only when explicitly opted-in with a mutable buffer; default to non-mutating parsing.
