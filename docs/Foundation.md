# Foundation and primitives

The Foundation component owns the low-level, provider-free types used by every
other NGIN.Base component. Start with `NGIN/NGIN.hpp` when a source file needs
several Foundation facilities; use individual headers in public interfaces and
when only one facility is required.

The central contracts are:

- `NGIN/Primitives.hpp` for fixed-width NGIN scalar aliases
- `NGIN/Defines.hpp` for visibility and platform declarations
- `NGIN/Utilities/Expected.hpp` for value-or-error results
- `NGIN/Exceptions/Exception.hpp` for the small exception hierarchy
- `NGIN/Text/String.hpp` and `NGIN/Containers/Vector.hpp` for allocator-aware
  owning values

`NGIN/NGIN.hpp` deliberately excludes Execution, IO, Net, Serialization, and
Crypto. Their focused umbrellas keep optional subsystem dependencies visible.
Headers below a lowercase `detail/` directory are implementation contracts,
not direct application entry points.

All public contract headers compile independently in
`NGINBasePublicHeaderChecks`.
