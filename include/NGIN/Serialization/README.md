# Serialization

Core JSON/XML parsing utilities for NGIN.Base.

## Components

- `Core/` – shared parse infrastructure (errors, cursor).
- `JSON/` – JSON DOM and streaming parser.
- `XML/` – XML DOM and streaming parser.

## Notes

- Parsers operate on `std::span<const NGIN::Byte>` or `std::string_view` inputs.
- No `std::istream` or `std::filesystem` dependencies.
- DOMs allocate from a linear arena sized off input length (configurable).
- Strings may reference the input buffer; keep it alive or use the reader overloads.
