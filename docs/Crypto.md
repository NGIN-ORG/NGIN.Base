# Crypto

`NGIN::Crypto` provides low-level cryptographic building blocks for NGIN.Base:

- recoverable error/result types,
- byte-oriented API contracts,
- secure memory helpers,
- platform secure randomness,
- strict text/binary encodings,
- neutral backend capability reporting,
- backend-backed hash, MAC, KDF, AEAD, signature, and key-agreement wrappers.

The module owns wrapper contracts and safety boundaries. It does not implement cryptographic primitives in-house.
Algorithms such as SHA-256, HMAC, HKDF, AES-GCM, ChaCha20-Poly1305, Ed25519, and X25519 are available only when the
selected `CryptoContext` reports support from an approved backend such as the optional OpenSSL build path.

## Include Surface

Use the umbrella header when compile time is not a concern:

```cpp
#include <NGIN/Crypto/Crypto.hpp>
```

Use leaf headers in public headers or hot build paths:

```cpp
#include <NGIN/Crypto/Random/RandomBytes.hpp>
#include <NGIN/Crypto/Hashing/Sha256.hpp>
#include <NGIN/Crypto/Symmetric/AesGcm.hpp>
```

## Results And Errors

Recoverable failures use:

```cpp
NGIN::Crypto::CryptoExpected<T>
```

`CryptoErrorCode` distinguishes invalid arguments, output-buffer size errors, unsupported algorithms, backend
unavailability, entropy failure, parse errors, authentication failure, and policy rejection. Verification APIs return
diagnostic errors instead of collapsing everything to `false`.

## Random Bytes

Use platform secure randomness for keys, nonces that are specified as random, and opaque tokens:

```cpp
auto key = NGIN::Crypto::Random::RandomBytes<32>();
if (!key.HasValue())
{
    return key.Error();
}
```

For caller-owned storage:

```cpp
std::array<NGIN::Byte, 24> nonce {};
auto result = NGIN::Crypto::Random::Fill({nonce.data(), nonce.size()});
```

`EntropySource` is a non-owning source view for backend adapters and deterministic tests. Do not mark deterministic test
sources as cryptographically secure.

## Secret Storage

Use ordinary `ByteBuffer` only for non-secret owned bytes such as digests, public keys, ciphertext, or encoded output.
Use secret storage for keys, passwords, private keys, and derived secret material:

```cpp
NGIN::Crypto::Memory::FixedSecret<32> key;
auto bytes = key.UnsafeMutableBytes();
```

Secret storage is move-only where practical and wipes memory on destruction and move. Avoid converting secrets to
strings. Text passwords should be accepted as transient byte spans and moved into secure storage as early as possible.

## Encodings

Use strict encoders and decoders:

- `Encoding::HexEncode` / `HexDecode`
- `Encoding::Base64Encode` / `Base64Decode`
- `Encoding::Base64UrlEncode` / `Base64UrlDecode`

Base64Url is intended for URL-safe opaque data such as random tokens. Token parsing formats with claims or signatures
are not part of the current Base crypto surface.

## Backend Contexts

Backend-backed algorithms require an explicit context:

```cpp
auto context = NGIN::Crypto::Backend::CreateContext();
if (!context.HasValue())
{
    return context.Error();
}

auto digest = NGIN::Crypto::Hashing::Sha256(message, context.Value());
```

Check capabilities before selecting optional algorithms:

```cpp
if (!context.Value().Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm))
{
    return NGIN::Crypto::CryptoError {NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm};
}
```

No public mutable backend registry exists. A context is explicit state with diagnostic metadata and immutable capability
bits.

## Hashes, MACs, And KDFs

Preferred baseline wrappers:

- `Hashing::Sha256Into`, `Hashing::Sha512Into`
- `Mac::HmacSha256Into`, `Mac::HmacSha512Into`
- `Kdf::HkdfSha256Into`, `Kdf::HkdfSha512Into`

PBKDF2 exists for interoperability. Password storage should prefer Argon2id through a backend/package that implements
it; NGIN.Base does not hand-roll Argon2id.

## Authenticated Encryption

Use AEAD APIs only:

- `AeadAlgorithm::Aes256Gcm`
- `AeadAlgorithm::ChaCha20Poly1305`
- `AeadAlgorithm::XChaCha20Poly1305` when a backend provides it

There is no first-class unauthenticated encryption API. A failed open returns `AuthenticationFailed` or a size/input
error and must not be treated as plaintext success.

## Signatures And Key Agreement

Preferred modern algorithms:

- Ed25519 for signatures
- X25519 for key agreement

Both require backend support. X25519 produces shared secret material; derive final keys with HKDF and protocol-specific
context info.

## What Is Not In Crypto

TLS contexts, sockets, handshakes, ALPN, and certificate validation flow belong in `NGIN::Net`.

The following remain future package or backend integration work until there is a concrete owner and approved dependency
path:

- PEM/DER and X.509 object models,
- RSA and ECDSA interoperability wrappers,
- JWT and PASETO token formats,
- BoringSSL and libsodium package adapters,
- platform CNG/CommonCrypto algorithm adapters beyond platform randomness.

Unsupported algorithms must return `UnsupportedAlgorithm` or `UnsupportedBackend`. Do not silently downgrade to a weaker
algorithm.

## References

- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final) for GCM/GMAC.
- [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) for HKDF.
- [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439) for ChaCha20-Poly1305.
- [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) for X25519.
- [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032) for Ed25519.
- [RFC 9106](https://www.rfc-editor.org/rfc/rfc9106) for Argon2id.
- [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)
  for password-storage policy guidance.
