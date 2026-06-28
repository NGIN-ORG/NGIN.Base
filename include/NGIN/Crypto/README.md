# NGIN Crypto Module

Backend-backed crypto contracts, secure memory helpers, encodings, and platform randomness.

## Components

| Component | Purpose |
|-----------|---------|
| `Types.hpp` | `ByteSpan`, `ConstByteSpan`, and fixed byte arrays |
| `Result.hpp` | `CryptoExpected<T>` alias |
| `Errors/*` | Stable error categories and diagnostics |
| `Memory/*` | constant-time compare, secure zeroing, secret buffers and views |
| `Random/*` | platform secure random bytes and `EntropySource` test/backend source view |
| `Encoding/*` | strict Hex, Base64, Base64Url, PEM, and DER codecs/parsers |
| `Backend/*` | neutral context, capability bits, diagnostic backend metadata |
| `Certificates/*` | lightweight X.509 certificate, chain, store, and Net handoff helpers |
| `Hashing/*` | hash algorithm contracts and SHA convenience wrappers |
| `Keys/*` | strict SPKI/PKCS#8 key-format parsing/writing helpers, parsed-key operation bridges, and provider-backed PBES2 encrypted private-key decrypt |
| `Mac/*` | MAC contracts and HMAC convenience wrappers |
| `Kdf/*` | HKDF, PBKDF2, Argon2id, and provider-backed password-hash string contracts |
| `Symmetric/*` | AEAD-only symmetric encryption contracts |
| `Asymmetric/*` | strongly typed Ed25519, ECDSA P-256, X25519, and provider-backed RSA interop helpers |
| `Signatures/*` | signature generation and verification contracts |
| `Tokens/*` | opaque secure token generation, strict JWT validation, and PASETO v4.public/v4.local token support |

## Design Principles

1. API contracts live in public headers; non-trivial implementation lives in `src/NGIN/Crypto`.
2. Public APIs use byte spans and fixed byte wrappers, not strings.
3. Recoverable failures return `CryptoExpected<T>`.
4. Backend-backed algorithms require an explicit neutral `CryptoContext`.
5. Backend names may appear in diagnostics, source files, CMake, and tests; backend-specific public C++ classes are not
   part of the stable include surface.
6. NGIN.Base does not hand-roll cryptographic primitives.
7. Secret material uses secure storage and is wiped on destruction and move.
8. Unsupported algorithms fail explicitly; no silent fallback to weaker algorithms.
9. JWT uses the Base JSON parser; token formats are the only Crypto surface that depends on Serialization.
10. PASETO supports v4.public validation and libsodium-backed v4.local seal/open; application authorization policy stays above Crypto.

## Backend Model

The Base core always owns platform secure randomness. Algorithm implementations are provided by explicit backend support:

```cpp
auto context = NGIN::Crypto::Backend::CreateContext();
if (!context.HasValue())
{
    return context.Error();
}

if (!context.Value().Supports(NGIN::Crypto::HashAlgorithm::Sha256))
{
    return NGIN::Crypto::CryptoError {NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm};
}
```

`NGIN_BASE_CRYPTO_WITH_OPENSSL` enables the current optional OpenSSL-backed implementation path.
`NGIN_BASE_CRYPTO_WITH_BORINGSSL` enables the optional BoringSSL-backed path through the same OpenSSL-compatible private
adapter when a real BoringSSL target or library/header layout is available. It is mutually exclusive with
`NGIN_BASE_CRYPTO_WITH_OPENSSL`.
`NGIN_BASE_CRYPTO_WITH_LIBSODIUM` enables the optional libsodium-backed path for XChaCha20-Poly1305, Ed25519, X25519,
and Argon2id when a usable libsodium package/header/library is available.
`NGIN_BASE_CRYPTO_OPENSSL` and `NGIN_CRYPTO_WITH_OPENSSL` remain compatibility aliases. External engines such as
BoringSSL or libsodium should be introduced through package wrappers or explicitly approved workspace composition rather
than as unconditional Base dependencies.

`NGIN_BASE_CRYPTO_REQUIRE_PROVIDER` and `NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS` fail CMake configuration when a build does
not provide required crypto support. Requirement values accept semicolon-, comma-, or space-separated provider/algorithm
identifiers.

Context creation helpers are explicit:

- `CreateBestAvailableContext()` prefers compiled package providers, then platform facilities.
- `CreatePlatformContext()` selects only platform facilities. On Windows with `NGIN_BASE_CRYPTO_WITH_CNG=ON`, this can
  expose CNG-backed SHA-2, HMAC, PBKDF2, and AES-GCM. On Apple platforms with `NGIN_BASE_CRYPTO_WITH_APPLE=ON`, this
  can expose CommonCrypto-backed SHA-2, HMAC, and PBKDF2.
- `CreatePackageContext("openssl")` or `CreatePackageContext("libsodium")` selects a named package provider.
- `BackendOptions::requiredAlgorithms` lets applications fail once at startup when required capabilities are missing.
- `CryptoContext::DescribeSupport(...)` reports a selected backend's reason when an algorithm is unavailable.
- `CreateContextWithDiagnostics()` reports rejected backend candidates for startup and tooling diagnostics.

One-shot helpers remain context-explicit. Use `Hashing::Sha256`, `Mac::HmacSha256`, `Kdf::HkdfSha256`,
`Kdf::HkdfSha256Secret<N>`, `Kdf::HashPassword`, `Kdf::VerifyPassword`, `Symmetric::GenerateAes256GcmKey`,
`Symmetric::SealAes256Gcm`, and similar typed wrappers when the algorithm is known at the call site. Use the generic
enum-driven APIs when the algorithm is policy-selected at runtime.

Use `PublicKey::FromBytes(ConstByteSpan)` and `PrivateKey::FromSecretBytes(ConstByteSpan)` when key material comes from
a parser or byte buffer. These helpers validate size and storage shape; backend-backed operations still validate
algorithm-specific key semantics.

RSA helpers are interop-focused and provider-backed. `Asymmetric/Rsa.hpp` accepts DER SubjectPublicKeyInfo public keys
and DER PKCS#8 private keys for RSA-PSS/SHA-256 and RSA-OAEP/SHA-256 through providers that advertise those capabilities.

## Extension Guidance

- Add leaf headers before adding umbrella exports.
- Add known-answer tests for every backend-backed algorithm.
- Add invalid size, key, nonce, tag, signature, and tampering tests for authentication APIs.
- Validate output buffer sizes before calling a backend.
- Keep parser APIs strict by default.
- Keep PEM parsing separate from key, certificate, and trust interpretation.
- Keep TLS, socket, ALPN, and certificate-validation flow in `NGIN::Net`.
- Keep platform trust-store loading explicit; Linux CA bundle loading exists, while native Windows/macOS stores remain
  future platform work. Use `OpenPlatformRootCertificateStoreWithDiagnostics()` when reporting why platform store
  selection failed or which OS/package source was used.

## Testing Guidance

Focused tests live under `tests/Crypto/`. New public APIs need positive and negative tests. Backend-specific algorithm
tests should skip or assert `UnsupportedAlgorithm` when the configured backend lacks a capability; they must not silently
exercise a different backend.

Shared provider vectors live under `tests/Crypto/ProviderVectors/` and are exercised by
`tests/Crypto/ProviderConformance.cpp`. New providers should pass that suite before being treated as supported.
