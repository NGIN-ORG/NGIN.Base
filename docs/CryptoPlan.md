# Crypto Renewal Plan

This plan replaces the current flat `NGIN::Crypto` scaffolding with a state-of-the-art, backend-backed crypto
surface aligned with NGIN.Base rules: C++23, small public headers, non-trivial and platform-specific implementation in
`src/`, deterministic error handling, no hidden mutable global state, and no unapproved production dependencies.

Crypto is security-critical. The default rule is: do not hand-roll cryptographic primitives in NGIN.Base. NGIN.Base may
own API contracts, secure memory utilities, constant-time helpers, encodings, OS randomness, dispatch, documentation,
and tests. Hashes, MACs, KDFs, AEADs, signatures, asymmetric operations, and certificate/key handling must be
implemented by approved backends or platform APIs, with known-answer tests validating the NGIN wrapper. TLS belongs to
`NGIN::Net`, which consumes Crypto primitives, certificate/key abstractions, and backend capabilities.

## Design Targets

- Public API under `include/NGIN/Crypto/...`.
- Non-trivial implementation under `src/NGIN/Crypto/...`.
- Platform and implementation backend names must not leak into the normal public include surface. Public headers describe
  algorithms, policies, contexts, capabilities, and results; CNG, Security/CommonCrypto, OpenSSL, BoringSSL, libsodium,
  and similar backends are private adapters selected by build configuration, package composition, or an explicit neutral
  runtime context.
- Error returns via `NGIN::Utilities::Expected<T, CryptoError>` for recoverable failures.
- Public byte-oriented APIs use `std::span<const NGIN::Byte>` and `std::span<NGIN::Byte>`.
- Owned binary outputs use a Crypto-level `ByteBuffer` alias backed by `NGIN::Containers::Vector<NGIN::Byte>` when
  allocator support is needed; fixed-size values use `std::array<NGIN::Byte, N>`.
- APIs are easy for normal users: one-shot helpers first, streaming state for large inputs, explicit expert APIs for
  context selection and preallocated output.
- Hot paths avoid heap allocation where caller-provided output buffers can be used.
- Secrets are move-only where practical, wiped on destruction, and never implicitly converted to strings.
- Algorithm choices make misuse hard: prefer AEAD over raw ciphers, Ed25519/X25519 over legacy curves, Argon2id over
  PBKDF2 for password hashing when a backend supports it.
- Legacy interoperability APIs such as RSA, ECDSA, PEM, DER, and X.509 are available only behind explicit backend
  capability checks.
- TLS context/session APIs are not part of `NGIN::Crypto`; they live under `NGIN::Net`.

## Non-Goals

- No custom AES, SHA, ChaCha, Ed25519, RSA, ASN.1, X.509, JWT, or Argon2 implementations in NGIN.Base core.
- No TLS context, TLS client, or TLS server public API under `NGIN::Crypto`.
- No root-level `CryptoRandom.*` duplicate family after migration.
- No public backend class family such as `WindowsCngProvider`, `OpenSslProvider`, or `LibsodiumProvider` in
  `include/NGIN/Crypto`. Backend names belong in private source files, package wrappers, build options, tests, and
  diagnostics.
- No implicit global "default backend" whose lifetime or configuration can surprise callers.
- No raw ECB/CBC/CTR cipher surface in the first-class API. Add only if a concrete interoperability requirement appears.
- No unauthenticated encryption helpers.
- No JWT or PASETO signing policy that encourages users to skip validation rules. Token support must be strict and
  opinionated if included.

## Proposed Header Layout

The old plan's broad ideas are retained, but reshaped into NGIN.Base's current include layout and a backend-backed
contract. The high-level convenience headers re-export stable leaf headers only.

```text
include/NGIN/Crypto/
  Crypto.hpp
  Concepts.hpp
  Types.hpp
  Algorithm.hpp
  Result.hpp
  ByteBuffer.hpp

  Errors/
    CryptoError.hpp
    ErrorCode.hpp
    AuthenticationError.hpp
    InvalidKeyError.hpp
    UnsupportedAlgorithmError.hpp

  Memory/
    ConstantTime.hpp
    ZeroMemory.hpp
    SecureBuffer.hpp
    SecureString.hpp
    Secret.hpp
    SecretView.hpp

  Random/
    Random.hpp
    RandomBytes.hpp
    SecureRandom.hpp
    RandomError.hpp
    EntropySource.hpp

  Encoding/
    Hex.hpp
    Base64.hpp
    Base64Url.hpp
    Pem.hpp
    Der.hpp

  Hashing/
    Hash.hpp
    HashAlgorithm.hpp
    Digest.hpp
    Sha256.hpp
    Sha512.hpp
    Sha3.hpp
    Blake3.hpp

  Mac/
    Mac.hpp
    MacAlgorithm.hpp
    Hmac.hpp
    HmacSha256.hpp
    HmacSha512.hpp

  Kdf/
    KeyDerivation.hpp
    Hkdf.hpp
    Pbkdf2.hpp
    Argon2id.hpp

  Symmetric/
    Aead.hpp
    AeadAlgorithm.hpp
    AesGcm.hpp
    ChaCha20Poly1305.hpp
    XChaCha20Poly1305.hpp
    SecretBox.hpp

  Asymmetric/
    KeyTypes.hpp
    KeyPair.hpp
    PublicKey.hpp
    PrivateKey.hpp
    Ed25519.hpp
    X25519.hpp
    Ecdsa.hpp
    Rsa.hpp

  Signatures/
    Signature.hpp
    Sign.hpp
    Verify.hpp

  Passwords/
    PasswordHash.hpp
    PasswordVerify.hpp
    PasswordPolicy.hpp

  Tokens/
    TokenGenerator.hpp
    SecureToken.hpp
    Jwt.hpp
    Paseto.hpp

  Certificates/
    Certificate.hpp
    CertificateChain.hpp
    X509.hpp
    CertificateStore.hpp

  Backend/
    CryptoContext.hpp
    BackendCapabilities.hpp
    BackendInfo.hpp
    BackendOptions.hpp
```

## Proposed Source Layout

Source files are added only when needed for platform calls, secure erasure barriers, backend adapters, parsers, or
non-trivial state machines. Header-only code is limited to small wrappers, fixed-size type declarations, concepts, and
constexpr helpers.

Backend adapters are implementation details. Their names may appear in source filenames and CMake options, but not as
ordinary public headers. Tests may include private test hooks when necessary, but application code should configure
crypto through neutral contexts and capability checks.

```text
src/NGIN/Crypto/
  Errors/
    CryptoError.cpp

  Memory/
    ZeroMemory.cpp
    ConstantTime.cpp
    SecureBuffer.cpp

  Random/
    SecureRandom.cpp
    SecureRandom.win32.cpp
    SecureRandom.linux.cpp
    SecureRandom.apple.cpp
    SecureRandom.posix.cpp

  Encoding/
    Hex.cpp
    Base64.cpp
    Base64Url.cpp
    Pem.cpp
    Der.cpp

  Hashing/
    Hash.cpp

  Mac/
    Hmac.cpp

  Kdf/
    Hkdf.cpp
    Pbkdf2.cpp
    Argon2id.cpp

  Symmetric/
    Aead.cpp

  Asymmetric/
    KeyTypes.cpp
    Ed25519.cpp
    X25519.cpp
    Ecdsa.cpp
    Rsa.cpp

  Signatures/
    Signature.cpp

  Passwords/
    PasswordHash.cpp
    PasswordPolicy.cpp

  Tokens/
    TokenGenerator.cpp
    Jwt.cpp
    Paseto.cpp

  Certificates/
    Certificate.cpp
    X509.cpp
    CertificateStore.cpp

  Backends/
    BackendDispatch.cpp
    PlatformRandomBackend.cpp
    CngBackend.win32.cpp
    AppleSecurityBackend.apple.cpp
    OpenSslBackend.cpp
    BoringSslBackend.cpp
    LibsodiumBackend.cpp
```

Backend source files must be gated by explicit CMake options or package wrappers. The initial core source set should
only include standard-library-compatible utilities and OS randomness.

## Public API Shape

### One-Shot Helpers

One-shot helpers are the recommended path for small inputs and application code:

```cpp
auto digest = NGIN::Crypto::Hashing::Sha256(message);
auto key = NGIN::Crypto::Random::RandomBytes<32>();
auto sealed = NGIN::Crypto::Symmetric::Aes256Gcm::Seal(key, nonce, plaintext, aad);
auto verified = NGIN::Crypto::Signatures::Ed25519::Verify(publicKey, message, signature);
```

### Preallocated APIs

Performance-sensitive paths use caller-owned buffers:

```cpp
auto result = NGIN::Crypto::Hashing::Sha256Into(message, outputDigest);
auto sealed = aead.SealInto(nonce, plaintext, aad, ciphertext, tag);
```

### Streaming APIs

Streaming state is explicit and backend-owned:

```cpp
NGIN::Crypto::Hashing::Sha256Hasher hasher;
hasher.Update(chunk0);
hasher.Update(chunk1);
auto digest = hasher.Finalize();
```

### Context and Backend APIs

Backend use is explicit without exposing backend classes as public API:

```cpp
NGIN::Crypto::Backend::CryptoContext context = NGIN::Crypto::Backend::CreateContext(options);
auto capabilities = context.Capabilities();
auto digest = NGIN::Crypto::Hashing::Sha256(message, context);
```

The convenience API may accept a neutral `CryptoContext&` overload, but must not silently select a mutable process-wide
backend. Backend-backed algorithms should require an explicit context until the immutable process-default policy is
implemented. If a default context is added later, it must be initialized once from build/package policy, immutable after
initialization, and documented as a process-level policy object. Backend diagnostics may report names such as `cng`,
`openssl`, or `libsodium`, but those names are data, not public C++ types.

### TLS Boundary

TLS belongs under `NGIN::Net`, not `NGIN::Crypto`. Crypto owns primitives, key/certificate representations, secure
memory, and neutral backend capability reporting. Net owns TLS contexts, sockets, handshakes, ALPN, session behavior,
certificate validation flow, and client/server ergonomics.

## Error Model

Use `CryptoExpected<T> = NGIN::Utilities::Expected<T, CryptoError>` for recoverable failures. Use assertions for
programmer errors such as invalid fixed-size type construction in unchecked internal helpers.

`CryptoErrorCode` should include:

- `None`
- `InvalidArgument`
- `OutputBufferTooSmall`
- `InvalidKey`
- `InvalidNonce`
- `InvalidTag`
- `AuthenticationFailed`
- `UnsupportedAlgorithm`
- `UnsupportedBackend`
- `BackendUnavailable`
- `EntropyUnavailable`
- `EncodingError`
- `ParseError`
- `PolicyRejected`
- `InternalError`

Authentication failures must be distinct from parse failures and invalid key failures. Token, AEAD, password, and
signature verification APIs must return failure values, not booleans alone, when the caller needs diagnostics.

## Concept Implementation Checklist

Use this table as the tracking checklist. `Done` means public docs, headers, implementation, tests, and CMake wiring are
all complete for the intended phase.

| Done | Concept | Phase | Public headers | Source files | Tests and benchmarks | Notes |
|---|---|---:|---|---|---|---|
| [x] | Crypto umbrella | 1 | `Crypto.hpp` | None | Include smoke test | Re-export stable leaf headers only. |
| [x] | Core types | 1 | `Types.hpp`, `Concepts.hpp`, `Algorithm.hpp`, `Result.hpp`, `ByteBuffer.hpp` | None | Compile-time API tests | Define byte spans, `ByteBuffer`, fixed-size wrappers, backend concepts, `CryptoExpected`. |
| [x] | Error model | 1 | `Errors/CryptoError.hpp`, `Errors/ErrorCode.hpp` | `Errors/CryptoError.cpp` | Error construction and message tests | Keep small, trivially copyable where possible. |
| [x] | Constant-time compare | 1 | `Memory/ConstantTime.hpp` | `Memory/ConstantTime.cpp` | `ConstantTimeTests.cpp`, timing sanity benchmark | Must not early-exit on equal-length inputs. |
| [x] | Secure zero memory | 1 | `Memory/ZeroMemory.hpp` | `Memory/ZeroMemory.cpp` | Optimizer barrier tests where practical | Use platform or compiler barriers; do not rely on volatile loop alone. |
| [x] | Secure buffer | 1 | `Memory/SecureBuffer.hpp`, `Memory/Secret.hpp`, `Memory/SecretView.hpp` | `Memory/SecureBuffer.cpp` | Move, wipe, resize, allocator tests | `SecureBuffer`/`DynamicSecret` for variable-size secrets; `Secret<T>` and `FixedSecret<N>` for typed fixed-size secret material. |
| [ ] | Secure string | 2 | `Memory/SecureString.hpp` | Optional | Wipe and conversion tests | Add only if real text secret use cases remain after `SecureBuffer`. |
| [x] | OS random bytes | 1 | `Random/Random.hpp`, `Random/RandomBytes.hpp`, `Random/SecureRandom.hpp`, `Random/RandomError.hpp` | `Random/SecureRandom.*.cpp` | `RandomTests.cpp` | Linux `getrandom`, Windows `BCryptGenRandom`, Apple `SecRandomCopyBytes`, POSIX fallback policy. |
| [ ] | Entropy source abstraction | 2 | `Random/EntropySource.hpp` | `Random/SecureRandom.cpp` | Deterministic test source tests | For tests and backend integration, not for userland PRNG security claims. |
| [x] | Hex encoding | 1 | `Encoding/Hex.hpp` | `Encoding/Hex.cpp` | `EncodingTests.cpp`, invalid input tests | Constant-time decode is not required unless used for secret comparison. |
| [x] | Base64 | 1 | `Encoding/Base64.hpp` | `Encoding/Base64.cpp` | RFC vector tests, invalid input tests | Strict decoder by default; no whitespace unless option enables it. |
| [x] | Base64Url | 1 | `Encoding/Base64Url.hpp` | `Encoding/Base64Url.cpp` | RFC vector tests | Required by tokens; padding policy explicit. |
| [ ] | PEM | 3 | `Encoding/Pem.hpp` | `Encoding/Pem.cpp` | PEM parse and reject tests | Parser only; key interpretation belongs to backend/key modules. |
| [ ] | DER | 3 | `Encoding/Der.hpp` | `Encoding/Der.cpp` | DER length and malformed tests | Minimal strict DER reader/writer needed by keys/certs; avoid full ASN.1 generality in phase 1. |
| [ ] | Backend context | 2 | `Backend/CryptoContext.hpp`, `Backend/BackendCapabilities.hpp`, `Backend/BackendInfo.hpp`, `Backend/BackendOptions.hpp` | `Backends/BackendDispatch.cpp` | Fake backend tests | Stable neutral contract for algorithms, capabilities, diagnostics, and policy. No public backend-specific C++ classes. |
| [ ] | Backend registry/selection | 2 | Optional neutral context factory only | `Backends/BackendDispatch.cpp` | Selection/lifetime tests | Optional convenience; no mutable global unless policy is explicit. |
| [ ] | Platform random backend | 1 | None beyond `Random/*` and neutral backend capability reporting | `Random/SecureRandom.*.cpp`, `Backends/PlatformRandomBackend.cpp` | Same as random tests | Only randomness in core backend at first; not a user-included backend type. |
| [ ] | Windows CNG backend | 3 | None | `Backends/CngBackend.win32.cpp` | Backend known-answer tests on Windows | Native algorithms through BCrypt/NCrypt where supported. |
| [ ] | Apple security backend | 3 | None | `Backends/AppleSecurityBackend.apple.cpp` | Backend known-answer tests on Apple | Use Security/CommonCrypto/CryptoKit availability carefully. |
| [ ] | OpenSSL backend | 3 | None | `Backends/OpenSslBackend.cpp` | Backend known-answer tests | Optional approved dependency/package, not default NGIN.Base core dependency. |
| [ ] | BoringSSL backend | 4 | None | `Backends/BoringSslBackend.cpp` | Backend known-answer tests | Only if workspace needs it separately from OpenSSL. |
| [ ] | Libsodium backend | 3 | None | `Backends/LibsodiumBackend.cpp` | Backend known-answer tests | Best fit for XChaCha20-Poly1305, Ed25519, X25519, Argon2id. |
| [ ] | Hash abstraction | 2 | `Hashing/Hash.hpp`, `Hashing/HashAlgorithm.hpp`, `Hashing/Digest.hpp` | `Hashing/Hash.cpp` | Hash API tests | Common one-shot, streaming, fixed digest type contracts. |
| [ ] | SHA-256 | 3 | `Hashing/Sha256.hpp` | Backend-backed | NIST/RFC known-answer tests, benchmark | Required baseline hash. |
| [ ] | SHA-512 | 3 | `Hashing/Sha512.hpp` | Backend-backed | Known-answer tests, benchmark | Required for HMAC-SHA512 and interoperability. |
| [ ] | SHA-3 | 4 | `Hashing/Sha3.hpp` | Backend-backed | Known-answer tests | Add if backend support and use cases justify it. |
| [ ] | BLAKE3 | 4 | `Hashing/Blake3.hpp` | Backend-backed or approved package | Official vector tests, benchmark | High-performance optional algorithm; dependency approval required. |
| [ ] | MAC abstraction | 2 | `Mac/Mac.hpp`, `Mac/MacAlgorithm.hpp` | `Mac/Hmac.cpp` | MAC API tests | Keep tag length and verification explicit. |
| [ ] | HMAC-SHA256 | 3 | `Mac/HmacSha256.hpp` | Backend-backed | RFC vector tests | Preferred baseline HMAC. |
| [ ] | HMAC-SHA512 | 3 | `Mac/HmacSha512.hpp` | Backend-backed | RFC vector tests | Add with SHA-512. |
| [ ] | KDF abstraction | 2 | `Kdf/KeyDerivation.hpp` | None or backend dispatch | KDF API tests | Inputs are spans; outputs caller-owned or secure buffers. |
| [ ] | HKDF | 3 | `Kdf/Hkdf.hpp` | Backend-backed or HMAC composition if backend-approved | RFC 5869 vectors | Extract and expand API plus one-shot derive. |
| [ ] | PBKDF2 | 3 | `Kdf/Pbkdf2.hpp` | Backend-backed | Known-answer tests | Interop only; docs should prefer Argon2id for password storage. |
| [ ] | Argon2id | 3 | `Kdf/Argon2id.hpp` | Backend-backed | RFC 9106 vectors, parameter validation tests | Do not implement core algorithm in-house. |
| [ ] | AEAD abstraction | 2 | `Symmetric/Aead.hpp`, `Symmetric/AeadAlgorithm.hpp` | `Symmetric/Aead.cpp` | AEAD API tests | Seal/open only; no unauthenticated encryption first-class API. |
| [ ] | AES-GCM | 3 | `Symmetric/AesGcm.hpp` | Backend-backed | NIST vectors, invalid tag tests, benchmark | Nonce-size policy must be explicit; 96-bit nonce fast path. |
| [ ] | ChaCha20-Poly1305 | 3 | `Symmetric/ChaCha20Poly1305.hpp` | Backend-backed | RFC 8439 vectors, invalid tag tests | Prefer where AES acceleration is unavailable. |
| [ ] | XChaCha20-Poly1305 | 3 | `Symmetric/XChaCha20Poly1305.hpp` | Libsodium or approved backend | Known-answer tests | Preferred random-nonce AEAD when backend exists. |
| [ ] | SecretBox | 4 | `Symmetric/SecretBox.hpp` | Backend-backed | Roundtrip and invalid tag tests | Convenience wrapper over XChaCha20-Poly1305 or backend equivalent. |
| [ ] | Key type wrappers | 2 | `Asymmetric/KeyTypes.hpp`, `PublicKey.hpp`, `PrivateKey.hpp`, `KeyPair.hpp` | `Asymmetric/KeyTypes.cpp` | Type size, move, wipe tests | Strong algorithm-specific key types prevent accidental key reuse. |
| [ ] | Ed25519 | 3 | `Asymmetric/Ed25519.hpp`, `Signatures/Sign.hpp`, `Verify.hpp`, `Signature.hpp` | Backend-backed | RFC 8032 vectors | Preferred signature API. |
| [ ] | X25519 | 3 | `Asymmetric/X25519.hpp` | Backend-backed | RFC 7748 vectors | Key agreement only; pair with HKDF for derived keys. |
| [ ] | ECDSA | 4 | `Asymmetric/Ecdsa.hpp` | Backend-backed | Backend vectors and DER signature tests | Interop API; avoid making it the default recommendation. |
| [ ] | RSA | 4 | `Asymmetric/Rsa.hpp` | Backend-backed | PSS/OAEP tests only | Interop API; no PKCS#1 v1.5 signing as a recommended default. |
| [ ] | Password policy | 3 | Optional low-level parameter validation only | Optional | Parameter validation tests | Application login policy, migration rules, and recommended settings belong in a higher-level security package. |
| [ ] | Password hashing | 3 | `Passwords/PasswordHash.hpp`, `PasswordVerify.hpp` | `Passwords/PasswordHash.cpp` | OWASP-aligned tests, rehash-needed tests | Backend-backed PHC string handling may live in Crypto; policy defaults and account-flow behavior do not. |
| [ ] | Secure token generator | 2 | `Tokens/TokenGenerator.hpp`, `Tokens/SecureToken.hpp` | `Tokens/TokenGenerator.cpp` | Length, alphabet, entropy tests | Random opaque tokens first; simplest safe token primitive. |
| [ ] | JWT | 4 | `Tokens/Jwt.hpp` | `Tokens/Jwt.cpp` | Strict parse, alg allowlist, claim validation tests | Must require explicit allowed algorithms and validation policy. |
| [ ] | PASETO | 4 | `Tokens/Paseto.hpp` | `Tokens/Paseto.cpp` | Official vectors | Prefer over JWT for new designs if backend support is practical. |
| [ ] | Certificate model | 4 | `Certificates/Certificate.hpp`, `CertificateChain.hpp` | `Certificates/Certificate.cpp` | Parse and lifetime tests | Lightweight handles over backend-owned X.509 objects. |
| [ ] | X.509 parser/view | 4 | `Certificates/X509.hpp` | `Certificates/X509.cpp` | Malformed cert tests | Backend-backed parse and validation, strict ownership. |
| [ ] | Certificate store | 4 | `Certificates/CertificateStore.hpp` | `Certificates/CertificateStore.cpp` | Platform store tests | Platform differences must be documented. |
| [ ] | Net TLS handoff | 5 | None under `NGIN::Crypto` | None under `src/NGIN/Crypto` | Integration tests in Net | TLS context, client, server, sockets, handshakes, ALPN, and certificate validation flow belong to `NGIN::Net`. |
| [ ] | Benchmarks | 2-5 | None | None | `benchmarks/Crypto*.cpp` | Track throughput, allocation counts, and backend overhead. |
| [ ] | User guide | 2 | `docs/Crypto.md`, `include/NGIN/Crypto/README.md` | None | Documentation review | Practical guide with safe defaults and examples. |

## Ordered Implementation Phases

### Phase 0: Remove Ambiguity

- Freeze this plan around the settled boundaries:
  - Crypto owns primitives, memory, encoding, randomness, neutral backend context/capability contracts, and
    backend-backed algorithm wrappers.
  - Net owns TLS.
  - NGIN.Base core owns OS randomness and native OS backends only when they do not introduce external dependencies.
  - OpenSSL, BoringSSL, and libsodium live behind separate workspace packages or package wrappers.
- Rename or delete duplicate old headers as part of the migration plan:
  - `CryptoRandom.hpp`, `CryptoRandom.impl.hpp`, `CryptoRandom.posix.hpp`, `CryptoRandom.win32.hpp`
  - old flat `Hash.hpp`, `Hasher.hpp`, `HMAC.hpp`, `KDF.hpp`, `Symmetric.hpp`, `SymmetricCipher.hpp`,
    `AsymmetricCipher.hpp`, `Signature.hpp`, `Encoding.hpp`, `SecureBuffer.hpp`, `Utils.hpp`
- Add migration shims only if downstream code requires a short transition. Otherwise prefer a clean breaking change.

### Phase 1: Safe Base Layer

- Add `CryptoError`, `CryptoExpected`, core types, and namespace conventions.
- Implement `Memory/ZeroMemory`, `Memory/ConstantTime`, and `Memory/SecureBuffer`.
- Implement OS-backed `RandomBytes` with platform-specific source files.
- Implement `Hex`, `Base64`, and `Base64Url`.
- Add focused tests under `tests/Crypto/`.
- Add only standard-library-compatible source files to `NGIN_BASE_CORE_SOURCES`.

### Phase 2: Backend Contract

- Add neutral backend capability model and fake backend test adapter.
- Add algorithm-neutral hashing, MAC, KDF, AEAD, key, and signature API contracts.
- Add documentation for context lifetimes, thread-safety, FIPS mode reporting, and capability probing.
- Do not add a public mutable backend registry. Selection is explicit context construction, build/package composition, or
  an immutable process-default context initialized once from policy.
- Add benchmarks for dispatch overhead and buffer allocation behavior.

### Phase 3: First Real Backend Set

- Add one approved real backend path and make it pass known-answer tests. Approved ownership is:
  - Windows CNG inside NGIN.Base when it uses platform APIs only.
  - Apple Security/CommonCrypto/CryptoKit inside NGIN.Base where APIs are available and no external dependency is added.
  - OpenSSL, BoringSSL, and libsodium through separate workspace packages or package wrappers.
- Implement SHA-256, SHA-512, HMAC, HKDF, PBKDF2, Argon2id, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519, and
  password hashing where backend capabilities exist.
- Add invalid authentication tag, invalid signature, invalid key, invalid nonce, and output-too-small tests.

### Phase 4: Interoperability Layer

- Add PEM/DER, RSA-PSS/OAEP, ECDSA, X.509, certificate stores, JWT, and PASETO.
- Require strict parse options and explicit validation policies.
- Add malformed input corpora and differential tests against backend behavior where feasible.

### Phase 5: TLS Integration

- Implement TLS under `NGIN::Net`, not `NGIN::Crypto`.
- Add Net TLS context, client, and server only after backend lifetime, certificate validation, and networking integration
  are stable enough to compose cleanly.
- Add local TLS fixture tests and platform-specific certificate store tests.

## Tests

Add focused test files under `tests/Crypto/`:

```text
tests/Crypto/
  CryptoError.cpp
  ConstantTime.cpp
  SecureBuffer.cpp
  Random.cpp
  Encoding.cpp
  Backend.cpp
  Hash.cpp
  Hmac.cpp
  Kdf.cpp
  Aead.cpp
  Asymmetric.cpp
  Signature.cpp
  Password.cpp
  Token.cpp
  Certificate.cpp
```

Test requirements:

- Every public API gets positive and negative tests.
- All backend-backed algorithms get official known-answer vectors.
- All authentication APIs test tampered data and wrong keys.
- All parsers test malformed, truncated, oversized, and duplicate-field inputs where relevant.
- Secret storage tests verify move behavior and destruction-time wipe behavior where practical.
- Randomness tests verify API behavior, sizes, and error mapping, not statistical quality beyond basic sanity checks.

## Benchmarks

Add benchmarks only after the API shape is stable:

```text
benchmarks/CryptoRandomBenchmarks.cpp
benchmarks/CryptoEncodingBenchmarks.cpp
benchmarks/CryptoHashBenchmarks.cpp
benchmarks/CryptoAeadBenchmarks.cpp
benchmarks/CryptoBackendBenchmarks.cpp
```

Benchmark dimensions:

- One-shot vs preallocated output.
- Small, medium, and large payloads.
- Backend dispatch overhead.
- Allocation counts for convenience APIs.
- AES-GCM vs ChaCha20-Poly1305 on machines with and without AES acceleration.

## Documentation Deliverables

- `docs/Crypto.md`: user guide with safe defaults, examples, and "what not to use" guidance.
- `include/NGIN/Crypto/README.md`: contributor-facing API map, invariants, and header ownership.
- Header-level Doxygen summaries for all public classes and functions.
- Backend support matrix:

| Backend | Random | SHA/HMAC/HKDF | PBKDF2 | Argon2id | AES-GCM | ChaCha20-Poly1305 | XChaCha20-Poly1305 | Ed25519 | X25519 | X.509/TLS |
|---|---|---|---|---|---|---|---|---|---|---|
| Platform random | Yes | No | No | No | No | No | No | No | No | No |
| Windows CNG | Yes | Yes | Yes | No | Yes | Maybe | No | Maybe | Maybe | Partial |
| Apple Security/CommonCrypto/CryptoKit | Yes | Yes | Yes | No | Yes | Maybe | No | Maybe | Maybe | Partial |
| OpenSSL | Yes | Yes | Yes | Backend/version dependent | Yes | Yes | No | Yes | Yes | Yes |
| BoringSSL | Yes | Yes | Yes | No | Yes | Yes | No | Yes | Yes | Yes |
| Libsodium | Yes | Limited | No | Yes | No | Yes | Yes | Yes | Yes | No |

The matrix must be verified against the exact backend versions used by the workspace before it becomes user-facing
documentation.

## CMake and Packaging

- Add core Crypto sources to `NGIN_BASE_CORE_SOURCES` only for standard-library-compatible and platform OS code.
- Add native OS backends to NGIN.Base only when they do not add external production dependencies.
- Add external crypto engines through package wrappers or higher-level workspace packages, not as default Base
  dependencies. Candidate package-facing options include:
  - `NGIN_BASE_CRYPTO_WITH_OPENSSL`
  - `NGIN_BASE_CRYPTO_WITH_BORINGSSL`
  - `NGIN_BASE_CRYPTO_WITH_LIBSODIUM`
- Prefer package wrappers in the wider NGIN workspace when backend integration is about composition or external source
  ownership.
- Backend-specific tests should be skipped when the backend is unavailable, not silently downgraded to another backend.

## Security Review Gates

Before any backend-backed algorithm is considered complete:

- Known-answer vectors pass.
- Invalid input and tampering tests pass.
- Key, nonce, tag, digest, and signature sizes are strongly typed or validated.
- Error paths do not leak partial plaintext as a successful result.
- Secret buffers are wiped on destruction and move.
- No debug logs print key material, plaintext secrets, nonces tied to secret contexts, or backend internal handles.
- Thread-safety and lifetime rules are documented.
- Sanitizers pass for changed code.
- Benchmarks show no accidental allocation in preallocated APIs.

## Settled Architecture Decisions

- TLS lives in `NGIN::Net`, not `NGIN::Crypto`.
- NGIN.Base core includes OS randomness, secure memory, constant-time helpers, encodings, error/result types, neutral
  backend context contracts, and native OS backends that do not add external dependencies.
- OpenSSL, BoringSSL, libsodium, and similar external engines are integrated through separate workspace packages or
  package wrappers.
- Backend-backed algorithms require an explicit `CryptoContext` until an immutable process-default context is
  implemented. There is no public mutable backend registry.
- Crypto exposes a `ByteBuffer` alias for ordinary owned bytes. Sensitive material uses `Secret`, `SecretView`,
  `SecureBuffer`, or dedicated secure byte storage.
- Low-level backend-backed password hash encoding/verification may live in Crypto. Password policy defaults, account
  migration rules, and login-flow guidance belong in a higher-level security package or application layer.

## Reference Standards and Guidance

- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final) for AES-GCM mode.
- [NIST FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final) for SHA-256 and SHA-512.
- [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) for HKDF.
- [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439) for ChaCha20-Poly1305.
- [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) for X25519.
- [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032) for Ed25519.
- [RFC 9106](https://www.rfc-editor.org/rfc/rfc9106) for Argon2id.
- [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)
  for password hashing policy guidance.
