# NGIN.Base Modularization Plan

This plan is the work we should do before continuing large Crypto expansion. The goal is to make `NGIN.Base`
smaller, easier to build, easier to package, and easier to consume without losing momentum on the current source tree.

The outside feedback is directionally right: `NGIN.Base` is carrying too much subsystem-specific code and its
`CMakeLists.txt` is doing too many jobs in one file. The correction should be staged. First make the current tree
cleanly describe its ownership boundaries, then split packages and repositories once those boundaries are testable.

## Recommendation

Do not keep growing `NGIN.Base` as one large general-purpose library. It is already becoming a foundation library plus
Crypto plus Net plus Serialization plus build/package policy. That makes dependencies, options, tests, and install
exports harder than they need to be.

Also do not split everything immediately. The current Crypto work has active backend dispatch, tests, CMake options,
and source files in flight. A physical split before the build is modular would create churn without improving user
value.

The practical path is:

1. Clean `NGIN.Base` CMake and source ownership inside the current tree.
2. Introduce explicit library boundaries and package names.
3. Move Crypto and Net into separate first-party libraries after the boundaries are already enforced by targets/tests.
4. Keep each final repository/package CMake boring and conventional.

## Final Package Shape

| Package | Public target | Depends on | Owns | Does not own |
|---|---|---|---|---|
| `NGIN.Base` | `NGIN::Base` | Standard library, platform system libs only | Core utilities, containers, allocators, text, time, execution primitives, basic IO/path/filesystem if kept foundational | Crypto primitives, external crypto engines, sockets, TLS, JWT, PKI |
| `NGIN.Crypto` | `NGIN::Crypto` | `NGIN::Base`, platform crypto APIs where appropriate | Errors/results for crypto, secure memory, constant-time helpers, random, encodings used by crypto, hash/MAC/KDF/AEAD/signature/key agreement contracts | TLS sockets, certificate validation flow, application auth policy |
| `NGIN.Crypto.OpenSSL` or `NGIN.Crypto` option | `NGIN::Crypto` backend composition | OpenSSL Crypto | OpenSSL backend adapter and known-answer tests | Public OpenSSL C++ types |
| `NGIN.Net` | `NGIN::Net` | `NGIN::Base` | TCP/UDP sockets, listeners, network drivers, byte streams | TLS policy, crypto algorithms |
| `NGIN.Net.Tls` | `NGIN::Net::Tls` or `NGIN::NetTls` | `NGIN::Net`, `NGIN::Crypto`, backend TLS package | TLS contexts, handshakes, ALPN, cert validation flow | Low-level crypto primitive implementation |
| `NGIN.Serialization` | `NGIN::Serialization` | `NGIN::Base` | JSON/XML/archive APIs | Networking, crypto, persistence policy |
| `NGIN.Security` | `NGIN::Security` | `NGIN::Base`, `NGIN::Crypto` | Password policy, token validation policy, JWT/PASETO application rules | Primitive crypto |
| `NGIN.Pki` | `NGIN::Pki` | `NGIN::Base`, `NGIN::Crypto` | PEM/DER/X.509/certificate stores and trust abstractions | TLS sockets |

`NGIN.Base` may remain a compatibility aggregate for a short transition, but the long-term user experience should be:

```cmake
find_package(NGINBase REQUIRED)
find_package(NGINCrypto REQUIRED)
find_package(NGINNet REQUIRED)

target_link_libraries(MyApp PRIVATE
    NGIN::Base
    NGIN::Crypto
    NGIN::Net
)
```

## CMake Principles

- Prefer one normal library target per package: `add_library(NGINBase ...)`, `add_library(NGIN::Base ALIAS NGINBase)`.
- Prefer standard `BUILD_SHARED_LIBS` for new packages.
- Keep existing static/shared compatibility options only during transition.
- Avoid a large central `ngin_add_library()` framework for now. Shared helper files are fine, but each package should
  remain understandable without learning a private build language.
- Put dependency discovery in package config files using `find_dependency()`.
- Keep external dependencies out of `NGIN.Base`. OpenSSL, BoringSSL, and libsodium belong in `NGIN.Crypto` package
  composition, not in the foundation library.
- Backend-specific tests should skip when unavailable, not silently run a different backend.
- Generated build trees remain generated. Do not edit them to implement this cleanup.

## Phase 1: Make Current Base CMake Readable

No source behavior changes in this phase. Keep current targets and options working.

| Done | Task | Files | Result |
|---|---|---|---|
| [x] | Create focused CMake include files | `cmake/NGINBaseOptions.cmake`, `cmake/NGINBaseWarnings.cmake`, `cmake/NGINBasePlatform.cmake`, `cmake/NGINBaseSources.cmake`, `cmake/NGINBaseInstall.cmake` | Top-level `CMakeLists.txt` becomes an orchestration file. |
| [x] | Group sources by ownership | `cmake/NGINBaseSources.cmake` | Separate variables for Core, IO, Execution, Serialization, Crypto, Net. |
| [x] | Move platform/system library probing out of top-level | `cmake/NGINBasePlatform.cmake` | Windows, Apple, Linux, atomic, and fiber platform rules are localized. |
| [x] | Move install/export/package config logic out of top-level | `cmake/NGINBaseInstall.cmake` | Install behavior is easier to compare with sibling packages. |
| [x] | Keep compatibility options stable | `CMakeLists.txt` and option include | Existing `NGIN_BASE_BUILD_STATIC`, `NGIN_BASE_BUILD_SHARED`, tests, examples, benchmarks, OpenSSL option still configure. |
| [x] | Add configure smoke tests | build presets or manual commands | Default and OpenSSL-enabled configure still work. |

Acceptance criteria:

- `Dependencies/NGIN/NGIN.Base/CMakeLists.txt` is short enough to review quickly.
- No public target name changes.
- Default build and OpenSSL-enabled Crypto tests still pass.

## Phase 2: Clean Test CMake

The current test CMake works, but it mixes test discovery, dependency fetching, sanitizer policy, CPU flags, and target
creation.

| Done | Task | Files | Result |
|---|---|---|---|
| [x] | Split test helpers | `tests/cmake/NGINBaseTestHelpers.cmake` | Test target creation is reusable and reviewable. |
| [x] | Rename test-only sanitizer option | `tests/CMakeLists.txt` | Avoid redefining or shadowing top-level `NGIN_BASE_ENABLE_ASAN`. |
| [x] | Remove unconditional `-mavx2` | `tests/CMakeLists.txt` | Tests build on machines without AVX2 unless a specific SIMD test opts in. |
| [x] | Keep one-executable-per-file behavior | `tests/CMakeLists.txt` | Existing focused `Crypto_AeadTests`, `Crypto_BackendTests`, etc. remain available. |
| [x] | Add domain labels | `tests/CMakeLists.txt` | CTest can run `Crypto`, `Net`, `IO`, and `Serialization` subsets cleanly. |

Acceptance criteria:

- Focused CTest labels exist.
- No architecture-specific flags leak into every test executable.
- Test configuration remains deterministic when Catch2 is already available or fetched.

## Phase 3: Expose Internal Library Boundaries

This phase introduces boundaries before moving files between repositories.

| Done | Task | Files | Result |
|---|---|---|---|
| [x] | Define internal source components | CMake source include | Components: `Base.Foundation`, `Base.IO`, `Base.Execution`, `Base.Serialization`, `Base.Crypto`, `Base.Net`. |
| [x] | Add optional internal object libraries or interface group targets | CMake source/target include | The build can validate component dependency direction without public package churn. |
| [x] | Prevent wrong dependency direction | CMake target links and includes | Crypto can depend on Base foundation; Base foundation cannot depend on Crypto or Net. |
| [x] | Document ownership boundaries | `docs/BaseModularizationPlan.md`, `docs/README.md` | Contributors know where new APIs belong. |
| [x] | Add include smoke tests per component | `tests/...` | Includes can be checked by domain. |

Acceptance criteria:

- We can tell from CMake which sources belong to Base, Crypto, Net, and Serialization.
- No subsystem-specific source is hidden inside an undifferentiated `NGIN_BASE_CORE_SOURCES` blob.
- User-facing behavior remains unchanged.

## Phase 4: Create Separate Package Targets In Tree

Only start this after Phase 1-3 are stable.

| Done | Task | Files | Result |
|---|---|---|---|
| [x] | Create `NGINCrypto` target in the current tree or sibling tree | CMake and Crypto source lists | `NGIN::Crypto` can be built and tested separately from the full Base aggregate. |
| [x] | Move OpenSSL option to Crypto naming | CMake | Prefer `NGIN_BASE_CRYPTO_WITH_OPENSSL`; keep `NGIN_BASE_CRYPTO_OPENSSL` and `NGIN_CRYPTO_WITH_OPENSSL` as compatibility aliases. |
| [x] | Create `NGINNet` target | CMake and Net source lists | `NGIN::Net` depends on `NGIN::Base`, not vice versa. |
| [x] | Decide whether Serialization leaves Base | CMake and docs | Serialization leaves the long-term Base foundation; a transitional `NGIN::Serialization` target now exists. |
| [ ] | Update package config files | `cmake/*Config.cmake.in` | `find_package(NGINCrypto)` calls `find_dependency(NGINBase)`. |

Acceptance criteria:

- A consumer can link only `NGIN::Base` without linking Crypto, OpenSSL, sockets, or TLS work.
- A consumer can link `NGIN::Crypto` and get `NGIN::Base` transitively.
- Existing workspace package wrappers can still consume the libraries.

Current status: transitional in-tree split targets exist as `NGIN::Crypto`, `NGIN::Net`, and `NGIN::Serialization`.
They are `EXCLUDE_FROM_ALL` and intentionally not exported yet while `NGIN::Base` remains the compatibility aggregate.
Tests for those domains link the split aliases when available, which validates the future target names without breaking
current consumers. The next breaking/topology step is to stop compiling these subsystem sources into the Base aggregate
and then export the split packages through their own package configs or wrappers.

## Phase 5: Physical Split

Only do this when package targets already work and tests prove the boundaries.

| Done | Task | Destination | Result |
|---|---|---|---|
| [ ] | Move Crypto source tree | `Dependencies/NGIN/NGIN.Crypto` | Crypto has its own `CMakeLists.txt`, docs, tests, examples, benchmarks. |
| [ ] | Move Net source tree | `Dependencies/NGIN/NGIN.Net` | Net has its own `CMakeLists.txt`, docs, tests, examples, benchmarks. |
| [ ] | Add or update package wrappers | `Packages/NGIN.Crypto`, `Packages/NGIN.Net` | Workspace composition uses package metadata, not dependency internals. |
| [ ] | Retire Base compatibility aggregate | after migration | `NGIN.Base` is a real foundation library, not a bundle of unrelated subsystems. |

Acceptance criteria:

- Each package has one normal exported target and one package config.
- Each package can be configured and tested independently.
- Workspace-level package restore/build still composes them.

## Immediate Work Order

We should implement these before adding more Crypto algorithms:

1. Extract `NGIN.Base` CMake into focused include files while preserving current behavior.
2. Clean `tests/CMakeLists.txt`, especially sanitizer option shadowing and unconditional AVX2.
3. Group source lists by ownership and document the intended package for each group.
4. Add CTest labels for Crypto, Net, IO, Serialization, and Base foundation tests.
5. Re-run default and OpenSSL-enabled Crypto test flows to prove the cleanup did not break the current renewal work.

After that, continue Crypto implementation from a cleaner base. The next split step should be creating an in-tree
`NGIN::Crypto` package target, not immediately moving every file to a new repository.

## Verification Plan

Use targeted verification after each phase:

```bash
cmake -S Dependencies/NGIN/NGIN.Base -B Dependencies/NGIN/NGIN.Base/build/default-dev \
  -DNGIN_BASE_BUILD_TESTS=ON \
  -DNGIN_BASE_BUILD_EXAMPLES=OFF \
  -DNGIN_BASE_BUILD_BENCHMARKS=OFF

cmake --build Dependencies/NGIN/NGIN.Base/build/default-dev --target Crypto_AeadTests Crypto_BackendTests
ctest --test-dir Dependencies/NGIN/NGIN.Base/build/default-dev --output-on-failure -L Crypto
```

For OpenSSL:

```bash
cmake -S Dependencies/NGIN/NGIN.Base -B Dependencies/NGIN/NGIN.Base/build/crypto-openssl-dev \
  -DNGIN_BASE_BUILD_TESTS=ON \
  -DNGIN_BASE_BUILD_EXAMPLES=OFF \
  -DNGIN_BASE_BUILD_BENCHMARKS=OFF \
  -DNGIN_BASE_CRYPTO_WITH_OPENSSL=ON

cmake --build Dependencies/NGIN/NGIN.Base/build/crypto-openssl-dev --target Crypto_AeadTests Crypto_BackendTests
ctest --test-dir Dependencies/NGIN/NGIN.Base/build/crypto-openssl-dev --output-on-failure -L Crypto
```

Always finish CMake cleanup with:

```bash
git diff --check
```

## Non-Goals

- Do not rewrite all sibling packages into a shared CMake framework in this step.
- Do not change public C++ APIs just to move build files around.
- Do not make OpenSSL a required dependency of `NGIN.Base`.
- Do not move TLS into Crypto.
- Do not edit generated build directories.
- Do not remove compatibility aliases until package wrappers and examples have been updated.
