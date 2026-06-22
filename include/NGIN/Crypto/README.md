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
| `Encoding/*` | strict Hex, Base64, and Base64Url codecs |
| `Backend/*` | neutral context, capability bits, diagnostic backend metadata |
| `Hashing/*` | hash algorithm contracts and SHA convenience wrappers |
| `Mac/*` | MAC contracts and HMAC convenience wrappers |
| `Kdf/*` | HKDF, PBKDF2, and Argon2id parameter contracts |
| `Symmetric/*` | AEAD-only symmetric encryption contracts |
| `Asymmetric/*` | strongly typed Ed25519 and X25519 key material |
| `Signatures/*` | signature generation and verification contracts |
| `Tokens/*` | opaque secure token generation |

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

`NGIN_BASE_CRYPTO_OPENSSL` / `NGIN_CRYPTO_WITH_OPENSSL` enable the current optional OpenSSL-backed implementation path.
External engines such as BoringSSL or libsodium should be introduced through package wrappers or explicitly approved
workspace composition rather than as unconditional Base dependencies.

## Extension Guidance

- Add leaf headers before adding umbrella exports.
- Add known-answer tests for every backend-backed algorithm.
- Add invalid size, key, nonce, tag, signature, and tampering tests for authentication APIs.
- Validate output buffer sizes before calling a backend.
- Keep parser APIs strict by default.
- Keep TLS, socket, ALPN, and certificate-validation flow in `NGIN::Net`.

## Testing Guidance

Focused tests live under `tests/Crypto/`. New public APIs need positive and negative tests. Backend-specific algorithm
tests should skip or assert `UnsupportedAlgorithm` when the configured backend lacks a capability; they must not silently
exercise a different backend.
