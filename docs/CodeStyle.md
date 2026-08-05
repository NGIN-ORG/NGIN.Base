# Public header and documentation style

NGIN.Base public headers should make the API discoverable from filenames,
declarations, and generated Doxygen output without requiring implementation
knowledge.

## Header ownership

- A header named after a public class, struct, enum, concept, or alias owns that
  declaration. For example, `TicketLock.hpp` owns `TicketLock` and
  `ResolvedAddress.hpp` owns `ResolvedAddress`.
- Closely coupled nested implementation types may remain with their owner.
  Put reusable implementation machinery under a `detail` namespace and, when
  substantial, a `detail/` header.
- A plural or family header such as `Algorithms.hpp` may intentionally group a
  small set of symmetric value types. Its filename and file comment must make
  that grouping explicit.
- Aggregate and compatibility headers contain includes, not unrelated
  declarations. New code should include the canonical type-named header.
- Avoid generic filenames such as `Types.hpp`, `Common.hpp`, or `Utilities.hpp`
  when a declaration has a stable public name and can stand alone.

## Doxygen contract

Every public header starts with `@file` and `@brief`. Every public type,
concept, alias, free function, and non-trivial member has a concise Doxygen
summary.

Document information that is not obvious from the signature:

- ownership and lifetime;
- invalidation and thread-safety rules;
- units, bounds, and sentinel values;
- error, cancellation, and exception behavior;
- `@tparam`, `@param`, and `@return` semantics where names do not fully explain
  the contract;
- enum values and public data members when their meaning or policy matters.

Do not narrate implementation steps. Private comments should explain
invariants, algorithms, or platform constraints rather than restating code.

## Compatibility headers

When correcting an established include path, retain a small forwarding header
for source compatibility unless the change is intentionally breaking. Mark it
as a compatibility include and migrate first-party code to the canonical
header immediately.

## Review checklist

Before adding or changing a public declaration:

1. Confirm the filename identifies its primary public declaration.
2. Confirm the header compiles independently.
3. Confirm public declarations and non-obvious members have Doxygen comments.
4. Confirm implementation helpers are nested or in `detail` scope.
5. Confirm umbrella, component ownership, tests, and examples use canonical
   include paths.
