# NGIN.Base serialization

This component provides strict JSON and XML document and event parsers for NGIN
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
`TryObject`) and query views (`ObjectView::Find`) in normal code. `Kind()` is
the single storage-kind query. Checked access returns `std::nullopt` for a
mismatch; it never substitutes a default value. `TryDouble()` converts any
numeric kind and can lose integer precision.

The redundant `GetType`, `As*`, and `FindPtr` APIs have been removed. Use `Kind`,
the matching `Try*` accessor, and `Find` respectively. Removing `FindPtr` also
removes the eager array of value views previously retained for every node.

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

## Query and storage costs

JSON arrays and XML attribute ranges provide constant-time subscripting. XML
`Children()` is a forward range: use a range-for loop, with constant-time sibling
increments and constant-time unfiltered `Size()`. Child subscripting has been
removed because repeated indexing walked the sibling chain repeatedly.
`Children(name)` filters during traversal; its `Size()` scans the siblings.

Objects and attribute lists with fewer than 16 entries use a linear name scan
without allocating an index. Wider containers build compact hash indexes for
duplicate detection and retain them for queries. Lookup performs a binary search
among indexed containers, then expected constant probe work plus key hashing.
Hash collisions can degrade this; there is no worst-case constant-time guarantee.
Iteration always retains source order. `Preserve` JSON lookup returns the first
matching member.

JSON uses compact source offsets, a single reusable pending-member stack, and
sibling links while parsing arrays. It creates views on demand. XML retains its
compact sibling tables. Indexes trade additional memory in wide XML attribute
lists for faster parsing and repeated lookup. See
[measurements and tradeoffs](../../../docs/SerializationPerformance.md).

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

`JSON::IncrementalEventParser` and `XML::IncrementalEventParser` accept arbitrary
`Feed()` chunks followed by `Finish()`:

- `Feed()` consumes complete tokens and invokes callbacks immediately. It returns
  `EventProduced` if callbacks ran, otherwise `NeedMoreInput`.
- `Finish()` consumes any final token and checks that the document is complete.
  Each result's `eventsProduced` counts callbacks during that call, including
  callbacks before a parse error or handler rejection.
- A later error does not retract callbacks already delivered. Consumers requiring
  atomic updates must stage their own changes until `Finish()` succeeds.
- Repeated successful `Finish()` is idempotent; errors remain stable until
  `Reset()`. A new document requires `Reset()`.

Completed input is released. Retained storage consists of the largest unfinished
token, reusable decoding scratch, and open-container state. JSON duplicate checking
also retains decoded keys of open objects. Memory is bounded by these structures
and `maxTotalMemoryBytes`, rather than total stream length. A very large single
string, XML text run, or start tag still requires correspondingly large storage.
`BufferedBytes()` reports pending token bytes; `MemoryCommitted()` includes
retained dynamic parser and scratch capacity, excluding fixed object storage.

`KeepLast` JSON is the exception: it buffers the document until `Finish()` and
parses it once through the DOM path, since later duplicate keys can replace
previous values. Select `Reject`, `Preserve`, or `KeepFirst` for early callbacks.

Input, node/member, and decoded-byte limits are cumulative across feeds. Source
spans and diagnostics use global byte offsets and `ParseOptions::source`.
Event text is valid only during its callback and must be copied if retained.
`Reset()` clears state and counters while retaining reusable capacity. Handler
exceptions propagate; reset the parser before reuse after an exception.

Event parsers also accept `std::string_view` directly, with source identity
provided in `ParseOptions::source`.

Handlers satisfy a concept and return `EventAction`; delivery uses a function
pointer adapter without a virtual handler interface.

JSON events are emitted directly from the parser without constructing a DOM
for `Reject`, `Preserve`, and `KeepFirst` duplicate-key policies. `KeepLast`
requires object buffering to suppress a previously encountered value and
therefore uses the semantic DOM path. For the direct event path, start-container,
end-container, and key spans cover individual source tokens. The `KeepLast` DOM
path retains its complete-value/member spans.

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
  `benchmarks/XmlBenchmarks.cpp`; before/after workloads: `benchmarks/SerializationWorkloads.cpp`

The fuzz targets are enabled with `NGIN_BASE_BUILD_FUZZERS=ON` on Clang.
