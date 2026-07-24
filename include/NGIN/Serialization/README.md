# NGIN.Base serialization

This component provides strict, in-memory JSON and XML facilities for NGIN
manifests, runtime metadata, editor protocols, and tool-driver JSONL.

The public contract is intentionally format-specific. There is no generic
archive layer.

## Ownership

Every parse call states its lifetime model:

- `JSON::Parse(OwnedTextBuffer)` and `XML::Parse(OwnedTextBuffer)` return
  self-contained, movable documents.
- `ParseBorrowed(BorrowedTextView, ParseScratch&)` returns a
  `BorrowedDocument`. Views are valid only while the input, scratch object, and
  document remain alive, and until that scratch object is reset or reused.
  `ParseScratch::Reset()` retains capacity.
- `JSON::ParseInSitu(MutableTextBuffer)` explicitly permits string decoding in
  the owned mutable source. It still returns a self-contained `Document`.
- `XML::ParseSyntax(OwnedTextBuffer)` returns the lossless syntax document used
  by formatter-style tools.

Passing a bare `std::string_view` is deliberately not an owning parse.

Documents own compact indexed node/member/child tables. `ValueView`,
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

Handlers are concepts rather than virtual interfaces and return
`EventAction`. Borrowed unescaped values follow input lifetime; decoded values
are valid only for the current handler invocation and must be copied if
retained.

JSON events are emitted directly from the parser without constructing a DOM
for `Reject`, `Preserve`, and `KeepFirst` duplicate-key policies. `KeepLast`
requires object buffering to suppress a previously encountered value and
therefore uses the semantic DOM path. Start-container, end-container, and key
event spans cover their individual source tokens.

XML events are also emitted directly without constructing a semantic document
or the document's cached `ElementView` table. Start-element spans cover the
opening `<name` token, attribute spans cover `name="value"`, and end-element
spans cover either `/>` or `</name>`. Text values are entity-decoded and
line-ending-normalized; comments and processing instructions inside elements
are emitted only when `TriviaPolicy::Preserve` is selected.

JSONL consumers should keep one `ParseScratch` per stream and call
`JSON::ParseBorrowed` for each complete line.

## Building and writing

`JSON::Builder` and `XML::Builder` construct immutable semantic documents.
`JSON::Writer` and `XML::Writer` serialize document views.

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
