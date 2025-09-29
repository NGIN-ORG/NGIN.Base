# NGIN Utilities

## Any

- **Purpose:** Provide a small-buffer-optimized, header-only alternative to `std::any` with predictable type identifiers and allocator customization.
- **Key traits:** 32-byte inline storage by default, FNV-1a hashed type identifiers via `NGIN::Meta::TypeName`, allocator pluggability, and helpers for safe downcasts (`TryCast`, `Cast`) plus lightweight visitation via `Any::View`.
- **Usage Example:**

  ```cpp
  #include <NGIN/Utilities/Any.hpp>

  NGIN::Utilities::Any<> box;
  box.Emplace<std::string>("hello");

  box.Visit([](NGIN::Utilities::Any<>::View view) {
      auto& str = view.Cast<std::string>();
      str += " world";
  });
  ```
- **Testing Notes:** See `tests/Utilities/Any.cpp` for coverage of SBO behavior, heap fallback, move-only semantics, and error pathways.

## StringInterner

- **Purpose:** Store unique strings and hand out stable `std::string_view` handles with identifiers for fast comparisons.
- **Key traits:** Header-only, allocator-parameterized, optional threading policy template (e.g. `StringInterner<SystemAllocator, std::mutex>`), page-backed storage with geometric growth, and FNV-1a hashing for bucket lookup. Provides lightweight counters via `GetStatistics` / `ResetStatistics`.
- **Usage Example:**

  ```cpp
  #include <NGIN/Utilities/StringInterner.hpp>

  NGIN::Utilities::StringInterner<> interner;
  auto nameId = interner.InsertOrGet("Component");
  std::string_view stored = interner.View(nameId);

  auto stats = interner.GetStatistics();
  // stats.inserted, stats.lookupHits, stats.totalBytesStored, ...
  ```
- **Testing Notes:** See `tests/Utilities/StringInterner.cpp` for deduplication, allocator instrumentation, and lookup failures.
