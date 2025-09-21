# Library Build Plan: Transition NGIN.Base from Header-Only to Static/Shared Variants

## Objectives
- Provide first-class static (`NGINBaseStatic`) and shared (`NGINBaseShared`) library builds suitable for runtime features such as threading, IO, and allocator back-ends.
- Enable subsystems that require compiled translation units (threading primitives, I/O back-ends, allocators, platform shims) to centralize logic within `src/` rather than duplicating code across consumers.
- Preserve ABI/ODR safety by designing explicit symbol export controls and minimizing global state.
- Maintain compatibility with current consumers (reflection, tests, examples) and keep CMake package exports coherent.

## Current State & Constraints
- `CMakeLists.txt` defines `NGIN.Base` as an `INTERFACE` library by default, with an OBJECT variant gated behind `NGIN_BASE_DEVELOPMENT_MODE`.
- All production code resides in headers; only tests/examples use `.cpp` files. No symbol export macros or compiled modules exist today.
- Consumers rely on `NGIN::Base` alias; packaging installs headers only.
- Need to support C++23, multi-platform (Windows, Linux, macOS) configurations.

## Strategic Considerations
- **Configuration Surface**: Provide options to select linkage type (`NGIN_BASE_BUILD_STATIC`, `NGIN_BASE_BUILD_SHARED`), supporting static-only, shared-only, or dual builds.
- **Source Layout**: Create `src/` tree mirroring `include/NGIN` namespaces, grouping functionality that benefits from compiled TUs (e.g., `src/Utilities/`, `src/Async/`, `src/IO/`). Document migration criteria to avoid arbitrary `.cpp` sprawl.
- **Symbol Visibility**: Define `NGIN_BASE_API` macro (platform-aware `__declspec(dllexport)`/`__attribute__ ((visibility("default")))`) to annotate exported types/functions. Provide `NGIN_BASE_LOCAL` for hidden symbols.
- **Threading & IO Hooks**: Identify components that will require platform abstractions (e.g., fibers, event loops, file handles) and plan for conditional compilation plus dependency injection of OS-level APIs.
- **Allocator & Static Storage Discipline**: Keep global singletons out of shared code; prefer internal singletons with function-local statics guarded by `std::call_once` if needed.
- **Testing & CI Impact**: Ensure tests link against both static and shared variants; adjust Catch2 setups to link with the chosen target.
- **Packaging**: Update install/export logic to ship built archives (`.a`/`.lib`, `.so`/`.dll`), versioned with existing CPack configuration, and generate config entries exposing all targets.

## Implementation Phases
1. **Design & Infrastructure Setup** *(Completed)*
   - Draft `NGIN_BASE_LIBRARY_MODE` decision matrix (static, shared, hybrid) and update documentation (`README`, `AGENTS.md`) with new options.
   - Introduce `src/` directory scaffold; move `NGINBaseDummy.cpp` or create placeholder `CoreInit.cpp` to verify build.
   - Add `NGIN_BASE_API` visibility header under `include/NGIN/Defines.hpp` or new `include/NGIN/BaseAPI.hpp` with platform macros.

2. **CMake Refactor** *(Completed)*
   - Replace current `if(NGIN_BASE_DEVELOPMENT_MODE)` with unified configuration:
     - Always define an OBJECT library (`NGIN.Base.Object`) containing common sources.
     - Conditionally create STATIC and SHARED libraries linking the OBJECT target.
   - Expose cache options for static/shared builds, defaulting to building all three targets (interface + static + shared) unless explicitly disabled.
   - Ensure `NGIN::Base` alias resolves to the appropriate target (shared preferred when available) while also exporting named targets (`NGIN::Base::Static`, `NGIN::Base::Shared`).
   - Update install rules to copy archives and export target properties (RPATH, versioning, IMPORTED configurations).

3. **Source Migration Criteria & Pilot Modules**
   - Identify candidate features to move into `.cpp` files:
     - Threading primitives needing OS handles (e.g., `Async`, `Utilities::ThreadName`).
     - IO back-ends (file descriptors, sockets).
     - Platform-specific hashing or crypto seeds.
   - For each candidate, document invariants, required headers, and API impact. Ensure translation units include necessary inline wrappers for header compatibility.
   - Implement at least one pilot module (e.g., `Utilities::Threading`) to validate linkage, symbol visibility, and packaging. *(Fiber migrated as initial pilot.)*

4. **Testing & Validation**
   - Update test CMake to link against new targets (`NGIN::Base::Static` or `NGIN::Base::Shared`) depending on build options.
   - Add CI presets compiling static-only, shared-only, and dual-build configurations to detect regressions.
   - Provide smoke tests ensuring that downstream consumers link successfully against the selected library mode.

5. **Documentation & Migration Guidance**
   - Extend `README.md` with build matrix instructions and environmental variables for runtime libraries (Windows DLL search path, etc.).
   - Create `docs/LibraryModes.md` or augment new `LibraryPlan.md` with status tracking.
   - Communicate required consumer changes (e.g., linking guidelines, macro usage).

## Risk Mitigation & Follow-Ups
- Introduce continuous checks for exported symbol lists to avoid ABI drift (optional `abi-compliance-checker` later).
- Evaluate need for per-component namespaces to avoid symbol collisions when compiled.
- After initial rollout, audit existing headers for functions that should migrate into sources for performance or encapsulation.
- Coordinate with dependent repos (e.g., `NGIN.Reflection`) to update dependency instructions once new targets land.
