# Crypto Platform And Interop Plan

This plan extends the completed `NGIN::Crypto` renewal baseline into a practical, plug-and-play crypto stack for
application and networked workloads.

The guiding decision remains: NGIN.Base should not implement cryptographic primitives in-house right now. The library
should instead provide excellent API contracts, platform-backed adapters, package-backed provider integration, parser
surfaces, tests, diagnostics, and documentation.

## Goals

- Make useful crypto available on fresh machines without requiring a manual OpenSSL install.
- Keep algorithms backend-backed by platform APIs or approved provider packages.
- Make backend selection understandable at build time and runtime.
- Keep the public API easy for ordinary users while still supporting expert policy and preallocated buffers.
- Add parser-heavy features needed by networked applications, especially PEM, DER, X.509, key formats, and strict token
  formats.
- Make unsupported algorithms and missing providers obvious, actionable, and testable.
- Preserve the boundary that TLS sessions, sockets, ALPN, and handshake flow live under `NGIN::Net`.

## Non-Goals

- No in-house AES, SHA, ChaCha, Ed25519, X25519, RSA, ECDSA, ASN.1, X.509 path validation, JWT, PASETO, or Argon2id
  primitive implementation.
- No silent fallback from a requested algorithm to a weaker algorithm.
- No public backend-specific class hierarchy such as `WindowsCngProvider`.
- No mutable global backend registry.
- No TLS client/server/session API under `NGIN::Crypto`.
- No parser that accepts loose, browser-like, or best-effort input by default.

## Current Baseline

The current Base-owned surface includes:

- `CryptoExpected<T>` and `CryptoError`.
- byte spans, fixed byte wrappers, and `ByteBuffer`.
- secure zeroing, constant-time comparison, `SecureBuffer`, `Secret`, and `SecretView`.
- OS secure random bytes and `EntropySource`.
- strict Hex, Base64, and Base64Url.
- neutral `CryptoContext`, backend info, options, and capability bits.
- opaque secure token generation.
- algorithm contracts and optional OpenSSL-backed wrappers for SHA-2, HMAC, HKDF, PBKDF2, AES-GCM,
  ChaCha20-Poly1305, Ed25519, ECDSA P-256/SHA-256, RSA-PSS/SHA-256, RSA-OAEP/SHA-256, and X25519.

The main weakness is provider availability. A fresh Windows machine currently has platform randomness but does not get
SHA, HMAC, KDF, AEAD, signatures, or key agreement unless the optional OpenSSL path is installed and enabled. That is not
good enough for a platform library.

## Implementation Progress

### 2026-06-22 Backend Policy And Diagnostics Slice

Implemented:

- `BackendPolicy` with `PlatformOnly`, `PackagesOnly`, `PreferPlatformThenPackages`,
  `PreferPackagesThenPlatform`, `RequireFipsCapable`, and `RequireAlgorithmSet`.
- `AlgorithmSet` for startup capability requirements.
- `CreateBestAvailableContext()`, `CreatePlatformContext()`, and `CreatePackageContext("openssl")`.
- richer `BackendInfo` metadata for source, build option, package name, and FIPS capability/validation flags.
- canonical `NGIN_BASE_CRYPTO_WITH_OPENSSL` CMake option, with existing `NGIN_BASE_CRYPTO_OPENSSL` and
  `NGIN_CRYPTO_WITH_OPENSSL` aliases preserved.

Route adjustment:

- The default `CreateContext()` policy is currently `PreferPackagesThenPlatform`, not `PreferPlatformThenPackages`.
  This preserves the existing behavior where an OpenSSL-enabled build gets the full compiled algorithm surface by
  default, while `CreatePlatformContext()` provides the explicit fresh-machine platform-only path. Applications that
  want platform-first selection can now request `BackendPolicy::PreferPlatformThenPackages`.

Still pending from this workstream:

- Apple AES-GCM or asymmetric adapters if a stable C-compatible path is verified.

### 2026-06-22 Backend Algorithm Diagnostics Slice

Implemented:

- `AlgorithmSupportInfo`.
- `CryptoContext::DescribeRandomSupport()`.
- `CryptoContext::DescribeSupport(...)` overloads for hash, MAC, KDF, AEAD, key agreement, asymmetric encryption, and
  signature algorithms.
- stable human-readable reasons for selected-backend unsupported algorithms, including platform-random, CNG, OpenSSL,
  and generic/test backends.

Route adjustment:

- Disabled algorithm reasons are reported from the selected `CryptoContext`, not from context-creation failure objects.
  This keeps the existing compact `CryptoError` result path stable while giving applications and future CLI tooling a
  neutral inspection API.

### 2026-06-22 Context Creation Diagnostics Slice

Implemented:

- `BackendSelectionDiagnostic`.
- `BackendSelectionDiagnostics`.
- `BackendContextSelection`.
- `CreateContextWithDiagnostics()`.
- rejected candidate diagnostics containing backend metadata, compact error code, and a human-readable reason.

Route adjustment:

- `CreateContext()` continues to return the compact `CryptoExpected<CryptoContext>` result for ordinary callers.
  Diagnostic-heavy callers opt in with `CreateContextWithDiagnostics()`, preserving existing API ergonomics and failure
  behavior.

### 2026-06-22 Windows CNG Backend Slice

Implemented:

- `NGIN_BASE_CRYPTO_WITH_CNG`, defaulting to `ON` on Windows and rejected on non-Windows hosts.
- `src/NGIN/Crypto/Backends/CngBackend.win32.cpp`, gated by `NGIN_BASE_CRYPTO_HAS_CNG`.
- `CreatePlatformContext()` can select `cng` as a platform backend when CNG is compiled in.
- CNG capability probes for SHA-256, SHA-512, HMAC-SHA256, HMAC-SHA512, PBKDF2-HMAC-SHA256,
  PBKDF2-HMAC-SHA512, AES-128-GCM, AES-256-GCM, and OS secure random.
- CNG dispatch for hash, HMAC, PBKDF2, AES-GCM seal, and AES-GCM open.
- backend tests recognize both `platform-random` and Windows `cng` platform contexts.

Verification status:

- Linux/non-Windows build and backend/include tests pass with the CNG option left off.
- `.github/workflows/crypto-native-ci.yml` defines a Windows native-crypto job that requires the `cng` provider and
  runs backend, provider-conformance, and certificate tests.
- A Windows MSVC or clang-cl build is still required to prove the `bcrypt.h` implementation compiles and to run the
  shared algorithm vectors against the actual CNG provider.

Still pending from this workstream:

- Successful Windows native-crypto CI execution for CNG.
- Provider conformance vectors running under CNG.
- Mapping CNG authentication failures and invalid-parameter failures into more precise diagnostic details.
- CNG-specific RSA-PSS, RSA-OAEP, and ECDSA after native Windows CI is available.

### 2026-06-22 Provider Conformance Slice

Implemented:

- `tests/Crypto/ProviderConformance.cpp`.
- shared vector headers under `tests/Crypto/ProviderVectors/` for hash, HMAC, KDF, AEAD, Ed25519, ECDSA P-256,
  RSA-PSS, RSA-OAEP roundtrip material, and X25519.
- conformance coverage for best-available, platform-only, and named OpenSSL package contexts.
- supported-provider vector checks for SHA-256, SHA-512, HMAC-SHA256, HMAC-SHA512, HKDF-SHA256,
  PBKDF2-HMAC-SHA256, PBKDF2-HMAC-SHA512, AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305, Ed25519,
  ECDSA P-256/SHA-256, RSA-PSS/SHA-256, RSA-OAEP/SHA-256, and X25519.
- unsupported-path checks for algorithms not claimed by a provider.
- AEAD tamper rejection checks and output-buffer preflight checks through the public API.

Verification status:

- Default Linux provider conformance test passes.
- OpenSSL-enabled Linux provider conformance test passes and runs the algorithm vectors against OpenSSL 3.0.13.

Still pending from this workstream:

- Running the same conformance suite on Windows with `NGIN_BASE_CRYPTO_WITH_CNG=ON`.
- Extending vectors as new providers add XChaCha20-Poly1305, Argon2id, parser-backed key import/export, certificates,
  JWT, PASETO, and additional RSA/ECDSA platform implementations.
- Splitting provider matrix reporting if CI needs one test case per provider/backend.

### 2026-06-22 Build-Time Requirement Slice

Implemented:

- `NGIN_BASE_CRYPTO_REQUIRE_PROVIDER`.
- `NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS`.
- Configure-time validation after provider source selection.
- Semicolon-, comma-, and space-separated requirement lists.
- Clear configure failures for unknown provider names, unavailable providers, unknown algorithm names, and unavailable
  algorithms.

Accepted provider identifiers:

- `platform`
- `platform-random`
- `apple`
- `cng`
- `openssl`
- `boringssl`
- `libsodium`

Accepted algorithm identifiers:

- `random`
- `sha256`
- `sha512`
- `hmac-sha256`
- `hmac-sha512`
- `hkdf-sha256`
- `hkdf-sha512`
- `pbkdf2-sha256`
- `pbkdf2-sha512`
- `aes-128-gcm`
- `aes-256-gcm`
- `chacha20-poly1305`
- `xchacha20-poly1305`
- `ecdsa-p256-sha256`
- `ed25519`
- `rsa-oaep-sha256`
- `rsa-pss-sha256`
- `x25519`
- `argon2id`

Verification status:

- Default Linux configure succeeds when requiring `platform-random` and `random`.
- Default Linux configure fails when requiring unavailable `openssl`.
- Default Linux configure fails when requiring unavailable `sha256`.
- OpenSSL-enabled Linux configure succeeds when requiring `openssl`, `sha256`, `hmac-sha256`, `aes-256-gcm`,
  `ed25519`, `ecdsa-p256-sha256`, `rsa-pss-sha256`, `rsa-oaep-sha256`, and `x25519`.
- Libsodium-enabled Linux configure succeeds when requiring `libsodium`, `xchacha20-poly1305`, `ed25519`, `x25519`,
  and `argon2id`.
- BoringSSL-enabled local Linux configure succeeds against a fresh source-built BoringSSL checkout when requiring
  `boringssl`, `random`, `sha256`, `hmac-sha256`, `hkdf-sha256`, `pbkdf2-sha256`, `aes-256-gcm`,
  `chacha20-poly1305`, `ed25519`, and `x25519`.

Still pending from this workstream:

- Successful remote libsodium provider CI execution on a build that provides real libsodium headers and library.
- Successful remote BoringSSL provider CI execution on a build that provides real BoringSSL headers and `libcrypto`.
- Successful Windows native-crypto CI execution for `NGIN_BASE_CRYPTO_REQUIRE_PROVIDER=cng`.

Route adjustment:

- `boringssl` and `libsodium` are known provider identifiers for requirement parsing and runtime package-context
  diagnostics. `libsodium` now selects an implemented backend when compiled in. `boringssl` now selects an
  OpenSSL-compatible private dispatch path when compiled with a real BoringSSL target. BoringSSL ChaCha20-Poly1305 uses
  BoringSSL's `EVP_AEAD` API because current BoringSSL headers do not expose OpenSSL's `EVP_chacha20_poly1305()`
  cipher helper. `.github/workflows/crypto-provider-ci.yml` now defines a BoringSSL provider-vector job that builds
  BoringSSL from source and configures NGIN.Base against the resulting headers and `libcrypto`; successful remote
  execution remains pending.

### 2026-06-22 PEM Parser Slice

Implemented:

- `include/NGIN/Crypto/Encoding/Pem.hpp`.
- `src/NGIN/Crypto/Encoding/Pem.cpp`.
- strict block boundary parsing with line ending normalization;
- optional label allowlist;
- decoded-size limit per block;
- malformed Base64 rejection through the existing strict Base64 decoder;
- nested block, mismatched end label, outside text, empty payload, and multiple-block policy rejection;
- parser-only return shape containing the PEM label and decoded bytes, with no key or certificate interpretation.

Route adjustment:

- The parser accepts multiple PEM blocks by default because certificate chains and key bundles commonly use adjacent
  blocks. It still rejects nested or mismatched blocks, and callers can set `allowMultipleBlocks = false` when a single
  block is required.

Still pending from this workstream:

- sustained sanitizer/fuzzer runs before broad parser use.

### 2026-06-22 DER Reader/Writer Slice

Implemented:

- `include/NGIN/Crypto/Encoding/Der.hpp`.
- `src/NGIN/Crypto/Encoding/Der.cpp`.
- bounded TLV reader with explicit maximum element size and nesting depth;
- definite-length-only DER parsing;
- rejection of BER/CER indefinite lengths;
- rejection of non-minimal length encodings and high-tag encodings;
- helpers for INTEGER, BIT STRING, OCTET STRING, OBJECT IDENTIFIER, SEQUENCE, and SET;
- writer helpers for the same core DER elements.

Route adjustment:

- The DER layer is intentionally a TLV utility, not a general ASN.1 object model. SET sorting and schema-specific
  constraints stay with higher-level key/certificate formats, where the parser has enough context to enforce them.

Still pending from this workstream:

- sustained sanitizer/fuzzer runs before broad parser use.

### 2026-06-22 Key Format Slice

Implemented:

- `include/NGIN/Crypto/Keys/KeyFormat.hpp`.
- `include/NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp`.
- `include/NGIN/Crypto/Keys/PrivateKeyInfo.hpp`.
- `src/NGIN/Crypto/Keys/KeyFormat.cpp`.
- strict SubjectPublicKeyInfo parse/write over DER;
- strict PKCS#8 PrivateKeyInfo parse/write over DER;
- OID mapping for Ed25519, X25519, ECDSA P-256, and RSA key identifiers;
- raw key-byte preservation without backend import or certificate/trust interpretation.

Route adjustment:

- PKCS#8 parsing currently rejects attributes and versioned public-key extensions. That keeps the initial surface strict
  and avoids silently accepting structures that need more policy. Support can be added later when encrypted keys,
  attributes, or RFC 5958 OneAsymmetricKey public-key fields have concrete consumers.

Still pending from this workstream:

- backend import/export hooks for parsed keys.
- sustained sanitizer/fuzzer runs before broad parser use.

### 2026-06-22 X.509 Certificate Slice

Implemented:

- `include/NGIN/Crypto/Certificates/Certificate.hpp`.
- `include/NGIN/Crypto/Certificates/CertificateChain.hpp`.
- `include/NGIN/Crypto/Certificates/X509.hpp`.
- `src/NGIN/Crypto/Certificates/Certificate.cpp`.
- strict top-level X.509 certificate parse into owned lightweight fields;
- raw issuer/subject DER preservation;
- lightweight issuer/subject RDN attribute parsing for common operational OIDs;
- validity string extraction;
- SubjectPublicKeyInfo extraction through the key-format layer;
- signature algorithm identification for Ed25519, ECDSA-with-SHA256, and RSASSA-PSS OIDs;
- subjectAltName DNS/email/IP extraction;
- keyUsage and extendedKeyUsage extraction;
- backend-backed certificate signature verification helper that delegates to the existing signature API.

Route adjustment:

- The certificate model extracts structure and selected extensions, but it deliberately does not implement path
  validation, hostname validation, revocation, trust-store lookup, or certificate policy processing. Those remain Net or
  higher-level PKI responsibilities.

Still pending from this workstream:

- schema-level validation for every X.509 extension;
- sustained sanitizer/fuzzer runs before broad parser use.

### 2026-06-22 Certificate Store Slice

Implemented:

- `include/NGIN/Crypto/Certificates/CertificateStore.hpp`.
- `src/NGIN/Crypto/Certificates/CertificateStore.cpp`.
- custom/in-memory certificate store construction;
- subject-DER lookup;
- subject key identifier and authority key identifier extraction from X.509 extensions;
- custom/in-memory lookup by subject key identifier and authority key identifier;
- Linux `OpenPlatformRootCertificateStore()` support backed by common system CA bundle paths;
- Windows `OpenPlatformRootCertificateStore()` source support backed by current-user `ROOT` and `CA` system stores;
- macOS `OpenPlatformRootCertificateStore()` source support backed by Apple Security trust anchors;
- structured trust-store diagnostics for custom stores, Linux CA bundle selection, Windows native stores, macOS trust
  anchors, and unsupported platforms;
- explicit `UnsupportedBackend` for hosts where no implemented root bundle or native store is available.

Route adjustment:

- Linux root enumeration uses the distribution-managed CA bundle when present. Windows uses native system certificate
  stores instead of fake roots or hardcoded certificates. macOS uses Security trust-anchor enumeration rather than
  filesystem bundle guesses.
- Trust-store diagnostics are intentionally source-level for now. They explain custom/Linux bundle selection, Windows
  store opening, macOS anchor loading, and unsupported platforms without implying full path validation, hostname policy,
  or revocation support in Crypto.

Verification status:

- Linux certificate-store tests pass after the guarded Windows/macOS source changes.
- `.github/workflows/crypto-native-ci.yml` defines Windows and macOS native-crypto jobs that run certificate tests
  alongside backend and provider-conformance tests.
- Windows and macOS source paths still require native CI/build execution before they can be marked platform-verified.

Still pending from this workstream:

- Successful Windows/macOS native-crypto CI execution for native certificate-store enumeration;
- Net-owned validation policy integration.

### 2026-06-22 PASETO v4.public/v4.local Slice

Implemented:

- `include/NGIN/Crypto/Tokens/Paseto.hpp`.
- `src/NGIN/Crypto/Tokens/Paseto.cpp`.
- strict `v4.public` parsing with explicit payload, footer, and implicit-assertion limits;
- PASETO pre-authentication encoding for header, payload, footer, and implicit assertion;
- footer policy enforcement before backend verification;
- duplicate payload JSON member rejection through the Base JSON parser;
- backend-backed Ed25519 verification through the existing signature API;
- official v4.public vector coverage for parsing and OpenSSL-backed verification;
- `v4.local` open/validation through the optional libsodium provider, using provider-backed keyed BLAKE2b for key split
  and authentication plus raw XChaCha20 for decrypting the ciphertext;
- `v4.local` sealing through the optional libsodium provider, with context-generated random nonces and strict payload
  JSON validation before encryption;
- official v4.local vector coverage, including footer and implicit assertion handling, when libsodium is enabled.

Route adjustment:

- PASETO remains a strict token-format surface, not a general application authorization policy surface. `v4.local` was
  added because the required BLAKE2b and raw XChaCha20 operations are now available through the approved libsodium
  package path; no in-house primitive implementation was added.

Still pending from this workstream:

- richer typed claim helpers if callers need them;
- sustained sanitizer/fuzzer runs before broad use.

### 2026-06-22 Password Hash String Slice

Implemented:

- `include/NGIN/Crypto/Kdf/PasswordHash.hpp`.
- `src/NGIN/Crypto/Kdf/PasswordHash.cpp`.
- context-explicit `HashPassword`, `VerifyPassword`, and `PasswordHashNeedsRehash` helpers;
- PHC-style Argon2id password-hash strings through the optional libsodium backend;
- selected-backend dispatch through `CryptoContext` without exposing libsodium public types;
- invalid-parameter, unsupported-context, fake-capability, wrong-password, malformed-hash, and rehash-needed tests.

Verification status:

- Default Linux KDF and Crypto include tests pass with libsodium disabled.
- Libsodium-enabled Linux KDF and Crypto include tests pass, including hash/verify/rehash coverage.

Route adjustment:

- Password-hash string storage is backend-backed and intentionally unavailable without a selected Argon2id-capable
  provider. Base owns the stable storage/verification contract, while provider-specific PHC encoding remains private to
  the backend implementation.

### 2026-06-22 Parser Malformed Corpus Slice

Implemented:

- deterministic malformed corpus tests for DER TLV parsing;
- deterministic malformed corpus tests for SPKI and PKCS#8 key format parsing;
- deterministic malformed corpus tests for X.509 certificate parsing;
- deterministic malformed corpus tests for JWT compact parsing;
- deterministic malformed corpus tests for PASETO v4.public parsing.

Route adjustment:

- This slice adds cheap regression corpus coverage inside the normal Catch2 test suite. It does not replace a future
  sanitizer/libFuzzer-style harness for parser APIs, which still belongs in the security gate before broad use.

### 2026-06-22 Parser Fuzz Harness Slice

Implemented:

- optional `NGIN_BASE_BUILD_FUZZERS` CMake option;
- Clang/libFuzzer targets for PEM, DER, SPKI/PKCS#8 key formats, X.509, JWT, and PASETO parser entry points;
- DER fuzzer coverage for both generic TLV helpers and ECDSA DER signature conversion parsing;
- PASETO fuzz coverage for both `v4.public` compact parsing and the `v4.local` compact open path, using a fixed
  in-harness test key when a provider is available;
- bounded fuzzer inputs that exercise parser APIs without network access;
- sanitizer flags for address and undefined-behavior checks when fuzzers are enabled.

Verification status:

- Clang/libFuzzer configure succeeds with `NGIN_BASE_BUILD_FUZZERS=ON`.
- `Crypto_DerFuzzer`, `Crypto_JwtFuzzer`, `Crypto_KeyFormatFuzzer`, `Crypto_PasetoFuzzer`, `Crypto_PemFuzzer`, and
  `Crypto_X509Fuzzer` build successfully.
- Each fuzzer is registered as a `Crypto.Fuzz` CTest startup check and starts cleanly with `-runs=1`.
- `Base.Crypto.Fuzz.Der.Startup` passes after adding ECDSA DER signature parser coverage.
- `Crypto_JwtFuzzer` covers compact parsing plus validation policy paths for HS256 and ES256 with fixed in-harness test
  key material, exercising both supported-provider and unsupported-provider return paths depending on the selected
  backend.
- `Base.Crypto.Fuzz.Paseto.Startup` passes in both the default no-provider fuzzer build and the libsodium-enabled fuzzer
  build, so the PASETO harness is checked against both unsupported-backend and provider-backed local-token paths.
- A bounded sanitizer-backed local campaign completed 1,000 runs each for PEM, DER, key-format, X.509, JWT, and PASETO
  parser fuzzers in the default fuzzer build, plus 1,000 runs for the libsodium-enabled PASETO local-token harness.

Route adjustment:

- Fuzzers are opt-in and excluded from ordinary test builds because they require Clang libFuzzer support and sanitizer
  flags. The implemented slice proves harness availability and startup health; sustained fuzz campaigns remain a
  security gate before broad parser hardening is considered complete. The 1,000-run local campaign is a smoke-level
  sanitizer pass, not a replacement for longer CI or release-gate fuzzing.

### 2026-06-22 Net Handoff Material Slice

Implemented:

- `include/NGIN/Crypto/Certificates/TlsCredentialMaterial.hpp`.
- a narrow `TlsCredentialMaterial` data object composed of `CertificateChain` and `Keys::PrivateKeyInfo`.
- umbrella exports through `Certificates/X509.hpp` and `Crypto.hpp`.

Route adjustment:

- The handoff object carries parsed credential material for future `NGIN::Net` TLS integration. It deliberately does not
  add TLS contexts, sessions, sockets, handshakes, ALPN, hostname validation, or chain validation to `NGIN::Crypto`.

### 2026-06-22 OpenSSL Package Wrapper Slice

Implemented:

- `Packages/OpenSSL/OpenSSL.nginpkg`.
- `Packages/OpenSSL/CMakeLists.txt`.
- CMake package identity mapping through `Build Mode="FindPackage" CMakePackage="OpenSSL"`.
- exported `OpenSSL::Crypto` library target metadata.
- `Build.Options` metadata for `NGIN_BASE_CRYPTO_WITH_OPENSSL=ON`.
- ECDSA P-256/SHA-256, RSA-PSS/SHA-256, and RSA-OAEP/SHA-256 provider capability metadata.

Route adjustment:

- This adds a reusable package wrapper for OpenSSL discovery and graph metadata. It does not yet make OpenSSL fully
  plug-and-play on a fresh machine by itself, because package acquisition still depends on a workspace-declared external
  package provider such as vcpkg or Conan. The wrapper now carries the NGIN.Base CMake option it requires, and generated
  CMake emits `Build.Options` for `FindPackage` packages.

### 2026-06-22 Additional Package Wrapper Slice

Implemented:

- `Packages/libsodium/libsodium.nginpkg`.
- `Packages/libsodium/CMakeLists.txt`.
- normalized `libsodium::libsodium` target support for common vcpkg, CMake config, pkg-config, and system
  library/header layouts.
- libsodium provider feature metadata for XChaCha20-Poly1305, Ed25519, X25519, and Argon2id capability discovery.
- `Packages/BoringSSL/BoringSSL.nginpkg`.
- `Packages/BoringSSL/CMakeLists.txt`.
- normalized `BoringSSL::Crypto` target support for common BoringSSL CMake or direct crypto library/header layouts.
- BoringSSL provider feature metadata for hash, HMAC, HKDF, PBKDF2, AEAD, Ed25519, ECDSA P-256/SHA-256,
  RSA-PSS/SHA-256, RSA-OAEP/SHA-256, and X25519 capability discovery.
- `Build.Options` metadata for `NGIN_BASE_CRYPTO_WITH_BORINGSSL=ON`.

Route adjustment:

- These wrappers started as package metadata and CMake target normalization only. Libsodium has since gained private
  Base backend dispatch for XChaCha20-Poly1305, Ed25519, X25519, and Argon2id behind
  `NGIN_BASE_CRYPTO_WITH_LIBSODIUM`. BoringSSL has since gained private Base dispatch through the OpenSSL-compatible
  adapter behind `NGIN_BASE_CRYPTO_WITH_BORINGSSL`; the CMake fallback refuses non-BoringSSL headers unless
  `openssl/base.h` defines `OPENSSL_IS_BORINGSSL`. Generated CMake now emits `Build.Options` for `FindPackage`
  packages, so provider metadata can propagate NGIN.Base enable options. Provider restore and successful BoringSSL
  provider CI execution remain separate verification work.

### 2026-06-22 One-Shot Helper Slice

Implemented:

- runtime-validated span construction for asymmetric key wrappers:
  - `PublicKey::FromBytes(ConstByteSpan)`;
  - `PrivateKey::FromSecretBytes(ConstByteSpan)`;
- typed random key and nonce helpers:
  - `Symmetric::GenerateAes128GcmKey`, `Symmetric::GenerateAes256GcmKey`, and
    `Symmetric::GenerateAesGcmNonce`;
  - `Symmetric::GenerateChaCha20Poly1305Key` and `Symmetric::GenerateChaCha20Poly1305Nonce`;
  - `Symmetric::GenerateXChaCha20Poly1305Key` and `Symmetric::GenerateXChaCha20Poly1305Nonce`;
- context-explicit typed AEAD helpers:
  - `Symmetric::SealAes128Gcm` and `Symmetric::OpenAes128Gcm`;
  - `Symmetric::SealAes256Gcm` and `Symmetric::OpenAes256Gcm`;
  - `Symmetric::SealChaCha20Poly1305` and `Symmetric::OpenChaCha20Poly1305`;
  - `Symmetric::SealXChaCha20Poly1305` and `Symmetric::OpenXChaCha20Poly1305`;
- context-explicit KDF convenience helpers:
  - `Kdf::HkdfSha256`, `Kdf::HkdfSha512`;
  - `Kdf::HkdfSha256Secret<N>`, `Kdf::HkdfSha512Secret<N>`;
  - `Kdf::Pbkdf2Sha256`, `Kdf::Pbkdf2Sha512`;
  - `Kdf::Pbkdf2Sha256Secret<N>`, `Kdf::Pbkdf2Sha512Secret<N>`.

Route adjustment:

- The helpers intentionally do not add implicit process-global backend selection. They bind known algorithm choices to
  the existing generic APIs while preserving explicit `CryptoContext` selection, capability checks, size validation, and
  authentication failure behavior.
- Random key and nonce helpers delegate to the existing platform secure random source. Counter/sequence nonce
  construction remains future protocol-policy work rather than a generic Crypto default.
- Span-based key construction validates byte length and keeps private bytes in secret storage. It deliberately does not
  claim full mathematical public/private key validity; backend operations remain responsible for rejecting invalid
  algorithm-specific key material.

## Architecture Direction

### Provider Tiers

Provider support should be layered in tiers:

| Tier | Provider | Ownership | Intended default |
|---|---|---|---|
| 0 | Platform random | NGIN.Base core | Always enabled |
| 1 | Native platform algorithms | NGIN.Base core | Enabled when the OS API is available |
| 2 | Package-backed providers | NGIN packages/wrappers | Enabled by workspace package composition |
| 3 | Specialized providers | Separate package or product integration | Explicit opt-in |

Tier 1 is the important missing piece for fresh machines. Tier 2 solves cross-platform parity and algorithms that native
APIs do not expose well.

### Backend Selection Policy

Backend selection should stay explicit, but become easier:

```cpp
auto context = NGIN::Crypto::Backend::CreateContext({
    .policy = NGIN::Crypto::Backend::BackendPolicy::PreferPlatformThenPackages,
});
```

Implemented policy modes:

- `PlatformOnly`
- `PackagesOnly`
- `PreferPlatformThenPackages`
- `PreferPackagesThenPlatform`
- `RequireFipsCapable`
- `RequireAlgorithmSet`

The policy object should be immutable after context creation. If a process-default context is added later, it must be
initialized once from build/package policy and then become immutable.

Current default:

- `CreateContext()` and `CreateBestAvailableContext()` prefer compiled package providers first, then platform providers.
- `CreatePlatformContext()` is the explicit platform-only helper for fresh-machine/platform-backed behavior.
- `CreatePackageContext("openssl")` is the explicit named package helper for provider-backed behavior.

### Backend Diagnostics

Every context should report:

- backend family and diagnostic name,
- backend version where available,
- FIPS or validated-module status where the provider can report it,
- available algorithms,
- selected-backend disabled algorithm reasons through `DescribeSupport(...)`,
- build option that enabled the backend,
- package name or OS API source.

Errors should remain compact, but add optional diagnostic detail for development tools:

```cpp
struct CryptoErrorDetail
{
    CryptoErrorCode code;
    Algorithm       algorithm;
    BackendInfo     backend;
    std::string_view detail;
};
```

The existing `CryptoError` can stay the ABI-friendly default. Rich details can be returned by inspection APIs or stored
in a small diagnostics object attached to context creation failures.

Status: selected-backend algorithm support reasons and context-creation candidate diagnostics are implemented through
`DescribeSupport(...)` and `CreateContextWithDiagnostics()`.

## Workstream 1: Native Platform Backends

### Windows CNG Backend

Add `src/NGIN/Crypto/Backends/CngBackend.win32.cpp`, gated by:

```cmake
NGIN_BASE_CRYPTO_WITH_CNG=ON
```

Default to `ON` on Windows when building NGIN.Base.

Initial capabilities:

- random: already covered by Windows secure random; wired into the `cng` platform context when available;
- SHA-256 and SHA-512; implemented through BCrypt;
- HMAC-SHA256 and HMAC-SHA512; implemented through BCrypt keyed hash providers;
- PBKDF2-HMAC-SHA256 and PBKDF2-HMAC-SHA512; implemented through `BCryptDeriveKeyPBKDF2`;
- AES-128-GCM and AES-256-GCM; implemented through BCrypt AES with GCM chaining;
- RSA-PSS and RSA-OAEP later, after key-format work;
- ECDSA later, after key-format and DER signature work.

Implementation notes:

- Use BCrypt for hash, HMAC, KDF, and symmetric AEAD.
- Use NCrypt only where persistent/private key handles are needed.
- Probe algorithms during context creation instead of assuming every Windows version/provider supports them.
- Keep CNG handles private to source files or private backend state.
- Return `UnsupportedAlgorithm` when a capability probe fails.
- Return `BackendUnavailable` for API/provider initialization failure.

Tests:

- shared provider conformance tests;
- NIST/RFC known-answer vectors;
- invalid key, nonce, tag, and output-size tests;
- Windows-only CI label.

Status: implementation exists, and the native crypto CI workflow now defines a Windows CNG job. Successful
Windows-hosted provider-vector execution is still pending.

### Apple Security/CommonCrypto Backend

Add a native Apple backend only after confirming exact C/C++ callable APIs per target:

```cmake
NGIN_BASE_CRYPTO_WITH_APPLE=ON
```

Initial candidates:

- secure random: already covered through platform random;
- SHA-256 and SHA-512; implemented through CommonCrypto;
- HMAC-SHA256 and HMAC-SHA512; implemented through CommonCrypto;
- PBKDF2-HMAC-SHA256 and PBKDF2-HMAC-SHA512; implemented through CommonCrypto;
- AES-GCM only when the selected API supports it cleanly;
- Ed25519/X25519 only through a stable C-compatible path or a package-backed provider.

Implementation notes:

- Avoid Objective-C or Swift-only APIs in Base unless a dedicated bridge layer is approved.
- Capability-probe each algorithm by target OS and SDK.
- Keep CommonCrypto/Security names out of public headers.

Status: `NGIN_BASE_CRYPTO_WITH_APPLE` defaults to `ON` on Apple platforms and is rejected elsewhere. The private
`apple` platform backend uses CommonCrypto for SHA-2, HMAC-SHA2, and PBKDF2-SHA2 and uses the existing Security-backed
platform random path. AES-GCM, Ed25519, and X25519 remain pending until a stable C-compatible Apple path is verified or a
package provider supplies them.

### Linux Native Strategy

Do not try to build a broad Linux-native crypto backend from scattered kernel APIs. For Linux, prioritize package-backed
OpenSSL/BoringSSL/libsodium integration.

Linux still uses OS randomness from `getrandom`.

## Workstream 2: Package-Backed Providers

### OpenSSL Package Integration

The current OpenSSL backend is useful but not plug-and-play. Improve it by treating OpenSSL as a workspace package, not
as a manual system prerequisite.

Deliverables:

- package wrapper that restores/builds OpenSSL where needed;
- CMake option and package metadata that turn on `NGIN_BASE_CRYPTO_WITH_OPENSSL`;
- version and provider probing;
- OpenSSL 3 provider diagnostics;
- clear failure messages when OpenSSL exists but an algorithm is unavailable.

Algorithms:

- SHA-256/SHA-512;
- HMAC-SHA256/HMAC-SHA512;
- HKDF-SHA256/HKDF-SHA512;
- PBKDF2-HMAC-SHA256/HMAC-SHA512;
- AES-GCM;
- ChaCha20-Poly1305;
- Ed25519;
- X25519;
- X.509, PEM, DER, RSA, and ECDSA once parser/key work is ready.

Status: an `OpenSSL` package wrapper exists for `find_package(OpenSSL)`, `OpenSSL::Crypto` target metadata, and
`CryptoProvider` capability discovery. The OpenSSL backend dispatches selected-package random through OpenSSL
`RAND_bytes` and delegates the existing hash, MAC, KDF, AEAD, Ed25519, ECDSA P-256, RSA-PSS/OAEP, and X25519 wrappers
through libcrypto. The package wrapper carries `NGIN_BASE_CRYPTO_WITH_OPENSSL=ON`, and generated CMake emits
`Build.Options` for `FindPackage` packages. The package feature declares `Crypto.Provider.openssl` and matching
`Crypto.Algorithm.*` capabilities, including random. The wrapper also declares provider-package identity
`openssl`/`3.0.0`, static/shared linkage support, `libcrypto` runtime artifact hints, and license metadata while
remaining provider-manager-neutral until a workspace or override binds `Provider="..."`. Provider restore with actual
external acquisition remains pending.

### Libsodium Package Integration

Use libsodium for modern algorithms that platform APIs and OpenSSL do not cover uniformly.

Algorithms:

- XChaCha20-Poly1305;
- Ed25519;
- X25519;
- Argon2id;
- random bytes if explicitly selected;
- sealed boxes or secret boxes only if exposed through opinionated NGIN wrappers.

Do not expose libsodium-specific public C++ types.

Status: package metadata, CMake target normalization, and Base backend dispatch are implemented. When
`NGIN_BASE_CRYPTO_WITH_LIBSODIUM=ON` finds `sodium.h` and a sodium/libsodium library or package target, the private
`libsodium` backend provides XChaCha20-Poly1305, Ed25519, X25519, Argon2id, and random through libsodium. Builds without
that provider still report those algorithms as unsupported. The package feature declares `Crypto.Provider.libsodium` and
matching `Crypto.Algorithm.*` capabilities, including random. The wrapper declares provider-package identity
`libsodium`/`1.0.19`, static/shared linkage support, `libsodium` runtime artifact hints, and license metadata while
remaining provider-manager-neutral until a workspace or override binds `Provider="..."`. Automatic package restore
remains pending. `.github/workflows/crypto-provider-ci.yml` defines an Ubuntu libsodium provider-vector job that installs
`libsodium-dev`, requires the `libsodium` provider plus XChaCha20-Poly1305, Ed25519, X25519, Argon2id, and random at
configure time, and runs backend, algorithm, token, provider-conformance, and include tests; successful remote execution
remains pending.

Verification status:

- Default Linux crypto tests pass with libsodium disabled.
- Linux configure succeeds when `NGIN_BASE_CRYPTO_WITH_LIBSODIUM=ON`,
  `NGIN_BASE_CRYPTO_REQUIRE_PROVIDER=libsodium`, and required algorithms include XChaCha20-Poly1305, Ed25519, X25519,
  and Argon2id, using a locally staged `libsodium-dev` package.
- Linux libsodium-enabled backend, KDF, AEAD, asymmetric, signature, provider-conformance, and include tests pass.

### BoringSSL Package Integration

Add only if the workspace needs BoringSSL separately from OpenSSL.

Primary use cases:

- network stack integration;
- consistent TLS-adjacent behavior;
- Google-style provider environments.

Status: package metadata, CMake target normalization, and Base backend dispatch are implemented. When
`NGIN_BASE_CRYPTO_WITH_BORINGSSL=ON`, NGIN.Base looks for `BoringSSL::Crypto`, normalizes common BoringSSL target names,
or falls back to a direct `openssl/base.h` plus `libcrypto` search only when the header identifies itself with
`OPENSSL_IS_BORINGSSL`. The selected context reports `boringssl` metadata and uses the shared OpenSSL-compatible private
operation adapter for SHA-2, HMAC, HKDF, PBKDF2, AES-GCM, Ed25519, X25519, and random. ChaCha20-Poly1305 uses a
BoringSSL-specific `EVP_AEAD` branch because BoringSSL does not expose OpenSSL's `EVP_chacha20_poly1305()` cipher
helper.

The package feature declares `Crypto.Provider.boringssl` and matching `Crypto.Algorithm.*` capabilities, including
random. The wrapper declares provider-package identity `boringssl`, static/shared linkage support, `libcrypto` runtime
artifact hints, and license metadata while remaining provider-manager-neutral until a workspace or override binds
`Provider="..."`. Local Linux verification builds BoringSSL from source and passes NGIN.Base backend,
provider-conformance, and include tests against that `libcrypto`. `.github/workflows/crypto-provider-ci.yml` defines the
same Ubuntu provider-vector route for remote CI; successful remote execution remains pending.

## Workstream 3: Provider Conformance Tests

Create one shared test suite that every provider must pass.

Suggested structure:

```text
tests/Crypto/ProviderConformance.cpp
tests/Crypto/ProviderVectors/
  HashVectors.hpp
  HmacVectors.hpp
  KdfVectors.hpp
  AeadVectors.hpp
  SignatureVectors.hpp
  KeyAgreementVectors.hpp
```

Each provider test should:

- create a context for one backend or policy;
- assert reported capabilities;
- run vectors only for supported algorithms;
- assert unsupported algorithms return `UnsupportedAlgorithm`;
- verify tamper rejection for authentication APIs;
- verify output buffers are checked before backend calls;
- verify no partial plaintext is reported as success.

Provider-specific tests may add backend-lifetime and diagnostics coverage, but vector behavior should be shared.

Status: initial shared suite exists for current algorithm providers. Windows CNG execution is pending actual Windows CI.

### 2026-06-22 ECDSA P-256/SHA-256 Interop Slice

Implemented:

- fixed-size ECDSA P-256/SHA-256 signature metadata using 65-byte uncompressed public keys, 32-byte private scalars, and
  64-byte raw `r || s` signatures;
- typed `Asymmetric/Ecdsa.hpp` convenience wrappers over the generic `Signatures::Sign` and `Signatures::Verify`
  contract;
- strict DER conversion helpers between fixed raw `r || s` signatures and standard ECDSA SEQUENCE/INTEGER signatures;
- OpenSSL-compatible provider support for ECDSA P-256/SHA-256 sign and verify;
- shared provider conformance and focused wrapper coverage for unsupported contexts, fixed raw signature verification,
  generated-signature verification, DER conversion, public header inclusion, and tamper rejection.

Verification status:

- Default Linux signature, include, and provider-conformance tests pass with no OpenSSL provider enabled.
- OpenSSL-enabled Linux signature, include, and provider-conformance tests pass, including the ECDSA vector,
  generated-signature round trip, and DER conversion coverage.

Route adjustment:

- The public signature API remains fixed-size. ECDSA is exposed as raw P1363-style `r || s` bytes at the API boundary,
  while DER conversion is explicit at the ECDSA helper boundary. This avoids widening the generic `Sign`/`Verify`
  contract for variable-length DER signatures.

### 2026-06-22 RSA-PSS/OAEP Interop Slice

Implemented:

- `Asymmetric/Rsa.hpp` helper API for RSA-PSS/SHA-256 sign/verify and RSA-OAEP/SHA-256 encrypt/decrypt;
- dedicated `AsymmetricEncryptionAlgorithm::RsaOaepSha256` capability bits, `AlgorithmSet` requirements, context
  support/diagnostic overloads, and backend dispatch hooks;
- OpenSSL-compatible provider support using DER SubjectPublicKeyInfo public keys and DER PKCS#8 private keys;
- shared provider conformance coverage for unsupported contexts, fixed OpenSSL-generated PSS vector verification,
  generated PSS signatures, signature tamper rejection, OAEP roundtrip encryption/decryption, and OAEP label mismatch
  rejection;
- configure-time requirement names and package capability metadata for `rsa-pss-sha256` and `rsa-oaep-sha256` on
  OpenSSL-compatible providers.

Verification status:

- Default Linux provider-conformance and include tests pass with RSA reported as unsupported.
- OpenSSL-enabled Linux provider-conformance and include tests pass, including the RSA-PSS vector and RSA-OAEP roundtrip.

Route adjustment:

- RSA is intentionally not exposed through the fixed-size generic signature helper because signature and ciphertext
  sizes are modulus-dependent. The public RSA helpers use full DER key containers at the API boundary to match common
  protocol and certificate interoperability points, while fixed-size Ed25519/ECDSA wrappers remain unchanged.
- PKCS#1 v1.5 signing is still not added as a recommended default.

## Workstream 4: Build-Time And Package Ergonomics

### Build Options

Normalize options:

```cmake
NGIN_BASE_CRYPTO_WITH_CNG
NGIN_BASE_CRYPTO_WITH_APPLE
NGIN_BASE_CRYPTO_WITH_OPENSSL
NGIN_BASE_CRYPTO_WITH_BORINGSSL
NGIN_BASE_CRYPTO_WITH_LIBSODIUM
NGIN_BASE_CRYPTO_REQUIRE_PROVIDER
NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS
```

Keep compatibility aliases temporarily only when needed by existing build trees.

Status: current Base-owned options are implemented for CNG/Apple/OpenSSL/libsodium and requirement enforcement.
BoringSSL package wrapper metadata and backend dispatch are implemented through the OpenSSL-compatible adapter.
`NGIN_BASE_CRYPTO_WITH_OPENSSL` and `NGIN_BASE_CRYPTO_WITH_BORINGSSL` are mutually exclusive because both bind an
OpenSSL-compatible `libcrypto` surface with different provider identity and packaging expectations.
Generated CMake emits package `Build.Options` for `FindPackage`, `AddSubdirectory`, and `Manual` integrations, allowing
OpenSSL/BoringSSL package metadata to set the corresponding NGIN.Base enable option when the package is part of the
resolved composition.

### Workspace Package Metadata

Provider packages should declare:

- provided backend name;
- algorithms and parser capabilities;
- static/shared linkage support;
- platform constraints;
- runtime deployment requirements;
- license notes.

The CLI exposes initial runtime crypto diagnostics:

```text
ngin crypto info
ngin crypto explain --algorithm AES-256-GCM
```

Status: initial runtime CLI diagnostics are implemented as `ngin crypto info` and
`ngin crypto explain --algorithm <name>`. The commands inspect the crypto backend compiled into the CLI process, print
selected backend metadata, and explain support or unsupported reasons for the same algorithm identifiers used by
`NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS`. Algorithm names are accepted case-insensitively and both commands support
`--format json` for tooling. When `ngin crypto explain` is run with `--project`, it also resolves the workspace graph
and reports selected `Crypto.Algorithm.*` package capabilities plus selected `Crypto.Provider.*` backend candidates,
including provider package identity, linkage support, runtime deployment hints, and runtime artifact hints from the
selected package wrapper.
Provider wrappers now declare backend names, algorithm capabilities, platform constraints, provider-package identities,
static/shared linkage support, runtime artifact hints, and license metadata. The CLI package model parses this metadata
and exposes it through graph package diagnostics, crypto explanation diagnostics, and provider restore lock output.
Automatic runtime dependency staging from those hints remains future package-contract work.

## Workstream 5: API Ergonomics

### Easy Context Creation

Add helper APIs that are still explicit:

```cpp
auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
auto context = NGIN::Crypto::Backend::CreatePlatformContext();
auto context = NGIN::Crypto::Backend::CreatePackageContext("openssl");
```

These helpers should call the same policy engine as `CreateContext`.

Status: implemented for the current platform-random and OpenSSL provider set.

### Algorithm Sets

Let users request a capability set:

```cpp
auto required = NGIN::Crypto::Backend::AlgorithmSet {}
    .Require(NGIN::Crypto::HashAlgorithm::Sha256)
    .Require(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

auto context = NGIN::Crypto::Backend::CreateContext({.requiredAlgorithms = required});
```

This gives application authors one failure point at startup instead of repeated failures deep in request handling.

Status: implemented through `BackendOptions::requiredAlgorithms`.

### One-Shot Defaults

Consider one-shot helpers that accept a context and remain allocation-conscious:

```cpp
auto digest = NGIN::Crypto::Hashing::Sha256(context, message);
auto sealed = NGIN::Crypto::Symmetric::SealAes256Gcm(context, key, nonce, plaintext, aad);
```

Avoid implicit process-global provider selection until the immutable default-context policy exists.

Status: implemented for SHA-2, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, and XChaCha20-Poly1305 wrappers that all
accept an explicit `CryptoContext`. XChaCha20-Poly1305 is supported when the optional libsodium backend is compiled.

### Stronger Types

Improve ergonomics without weakening safety:

- random constructors for typed keys;
- nonce helper types with explicit random/counter construction;
- `PublicKey::FromBytes` and `PrivateKey::FromSecretBytes` validation helpers;
- `Verified<T>` wrappers for successfully parsed and validated structures;
- `UnsafeBytes` naming for APIs that expose secret material.

Status: random typed key and nonce helpers are implemented for the AEAD leaf headers. Span-based public/private key
construction helpers are implemented. Counter/sequence nonce construction remains pending because the correct policy is
protocol-specific.

## Workstream 6: Parser-Heavy Features

Parser-heavy features are important for networked applications, but they must be strict and deliberately staged.

### Parser Principles

- Strict by default.
- No duplicate fields unless the format explicitly permits them.
- No best-effort recovery for security formats.
- Length limits are explicit and enforced before allocation.
- Parsing and validation are separate when that improves clarity.
- Parsed objects preserve enough source information for diagnostics, but never expose secret material accidentally.
- All parser APIs return `CryptoExpected<T>`.
- Fuzz tests are required before broad use.

### PEM

Public headers:

```text
include/NGIN/Crypto/Encoding/Pem.hpp
```

Source:

```text
src/NGIN/Crypto/Encoding/Pem.cpp
```

Scope:

- strict PEM block parser;
- label allowlist;
- maximum decoded size;
- line ending normalization;
- reject malformed base64;
- reject nested or mixed blocks by default;
- no key interpretation in the PEM parser.

Status: implemented for raw PEM block decoding. Key, certificate, and trust interpretation intentionally remain in later
layers.

API sketch:

```cpp
auto blocks = NGIN::Crypto::Encoding::ParsePem(input, {
    .allowedLabels = {"CERTIFICATE", "PUBLIC KEY", "PRIVATE KEY"},
    .maxDecodedBytes = 1u << 20,
});
```

### DER

Public headers:

```text
include/NGIN/Crypto/Encoding/Der.hpp
```

Scope:

- minimal strict DER reader and writer;
- definite lengths only;
- reject BER/CER indefinite lengths;
- reject non-minimal length encodings;
- bounded nesting depth;
- bounded allocation;
- primitive helpers for INTEGER, BIT STRING, OCTET STRING, OID, SEQUENCE, SET.

Do not build a general ASN.1 object model unless a concrete feature needs it.

Status: implemented as a bounded DER TLV reader/writer with primitive helpers. Schema-specific validation remains in
the key-format and certificate layers.

### Key Formats

Public headers:

```text
include/NGIN/Crypto/Keys/KeyFormat.hpp
include/NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp
include/NGIN/Crypto/Keys/PrivateKeyInfo.hpp
```

Scope:

- SubjectPublicKeyInfo parse/write;
- PKCS#8 PrivateKeyInfo parse/write;
- PKCS#8 EncryptedPrivateKeyInfo parse/write as a data-only envelope;
- algorithm OID mapping to NGIN algorithm enums;
- typed Ed25519/X25519/ECDSA P-256 key import/export bridges between parsed key formats and fixed-size wrappers;
- encrypted private-key decryption later, after password KDF and AEAD policy are stable;
- backend import/export hooks.

Key interpretation belongs above raw PEM/DER but below certificates and TLS.

Status: raw SubjectPublicKeyInfo and PKCS#8 PrivateKeyInfo parse/write are implemented. Typed Ed25519, X25519, and
ECDSA P-256 key import/export bridges are implemented for the existing fixed-size wrappers. ECDSA P-256 public import
requires a 65-byte uncompressed point, and private import requires a 32-byte scalar. PKCS#8 EncryptedPrivateKeyInfo
parse/write is implemented as a strict data-only envelope that preserves encryption AlgorithmIdentifier bytes and
encrypted payload bytes. Password decryption, password-KDF/cipher policy, and backend key-handle import/export hooks
remain pending.

### X.509 Certificates

Public headers:

```text
include/NGIN/Crypto/Certificates/Certificate.hpp
include/NGIN/Crypto/Certificates/CertificateChain.hpp
include/NGIN/Crypto/Certificates/X509.hpp
```

Scope:

- strict certificate parse into lightweight views;
- subject, issuer, serial, validity, public key info;
- subjectAltName DNS/IP/email extraction;
- key usage and extended key usage extraction;
- signature algorithm identification;
- backend-backed signature verification;
- chain container type.

Status: lightweight certificate and chain containers are implemented with issuer/subject RDN extraction, selected
extension extraction, and a backend signature verification handoff. Full path validation and trust policy remain out of
Crypto.

Out of initial scope:

- full path validation policy;
- hostname verification policy;
- trust store selection;
- revocation checking.

Those belong in `NGIN::Net` or a higher-level certificate-validation package that consumes the parsed certificate model.

### Certificate Stores

Public headers:

```text
include/NGIN/Crypto/Certificates/CertificateStore.hpp
```

Scope:

- neutral handle over platform/package trust material;
- enumerate roots where platform policy allows it;
- lookup by subject/key identifier where supported;
- diagnostics for unavailable platform stores.

Integration with TLS validation should live in `NGIN::Net`.

Status: custom/in-memory store lookup by subject DER, subject key identifier, and authority key identifier is
implemented. Linux platform root loading from common system CA bundle paths is implemented. Windows native
current-user `ROOT`/`CA` store enumeration source is implemented. macOS Security trust-anchor enumeration source is
implemented. Store info and opt-in selection diagnostics report OS, source, path, availability, loaded/skipped counts,
and failure reason. Linux certificate-store tests pass after the guarded platform changes; Windows/macOS native CI
execution remains pending.

### JWT

Public headers:

```text
include/NGIN/Crypto/Tokens/Jwt.hpp
```

Scope:

- strict compact serialization parser;
- no `alg=none`;
- explicit allowed algorithms;
- explicit expected issuer/audience/time policy;
- required claim checks;
- typed string/int64/bool custom claim accessors;
- constant-time signature comparison;
- size limits for header, payload, and signature;
- no implicit JSON claim interpretation beyond required validation primitives.

JWT should be treated as interop support, not the default new-token recommendation.

Status: strict compact parsing and validation are implemented for HS256, PS256, ES256, and EdDSA. Validation rejects
`alg=none`, requires explicit allowed algorithms, applies size limits, rejects duplicate JSON object members through the
Base JSON parser, enforces issuer/audience/time/required-claim policy, exposes typed custom claim accessors, delegates
signatures/MACs to the selected backend, and has malformed corpus plus optional libFuzzer startup coverage.

Route adjustment:

- JWT is the only current Crypto surface that depends on the Base Serialization component, because JWT header and claim
  sets are JSON. The component metadata now records `NGIN.Base.Crypto -> NGIN.Base.Serialization` rather than adding a
  second JSON parser inside Crypto.
- PS256 validation uses the RSA-PSS/SHA-256 helper and DER SubjectPublicKeyInfo key material, matching the broader
  RSA interop boundary instead of adding JWT-specific key import code.
- ES256 validation uses the existing ECDSA P-256/SHA-256 provider capability with raw uncompressed P-256 public keys and
  fixed raw `r || s` signatures. JWK import remains a separate key-format feature rather than being hidden in JWT.

Still pending:

- more JWT algorithms only when corresponding backend-safe key import and signature APIs are ready;
- sustained sanitizer/fuzzer runs before broad use.

### PASETO

Public headers:

```text
include/NGIN/Crypto/Tokens/Paseto.hpp
```

Scope:

- official-vector-backed implementation through backend primitives;
- explicit supported versions and purposes;
- footer and implicit assertion handling;
- typed string/int64/bool payload claim accessors;
- strict parse and validation API.

Prefer PASETO over JWT for new designs when provider support and interoperability needs allow it.

Status: `v4.public` parsing and validation are implemented. Validation uses PASETO pre-authentication encoding over the
header, payload, footer, and implicit assertion, rejects duplicate JSON object members in the payload, enforces footer
policy before backend verification, exposes typed payload claim accessors, and delegates Ed25519 verification to the
selected backend. `v4.local` seal/open validation is implemented through the optional libsodium provider using
context-generated random nonces, keyed BLAKE2b for key split/authentication, and raw XChaCha20 for encryption/decryption.
Official vector decryption, footer/implicit assertion handling, roundtrip sealing, and tamper rejection are covered by
focused token tests.

Route adjustment:

- PASETO is included as a strict interop parser/validator, not as a new primitive implementation. The parser follows the
  same Base rule as JWT: format handling is Base-owned, cryptographic verification is provider-backed.

## Workstream 7: Net Integration Boundary

Crypto should provide:

- certificate and key object models;
- parse/import/export helpers;
- backend-backed signature/key operations;
- provider diagnostics;
- trust-store handles.

Net should provide:

- TLS context;
- TLS client/server sockets;
- ALPN;
- SNI;
- session behavior;
- hostname validation flow;
- chain validation policy;
- certificate store selection;
- handshake diagnostics.

The first Net-facing deliverable should be a narrow handoff object:

```cpp
struct TlsCredentialMaterial
{
    NGIN::Crypto::Certificates::CertificateChain certificateChain;
    NGIN::Crypto::Keys::PrivateKeyInfo privateKey;
};
```

Status: `Certificates::TlsCredentialMaterial` is implemented as a data-only handoff type. Net-owned TLS context,
validation policy, and runtime behavior remain pending.

## Workstream 8: Documentation And Examples

Add examples for:

- fresh Windows platform context;
- checking capabilities at startup;
- random token generation;
- hashing with explicit context;
- AES-GCM seal/open;
- Ed25519 sign/verify;
- X25519 plus HKDF;
- PEM to certificate parse;
- certificate subjectAltName extraction;
- JWT validation with explicit policy.

Update `docs/Crypto.md` with:

- platform support matrix;
- provider installation and package restore guide;
- "fresh machine" behavior;
- unsupported algorithm troubleshooting;
- parser strictness rules;
- "what not to use" guidance.

Status: `docs/Crypto.md` documents fresh-machine behavior, provider diagnostics, provider install/restore routes,
build-time requirements, parser strictness, unsupported-feature boundaries, and the `examples/CryptoCapabilities`
startup diagnostic example.
`examples/CryptoCookbook` now provides a capability-gated walkthrough for random token generation, hashing, AES-GCM,
Ed25519, X25519 plus HKDF, PEM-to-X.509 parsing with subjectAltName extraction, and JWT validation with explicit policy.
The cookbook builds and runs against the default Linux platform-random backend, where provider-backed operations report
skip reasons and parser-only operations still execute. The provider walkthrough documents system-package, vcpkg, and
Conan restore paths, including the requirement that package build metadata name an external provider before
`ngin restore` acquires that package.

## Workstream 9: Performance And Allocation Discipline

Add benchmarks after provider APIs stabilize:

```text
benchmarks/CryptoRandomBenchmarks.cpp
benchmarks/CryptoHashBenchmarks.cpp
benchmarks/CryptoAeadBenchmarks.cpp
benchmarks/CryptoParserBenchmarks.cpp
benchmarks/CryptoKeyFormatBenchmarks.cpp
benchmarks/CryptoBackendDispatchBenchmarks.cpp
```

Measure:

- one-shot versus preallocated APIs;
- backend dispatch overhead;
- parser allocation counts;
- PEM/DER parse throughput;
- AES-GCM and ChaCha20-Poly1305 throughput;
- key import/export cost.

Status: initial crypto benchmark targets are implemented as `CryptoRandomBenchmarks`, `CryptoHashBenchmarks`,
`CryptoAeadBenchmarks`, `CryptoParserBenchmarks`, `CryptoKeyFormatBenchmarks`, and
`CryptoBackendDispatchBenchmarks`. They build in the benchmark tree and smoke-run successfully. Hash and AEAD benchmarks
register work only when the selected context supports those algorithms. Key-format benchmark coverage measures
SPKI/PKCS#8 parse/import/export/write paths through the Base-owned typed Ed25519/X25519/ECDSA P-256 bridges.

Hot-path APIs must provide caller-owned output variants.

## Workstream 10: Security Gates

Before a feature is considered complete:

- known-answer vectors pass;
- malformed input tests pass;
- tamper tests pass;
- output buffer tests pass;
- capability and unsupported-path tests pass;
- no successful result contains unverified plaintext or claims;
- secret buffers are wiped on destruction and move;
- no debug output leaks key material or plaintext secrets;
- fuzz tests exist for parsers;
- sanitizer runs are clean for new parser code;
- docs identify supported providers and unsupported platforms.

## Proposed Order

1. Provider diagnostics and backend policy model.
2. Windows CNG backend for SHA-2, HMAC, PBKDF2, and AES-GCM.
3. Shared provider conformance tests.
4. OpenSSL package-backed provider integration.
5. Capability-driven examples and docs.
6. PEM parser.
7. DER reader.
8. SubjectPublicKeyInfo and PKCS#8 key import/export.
9. X.509 certificate parse/view.
10. Net handoff objects for certificate chains and private keys.
11. JWT strict validation.
12. libsodium package for XChaCha20-Poly1305 and Argon2id.
13. PASETO.
14. Certificate store and Net validation integration.

## Reference Material

- Microsoft CNG algorithm identifiers:
  <https://learn.microsoft.com/en-us/windows/win32/seccng/cng-algorithm-identifiers>
- Microsoft CNG algorithm pseudo-handles:
  <https://learn.microsoft.com/en-us/windows/win32/seccng/cng-algorithm-pseudo-handles>
- Apple CryptoKit overview:
  <https://developer.apple.com/documentation/cryptokit/>
- OpenSSL 3 migration guide:
  <https://docs.openssl.org/3.0/man7/migration_guide/>
- libsodium XChaCha20-Poly1305 documentation:
  <https://libsodium.gitbook.io/doc/secret-key_cryptography/aead/chacha20-poly1305/xchacha20-poly1305_construction>
- RFC 5280 for X.509 public key infrastructure.
- RFC 7519 for JWT.
- PASETO specifications:
  <https://paseto.io/>
  <https://datatracker.ietf.org/doc/html/draft-paragon-paseto-rfc-01>
