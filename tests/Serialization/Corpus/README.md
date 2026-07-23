# Serialization correctness corpus

These UTF-8 fixtures define the default strict profiles exercised by
`CorpusTests.cpp`.

- `json/valid`: RFC 8259 documents, numeric boundaries, and NGIN-shaped events.
- `json/invalid`: extensions disabled by default, duplicate names, malformed
  escapes, and trailing data.
- `xml/valid`: the supported XML 1.0 semantic subset and NGIN manifest shapes.
- `xml/invalid`: document-structure, entity, attribute, comment, and DTD
  security failures.

The corpus is authored for this repository and has no external licensing
requirements. Binary UTF-8 edge cases remain generated in focused tests and
fuzzers because source-control text normalization can alter those bytes.
