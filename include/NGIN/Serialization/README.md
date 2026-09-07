# NGIN.Base serialization

This component provides strict, in-memory JSON and XML facilities for NGIN
manifests, runtime metadata, editor protocols, and tool-driver JSONL.

The public contract is intentionally format-specific. There is no generic
archive layer.

## Ownership

Use `JSON::Parse(text)` or `XML::Parse(text)` for ordinary parsing:

```cpp
auto document = JSON::Parse(R"({"name":"NGIN","count":3})");
```

Both accept `std::string_view`, including literals, `std::string`, and existing
views. The parser copies the input into a self-contained, movable document.
The input is needed only during the call and may then be modified or destroyed.
`XML::ParseSyntax(text)` has the same ownership guarantee and preserves exact
source bytes for formatter-style tools. Input and total-memory limits are
checked before copying; allocation failures return a parse diagnostic.

Set `ParseOptions::source` when spans and diagnostics need a caller-defined
`SourceId`. Every document parse returns an owning document; views borrow only
from that document.

Documents own compact indexed node/member tables. XML child traversal uses
compact sibling IDs rather than a second child-pointer table. `ValueView`,
`ElementView`, and range values are immutable handles into those tables.
Moving a document does not invalidate existing views because the backing state
itself is not relocated.

## JSON contract

The default profile is strict JSON:

- comments and trailing commas are rejected
- duplicate object keys are rejected
- UTF-8 and Unicode escape/surrogate structure are validated
- integers are retained as `Int64` or `UInt64`; they are not routed through
  `double`
- non-integral numbers are retained as finite `Double`

Extensions are explicit in `JSON::ParseOptions`. Duplicate handling supports
`Reject`, `Preserve`, `KeepFirst`, and `KeepLast`.

Use checked access (`TryInt64`, `TryUInt64`, `TryString`, `TryArray`,
`TryObject`) and query views (`ObjectView::Find`) in normal code.

## XML contract

The semantic parser implements a secure XML 1.0-oriented profile:

- exactly one document element is required
- element names and end tags must match
- attributes must be quoted and unique
- predefined and numeric entity references are decoded and validated
- CR/LF normalization is applied to semantic text
- malformed comments, CDATA, processing instructions, UTF-8, characters, and
  document structure are rejected
- `DOCTYPE` is rejected by default
- the opt-in `AllowWithoutExternalEntities` mode still rejects `SYSTEM` and
  `PUBLIC` external identifiers and does not perform entity expansion

Namespaces are lexically preserved but are not resolved into URI/local-name
pairs in the default semantic profile.

Semantic nodes carry source spans. `ElementView::Attribute`,
`ElementView::Children(name)`, `FirstChild`, and `FirstText` provide
allocation-free queries.

`XML::Parse` preserves the input in `SourceText()` while storing decoded text
separately. Use `ParseSyntax` for formatter/editor round trips.

`ParseSyntax` validates with the same semantic rules while retaining the exact
source and syntax tokens, including declarations, comments, CDATA, processing
instructions, quote choices, whitespace, and line endings. Writing a
`SyntaxDocument` is byte-for-byte lossless.

## Limits and diagnostics

`ParseLimits` bounds input bytes, depth, nodes, members/attributes, decoded
bytes, and total retained memory. Limits are checked with overflow-safe
arithmetic.

Failures return `ParseDiagnostic`, including an error code, byte/line/column
location, primary source span, and—where applicable—a related span such as the
first duplicate name. Event-handler aborts retain the handler's numeric
`consumerContext`.

## Events

`JSON::EventParser::ParseContiguous` and
`XML::EventParser::ParseContiguous` deliver typed events from one complete
contiguous input. Their names intentionally do not claim chunked input.

`JSON::IncrementalEventParser` and `XML::IncrementalEventParser` accept an
arbitrary sequence of `Feed()` chunks followed by `Finish()`. Their state
machine is deliberately transactional:

- accepting: `Feed()` retains bytes and returns `NeedMoreInput` without calling
  the handler
- finishing: `Finish()` validates the complete retained document, then emits
  its events exactly once and returns `Complete` with `eventsProduced`
- complete: repeated `Finish()` is idempotent; a new document requires
  `Reset()`
- failed: the diagnostic remains stable until `Reset()`

Validation-before-emission means a token split across chunks cannot cause a
duplicate callback when more bytes arrive. It also preserves the contiguous
parser's duplicate-key, trivia, namespace, `DOCTYPE`, UTF-8, escape, entity,
and diagnostic behavior. The common result enum reserves `EventProduced` for
future parsers that can safely commit partial events; these transactional
parsers complete emission during `Finish()`.

The retained source counts against `maxTotalMemoryBytes`, and
`maxInputBytes`/total-memory accounting spans all feeds. Byte offsets and
`SourceId` values are relative to the whole accumulated source. Event values
are callback-scoped—including values assembled from multiple chunks—and must
be copied if retained. `Reset()` keeps source and scratch capacity, making one
parser reusable for complete JSONL records.

Event parsers also accept `std::string_view` directly, with source identity
provided in `ParseOptions::source`.

Handlers are concepts rather than virtual interfaces and return
`EventAction`. Borrowed unescaped values follow input lifetime; decoded values
are valid only for the current handler invocation and must be copied if
retained.

JSON events are emitted directly from the parser without constructing a DOM
for `Reject`, `Preserve`, and `KeepFirst` duplicate-key policies. `KeepLast`
requires object buffering to suppress a previously encountered value and
therefore uses the semantic DOM path. Start-container, end-container, and key
event spans cover their individual source tokens.

XML events are also emitted directly without constructing a semantic document.
Start-element spans cover the
opening `<name` token, attribute spans cover `name="value"`, and end-element
spans cover either `/>` or `</name>`. Text values are entity-decoded and
line-ending-normalized; comments and processing instructions inside elements
are emitted only when `TriviaPolicy::Preserve` is selected.

JSONL consumers may keep one incremental parser and `ParseScratch` per stream,
call `Finish()` for each complete line, and `Reset()` before the next record.
When line framing already provides contiguous records, use `JSON::Parse` for
a document or `EventParser::ParseContiguous` for synchronous event delivery.

## Building and writing

`JSON::Builder` and `XML::Builder` construct immutable semantic documents.
`JSON::Writer` and `XML::Writer` serialize document views.

An XML node has one parent. `XML::Builder` rejects duplicate child handles,
reattaching an already-owned child, and finishing an attached node as the
document root. This keeps the semantic tree compact and makes child iteration
allocation-free.

For directly authored output, use `JSON::StreamWriter` or
`XML::StreamWriter` with the minimal `TextSink` adapter:

```cpp
std::string output;
NGIN::Serialization::JSON::StreamWriter writer{
    NGIN::Serialization::MakeTextSink(output)};

writer.BeginObject();
writer.Key("sequence");
writer.UInt64(sequence);
writer.EndObject();
writer.Finish();
```

Stateful writers validate nesting, enforce output/depth limits, and retain
stack capacity across `Reset()`. JSON escaping, exact integer formatting,
non-finite number rejection, XML escaping, duplicate attributes, invalid
comments, and CDATA splitting are centralized here.

## Validation assets

- focused tests: `tests/Serialization/*Tests.cpp`
- checked-in corpus: `tests/Serialization/Corpus/`
- libFuzzer entry points: `tests/Serialization/Fuzz/`
- workload benchmarks: `benchmarks/JsonBenchmarks.cpp` and
  `benchmarks/XmlBenchmarks.cpp`

The fuzz targets are enabled with `NGIN_BASE_BUILD_FUZZERS=ON` on Clang.
