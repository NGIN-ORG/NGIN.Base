# Meta and hashing

The Meta area supplies compile-time identity and callable inspection:

- `TypeId`, `SymbolId`, and `ReflectionIdentity` provide stable NGIN-facing
  identifiers
- `TypeName` exposes compiler-derived diagnostic names
- `FunctionTraits`, `TypeTraits`, and `EnumTraits` support constrained generic
  code

The Hashing area contains FNV hashing plus CRC/checksum facilities. Use FNV for
stable non-cryptographic identifiers and hash tables. Use CRC/checksums for
accidental-corruption detection. Neither is a cryptographic integrity primitive;
use the Crypto component when an attacker can choose input.

Do not persist compiler-derived `TypeName` text as a wire or storage identity.
Persist an explicit schema or symbol identifier instead.
