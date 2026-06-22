# Crypto

`NGIN::Crypto` provides low-level cryptographic building blocks for NGIN.Base:

- recoverable error/result types,
- byte-oriented API contracts,
- secure memory helpers,
- platform secure randomness,
- strict text/binary encodings,
- neutral backend capability reporting,
- backend-backed hash, MAC, KDF, AEAD, signature, key-agreement, and RSA interop wrappers.

The module owns wrapper contracts and safety boundaries. It does not implement cryptographic primitives in-house.
Algorithms such as SHA-256, HMAC, HKDF, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519, RSA-PSS/SHA-256, and
RSA-OAEP/SHA-256 are available only when the selected `CryptoContext` reports support from an approved backend such as
the optional OpenSSL build path.

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
- `Encoding::ParsePem`
- `Encoding::DerReader` and DER writer helpers

Base64Url is intended for URL-safe opaque data such as random tokens. Higher-level token formats such as JWT and
PASETO use the strict token parsers instead of ad hoc Base64Url splitting.

`ParsePem` accepts strict RFC 7468-style blocks, enforces an optional label allowlist, normalizes line endings, rejects
malformed Base64, and applies a maximum decoded size per block. It only returns the PEM label and decoded bytes; key,
certificate, and trust interpretation belong in higher layers.

`DerReader` is a bounded strict TLV reader for DER data. It rejects indefinite lengths, non-minimal length encodings, and
oversized elements. Helper APIs cover INTEGER, BIT STRING, OCTET STRING, OBJECT IDENTIFIER, SEQUENCE, and SET without
building a general ASN.1 object model.

SubjectPublicKeyInfo and PKCS#8 PrivateKeyInfo helpers sit above DER. They identify common key OIDs, preserve raw key
bytes, and do not import keys into a backend or validate certificate/trust policy.
Algorithm-specific key wrappers provide fixed-array construction and runtime-checked span construction through
`PublicKey::FromBytes(...)` and `PrivateKey::FromSecretBytes(...)`. These helpers validate byte length and secret
storage handling; backend operations still decide whether the bytes are cryptographically valid for an algorithm.
`ImportEd25519PublicKey`, `ImportX25519PublicKey`, `ImportEcdsaP256PublicKey`, `ImportEd25519PrivateKey`,
`ImportX25519PrivateKey`, and `ImportEcdsaP256PrivateKey` bridge parsed SPKI/PKCS#8 data into the typed key wrappers
with algorithm and size checks. The ECDSA P-256 public-key bridge accepts only 65-byte uncompressed points. Matching
`ExportSubjectPublicKeyInfo` and `ExportPrivateKeyInfo` overloads write typed wrappers back to backend-neutral key-format
structs.
`ParseEncryptedPrivateKeyInfo` and `WriteEncryptedPrivateKeyInfo` support strict data-only PKCS#8 encrypted private-key
envelopes. They preserve the encryption AlgorithmIdentifier and encrypted payload bytes, but they do not decrypt,
interpret password KDFs, or select cipher policy.

RSA helpers are exposed through `Asymmetric/Rsa.hpp` for interoperability with existing key material. They use complete
DER SubjectPublicKeyInfo public keys and DER PKCS#8 private keys at the API boundary, and currently cover
RSA-PSS/SHA-256 signing/verification plus RSA-OAEP/SHA-256 encryption/decryption when an OpenSSL-compatible provider is
selected. They intentionally do not expose PKCS#1 v1.5 signing as a preferred default.

The X.509 helper parses certificate structure into lightweight owned fields: serial, issuer/subject DER, validity text,
SubjectPublicKeyInfo, signature algorithm, signature bytes, and selected extensions such as subjectAltName, keyUsage,
extendedKeyUsage, subjectKeyIdentifier, and authorityKeyIdentifier. It also exposes best-effort issuer and subject RDN
attributes for common name, organization, organizational unit, country, locality, state/province, serial number, domain
component, and email address OIDs while preserving raw DER for policy layers. Trust decisions, hostname validation,
revocation, and platform store selection remain outside `NGIN::Crypto`.

`CertificateStore` currently supports explicit custom/in-memory certificate collections and lookup by subject DER,
subject key identifier, and authority key identifier. On Linux, platform root-store opening loads common distribution CA
bundle paths such as `/etc/ssl/certs/ca-certificates.crt`. On Windows, it enumerates the current-user `ROOT` and `CA`
system certificate stores. On macOS, it enumerates Apple Security trust anchors. `CertificateStoreInfo` reports the
store kind, operating system, source, source path, loaded/skipped certificate counts, and a compatibility diagnostic
string. Call `OpenPlatformRootCertificateStoreWithDiagnostics()` when callers need per-source failure details; the
compact `OpenPlatformRootCertificateStore()` API returns only the selected store or error. These APIs expose parsed
certificate material and lookup helpers only; path validation and trust policy remain outside `NGIN::Crypto`.

`TlsCredentialMaterial` is a data-only handoff object for future `NGIN::Net` TLS integration. It carries a parsed
certificate chain and PKCS#8 private key info; TLS context/session behavior still belongs in `NGIN::Net`.

JWT compact validation is strict and policy-driven through `Tokens::ValidateJwt`. It rejects `alg=none`, requires an
explicit allowed-algorithm policy, enforces size limits and duplicate-field rejection, validates issuer/audience/time
policy when configured, and delegates HS256, PS256, ES256, or EdDSA signature checks to the selected backend. ES256 uses
raw uncompressed P-256 public keys and fixed raw `r || s` signatures at the JWT boundary; PS256 uses DER
SubjectPublicKeyInfo public keys. After parsing, use `HasJwtClaim`, `GetJwtStringClaim`, `GetJwtInt64Claim`, and
`GetJwtBoolClaim` for typed standard or custom claim reads without exposing the JSON DOM through the token API.

PASETO v4.public validation is strict and policy-driven through `Tokens::ValidatePasetoV4Public`. It enforces payload,
footer, and implicit-assertion limits, rejects duplicate payload JSON fields, checks expected footer policy before
signature verification, and delegates Ed25519 verification to the selected backend. PASETO v4.local seal/open support is
available through `Tokens::SealPasetoV4Local` and `Tokens::OpenPasetoV4Local` when the selected backend is the optional
libsodium provider; it uses provider-backed BLAKE2b and raw XChaCha20, verifies footer policy and implicit assertions,
and returns plaintext payload JSON only after authentication succeeds. After parsing/opening, use `HasPasetoClaim`,
`GetPasetoStringClaim`, `GetPasetoInt64Claim`, and `GetPasetoBoolClaim` for typed payload reads.

## Backend Contexts

Backend-backed algorithms require an explicit context. The default helper preserves the strongest compiled provider
surface first, then falls back to platform facilities:

```cpp
auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
if (!context.HasValue())
{
    return context.Error();
}

auto digest = NGIN::Crypto::Hashing::Sha256(context.Value(), message);
```

Use `CreatePlatformContext()` when you explicitly want only fresh-machine platform facilities. On Windows builds with
`NGIN_BASE_CRYPTO_WITH_CNG=ON`, that platform context can expose CNG-backed SHA-2, HMAC, PBKDF2, and AES-GCM. On
non-CNG builds, platform context currently means secure random only. Use `CreatePackageContext("openssl")` when a
package provider must be selected by name.

Check capabilities before selecting optional algorithms:

```cpp
if (!context.Value().Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm))
{
    return NGIN::Crypto::CryptoError {NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm};
}
```

Use `DescribeSupport(...)` when diagnostics matter:

```cpp
auto support = context.Value().DescribeSupport(NGIN::Crypto::HashAlgorithm::Sha256);
if (!support.supported)
{
    return support.reason;
}
```

Use `CreateContextWithDiagnostics()` when startup tooling needs to explain rejected providers:

```cpp
auto selection = NGIN::Crypto::Backend::CreateContextWithDiagnostics({
        .policy = NGIN::Crypto::Backend::BackendPolicy::RequireAlgorithmSet,
});

if (!selection.context.HasValue())
{
    for (NGIN::UIntSize i = 0; i < selection.diagnostics.Count(); ++i)
    {
        auto entry = selection.diagnostics[i];
        // entry.backend.Name(), entry.code, and entry.reason are safe to log.
    }
}
```

You can also fail once at startup by requiring an algorithm set:

```cpp
auto required = NGIN::Crypto::Backend::AlgorithmSet {}
                        .Require(NGIN::Crypto::HashAlgorithm::Sha256)
                        .Require(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

auto context = NGIN::Crypto::Backend::CreateContext({
        .policy             = NGIN::Crypto::Backend::BackendPolicy::RequireAlgorithmSet,
        .requiredAlgorithms = required,
});
```

No public mutable backend registry exists. A context is explicit state with diagnostic metadata and immutable capability
bits.

Provider implementations are expected to pass the shared conformance vectors in `tests/Crypto/ProviderConformance.cpp`.
The same suite is used for best-available, platform-only, and named package contexts where those providers are compiled.

`Packages/OpenSSL` provides a CMake `FindPackage` wrapper for `OpenSSL::Crypto`. Its package metadata carries the
`NGIN_BASE_CRYPTO_WITH_OPENSSL=ON` option, so workspace-generated CMake can enable the NGIN.Base provider path when the
package is present in the resolved graph.
`Packages/libsodium` provides equivalent package metadata/discovery for `libsodium::libsodium`; when
`NGIN_BASE_CRYPTO_WITH_LIBSODIUM=ON` and the target can be found, NGIN.Base can dispatch XChaCha20-Poly1305, Argon2id,
Ed25519, X25519, and secure random through libsodium. `Packages/BoringSSL` provides metadata/discovery for
`BoringSSL::Crypto`; when `NGIN_BASE_CRYPTO_WITH_BORINGSSL=ON` and a real BoringSSL target or library/header layout is
available, NGIN.Base dispatches through the OpenSSL-compatible private adapter with `boringssl` provider identity.

The `examples/CryptoCapabilities` executable prints selected backend metadata, capability diagnostics, and a generated
opaque token. It is the quickest way to inspect what a configured build can actually do.

The workspace CLI also exposes lightweight runtime diagnostics. `ngin crypto info` prints the backend selected by the
CLI process and capability status for the supported requirement names. `ngin crypto explain --algorithm aes-256-gcm`
prints the selected backend metadata and the support or unsupported reason for one algorithm. Add
`--project <file.nginproj>` to `crypto explain` when you also want the resolved workspace graph to show which selected
package feature declares the matching `Crypto.Algorithm.*` capability and which `Crypto.Provider.*` backend candidates
are present. The project-aware output also includes provider package identity, linkage support, and runtime artifact
hints declared by the selected package wrapper. Both commands accept `--format json` for machine-readable diagnostics.

The `examples/CryptoCookbook` executable demonstrates the main user-facing flows with explicit capability checks:
random tokens, SHA-256, AES-256-GCM, Ed25519, X25519 plus HKDF, PEM-to-X.509 parsing with subjectAltName extraction,
and JWT validation with an explicit policy. Provider-backed operations print clear skip reasons when the selected
backend does not support them, so the example remains useful on fresh machines.

## Fresh-Machine Behavior

The default source of portable crypto on a clean machine is the platform. Package-backed providers add broader algorithm
coverage when they are installed, restored, and enabled at build time.

| Host/build | Fresh-machine backend | Algorithms available without external packages |
| --- | --- | --- |
| Windows with `NGIN_BASE_CRYPTO_WITH_CNG=ON` | `cng` | random, SHA-2, HMAC-SHA2, PBKDF2-SHA2, AES-GCM |
| Linux default | `platform-random` | random |
| macOS with `NGIN_BASE_CRYPTO_WITH_APPLE=ON` | `apple` | random, SHA-2, HMAC-SHA2, PBKDF2-SHA2 |
| Any host with `NGIN_BASE_CRYPTO_WITH_OPENSSL=ON` and `OpenSSL::Crypto` | `openssl` package backend | random plus SHA-2, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519 |

For startup diagnostics:

- Use `CreatePlatformContext()` to see only what the fresh platform can provide.
- Use `CreateBestAvailableContext()` to prefer compiled package providers, then platform facilities.
- Use `CreateContextWithDiagnostics()` when logs or tooling should explain why a provider was rejected.
- Use `NGIN_BASE_CRYPTO_REQUIRE_PROVIDER` and `NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS` to fail configuration instead of
  discovering missing support at runtime.

Troubleshooting common failures:

- `UnsupportedAlgorithm`: the selected backend exists but does not claim that algorithm; inspect
  `context.DescribeSupport(...)`.
- `BackendUnavailable`: a named provider such as `openssl`, `boringssl`, or `libsodium` was requested but was not
  compiled into this NGIN.Base build.
- `UnsupportedBackend`: the provider name or platform facility is not implemented on this host.
- CMake cannot find OpenSSL: install or restore a package that exports `OpenSSL::Crypto`, then configure with
  `NGIN_BASE_CRYPTO_WITH_OPENSSL=ON`.
- CMake cannot find libsodium: install or restore a package that provides `sodium.h` and a sodium/libsodium library,
  then configure with `NGIN_BASE_CRYPTO_WITH_LIBSODIUM=ON`.
- CMake cannot find BoringSSL: install or restore a package that exports `BoringSSL::Crypto`, or provide a
  BoringSSL `openssl/base.h` plus `libcrypto` layout, then configure with `NGIN_BASE_CRYPTO_WITH_BORINGSSL=ON`.
- Do not enable OpenSSL and BoringSSL together; NGIN.Base treats them as mutually exclusive OpenSSL-compatible package
  providers.

## Provider Install And Restore

There are three supported ways to make a package-backed provider available:

1. System or SDK install: install the provider with your OS/toolchain and configure NGIN.Base directly.

```bash
cmake -S Dependencies/NGIN/NGIN.Base -B build/base-openssl \
  -DNGIN_BASE_CRYPTO_WITH_OPENSSL=ON \
  -DNGIN_BASE_CRYPTO_REQUIRE_PROVIDER=openssl
```

2. Workspace package wrapper plus external provider: use a package manifest whose `<Build>` names a provider, and declare
   that external restore provider in the workspace manifest. A vcpkg-shaped workspace entry looks like:

```xml
<Packages>
  <PackageProvider Name="OpenSSL" Root="Packages/OpenSSL" />
  <Provider Name="vcpkg" Kind="Vcpkg" Root="Tools/vcpkg" Triplet="x64-windows" />
</Packages>
```

The OpenSSL package manifest uses `Build Mode="FindPackage"` and carries the NGIN.Base enable option. If the package is
resolved through an external provider, `ngin restore` writes a vcpkg manifest containing the provider package name, runs
`vcpkg install`, records the vcpkg toolchain and install prefix, and generated CMake consumes that metadata. The
provider binding belongs on the package build metadata:

```xml
<Build Backend="CMake"
       Mode="FindPackage"
       Provider="vcpkg"
       ProviderPackage="openssl"
       ProviderVersion="3.0.0"
       CMakePackage="OpenSSL"
       Linkage="Static;Shared"
       RuntimeDeployment="PackageRuntimeLibraries"
       RuntimeArtifacts="libcrypto">
  <Options>
    <Option Name="NGIN_BASE_CRYPTO_WITH_OPENSSL" Value="ON" />
  </Options>
</Build>
```

The committed `Packages/OpenSSL`, `Packages/libsodium`, and `Packages/BoringSSL` wrappers are provider-neutral discovery
wrappers. They declare default provider package names and versions where the upstream ecosystem has a stable package
identity, plus linkage and runtime artifact hints for diagnostics and future staging. They do not choose vcpkg, Conan,
or another acquisition tool by themselves. Use them as-is for system packages or CMake prefix paths. Add `Provider` in a
project-local wrapper or package override when the dependency should be acquired by `ngin restore`.

3. Conan-backed restore: use the same package wrapper, but declare a Conan provider:

```xml
<Packages>
  <PackageProvider Name="OpenSSL" Root="Packages/OpenSSL" />
  <Provider Name="conan" Kind="Conan" Profile="default" />
</Packages>
```

`ngin restore` writes a `conanfile.txt` for the provider packages, runs `conan install --build=missing`, records the
generated prefix path, and generated CMake passes that prefix path to package discovery.

Then restore and build the consuming project:

```bash
./build/dev/Tools/NGIN.CLI/ngin restore --project path/to/App.nginproj --profile Debug
./build/dev/Tools/NGIN.CLI/ngin build --project path/to/App.nginproj --profile Debug --output build/manual/App
```

Important limits:

- Restore is explicit; ordinary build expects provider metadata to exist when a package uses an external provider.
- vcpkg restore requires a provider `Root` or `VCPKG_ROOT` so the generated build can use the vcpkg toolchain file.
- Conan restore requires `conan` on `PATH` or a provider `Root` that contains the executable.
- `Packages/BoringSSL` normalizes BoringSSL layouts but does not prove a BoringSSL installation exists on every host.
- A clean Windows machine can use platform CNG for baseline algorithms without OpenSSL, but OpenSSL/BoringSSL/libsodium
  still require either an installed package or an explicit restore provider.

## Build-Time Requirements

Use CMake requirement options when an application or package cannot run without specific crypto support:

```bash
cmake -S Dependencies/NGIN/NGIN.Base -B build/base \
  -DNGIN_BASE_CRYPTO_WITH_OPENSSL=ON \
  -DNGIN_BASE_CRYPTO_REQUIRE_PROVIDER=openssl \
  -DNGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS=sha256,hmac-sha256,aes-256-gcm
```

Provider names: `platform`, `platform-random`, `apple`, `cng`, `openssl`, `boringssl`, `libsodium`. The libsodium
provider is available when `NGIN_BASE_CRYPTO_WITH_LIBSODIUM=ON` finds a usable libsodium package/header/library.
BoringSSL is available when `NGIN_BASE_CRYPTO_WITH_BORINGSSL=ON` finds `BoringSSL::Crypto` or a BoringSSL
library/header layout. The fallback refuses non-BoringSSL OpenSSL-compatible headers.

Algorithm names: `random`, `sha256`, `sha512`, `hmac-sha256`, `hmac-sha512`, `hkdf-sha256`, `hkdf-sha512`,
`pbkdf2-sha256`, `pbkdf2-sha512`, `aes-128-gcm`, `aes-256-gcm`, `chacha20-poly1305`,
`xchacha20-poly1305`, `ecdsa-p256-sha256`, `ed25519`, `rsa-oaep-sha256`, `rsa-pss-sha256`, `x25519`, `argon2id`.

Requirement lists may be separated with semicolons, commas, or spaces. Unknown names or unavailable providers/algorithms
fail configuration.

## Hashes, MACs, And KDFs

Preferred baseline wrappers:

- `Hashing::Sha256Into`, `Hashing::Sha512Into`
- `Hashing::Sha256`, `Hashing::Sha512`
- `Mac::HmacSha256Into`, `Mac::HmacSha512Into`
- `Mac::HmacSha256`, `Mac::HmacSha512`
- `Kdf::HkdfSha256Into`, `Kdf::HkdfSha512Into`
- `Kdf::HkdfSha256`, `Kdf::HkdfSha512`
- `Kdf::HkdfSha256Secret<N>`, `Kdf::HkdfSha512Secret<N>`
- `Kdf::Pbkdf2Sha256`, `Kdf::Pbkdf2Sha512`
- `Kdf::Pbkdf2Sha256Secret<N>`, `Kdf::Pbkdf2Sha512Secret<N>`
- `Kdf::HashPassword`, `Kdf::VerifyPassword`, and `Kdf::PasswordHashNeedsRehash` when Argon2id string storage is
  available through a selected backend

PBKDF2 exists for interoperability. Password storage should prefer Argon2id through a backend/package that implements
it; NGIN.Base does not hand-roll Argon2id. The password-hash helpers produce and verify PHC-style Argon2id strings
through the selected `CryptoContext`. Today that means the optional libsodium backend; builds without Argon2id support
return `UnsupportedAlgorithm`.

## Authenticated Encryption

Use AEAD APIs only:

- `AeadAlgorithm::Aes256Gcm`
- `AeadAlgorithm::ChaCha20Poly1305`
- `AeadAlgorithm::XChaCha20Poly1305` when a backend provides it
- typed key and nonce generators such as `Symmetric::GenerateAes256GcmKey`,
  `Symmetric::GenerateAesGcmNonce`, `Symmetric::GenerateChaCha20Poly1305Key`, and
  `Symmetric::GenerateChaCha20Poly1305Nonce`
- typed helpers such as `Symmetric::SealAes256Gcm`, `Symmetric::OpenAes256Gcm`,
  `Symmetric::SealChaCha20Poly1305`, and `Symmetric::OpenChaCha20Poly1305`

There is no first-class unauthenticated encryption API. A failed open returns `AuthenticationFailed` or a size/input
error and must not be treated as plaintext success.

## Signatures And Key Agreement

Preferred modern algorithms:

- Ed25519 for signatures
- X25519 for key agreement

Both require backend support. X25519 produces shared secret material; derive final keys with HKDF and protocol-specific
context info. ECDSA P-256/SHA-256 is available as an OpenSSL-compatible interoperability path through
`Asymmetric::SignEcdsaP256Sha256` and `Asymmetric::VerifyEcdsaP256Sha256`; it uses fixed 64-byte raw `r || s`
signatures at the API boundary and should not replace Ed25519 as the default choice for new designs. Use
`Asymmetric::EncodeEcdsaP256Sha256SignatureDer` and `Asymmetric::ParseEcdsaP256Sha256SignatureDer` at protocol
boundaries that require DER ECDSA signatures. RSA-PSS/SHA-256 and RSA-OAEP/SHA-256 are available through
`Asymmetric/Rsa.hpp` with DER SPKI/PKCS#8 key input when an OpenSSL-compatible provider is selected.

## What Is Not In Crypto

TLS contexts, sockets, handshakes, ALPN, and certificate validation flow belong in `NGIN::Net`.

The following remain future package or backend integration work until there is a concrete owner and approved dependency
path:

- X.509 path validation and trust policy,
- RSA provider support beyond the current OpenSSL-compatible PSS/OAEP DER-key interop path and ECDSA support beyond the
  current OpenSSL-compatible P-256/SHA-256 raw-signature path,
- additional JWT/PASETO algorithms and modes beyond the current strict HS256/PS256/ES256/EdDSA JWT and v4.public/v4.local
  PASETO validation support,
- actual BoringSSL CI/provider-vector verification,
- Apple CommonCrypto/Security adapters beyond SHA-2, HMAC-SHA2, and PBKDF2-SHA2.

Unsupported algorithms must return `UnsupportedAlgorithm` or `UnsupportedBackend`. Do not silently downgrade to a weaker
algorithm.

The forward plan for native platform providers, package-backed providers, and parser-heavy network features is tracked
in [CryptoPlatformAndInteropPlan.md](/home/berggrenmille/NGIN/Dependencies/NGIN/NGIN.Base/docs/CryptoPlatformAndInteropPlan.md).

## References

- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final) for GCM/GMAC.
- [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) for HKDF.
- [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439) for ChaCha20-Poly1305.
- [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) for X25519.
- [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032) for Ed25519.
- [RFC 9106](https://www.rfc-editor.org/rfc/rfc9106) for Argon2id.
- [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)
  for password-storage policy guidance.
