#include <NGIN/Crypto/Certificates/CertificateStore.hpp>

#include <NGIN/Crypto/Encoding/Pem.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/ConstantTime.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace NGIN::Crypto::Certificates
{
    namespace
    {
        [[nodiscard]] bool BytesEqual(ConstByteSpan left, ConstByteSpan right) noexcept
        {
            if (left.size() != right.size())
            {
                return false;
            }

            return NGIN::Crypto::Memory::ConstantTimeEqual(left, right);
        }

        [[nodiscard]] constexpr CryptoError UnsupportedBackend() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedBackend};
        }

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] std::string CurrentOperatingSystem()
        {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#elif defined(__linux__)
            return "linux";
#else
            return "unsupported";
#endif
        }

        [[nodiscard]] CertificateStoreInfo MakePlatformStoreInfo(
                std::string_view name,
                std::string_view source,
                std::string_view sourcePath,
                bool             available,
                std::string      diagnostic = {})
        {
            return CertificateStoreInfo {
                    .kind            = CertificateStoreKind::PlatformRoot,
                    .name            = std::string {name},
                    .operatingSystem = CurrentOperatingSystem(),
                    .source          = std::string {source},
                    .sourcePath      = std::string {sourcePath},
                    .platformBacked  = true,
                    .available       = available,
                    .diagnostic      = std::move(diagnostic),
            };
        }

        void AddDiagnostic(
                NGIN::Containers::Vector<CertificateStoreOpenDiagnostic>& diagnostics,
                CertificateStoreInfo                                      info,
                CryptoErrorCode                                           code,
                std::string                                               reason)
        {
            diagnostics.PushBack(CertificateStoreOpenDiagnostic {
                    .info   = std::move(info),
                    .code   = code,
                    .reason = std::move(reason),
            });
        }

        bool TryAppendParsedCertificate(
                NGIN::Containers::Vector<Certificate>& certificates,
                ConstByteSpan                          der,
                NGIN::UIntSize&                        skipped)
        {
            auto parsed = ParseX509Certificate(der);
            if (parsed.HasValue())
            {
                certificates.PushBack(std::move(parsed.Value()));
                return true;
            }

            ++skipped;
            return false;
        }

#if defined(__linux__)
        constexpr std::array<std::string_view, 4> LINUX_CA_BUNDLE_PATHS {
                "/etc/ssl/certs/ca-certificates.crt",
                "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/ssl/ca-bundle.pem",
                "/etc/ssl/cert.pem",
        };

        [[nodiscard]] CryptoExpected<std::string> ReadTextFile(std::string_view path) noexcept
        {
            try
            {
                std::ifstream input {std::string {path}, std::ios::binary};
                if (!input)
                {
                    return UnsupportedBackend();
                }

                return std::string {
                        std::istreambuf_iterator<char> {input},
                        std::istreambuf_iterator<char> {},
                };
            } catch (...)
            {
                return CryptoError {CryptoErrorCode::InternalError};
            }
        }

        [[nodiscard]] CryptoExpected<CertificateStore> OpenPemCertificateBundle(
                std::string_view bundlePath,
                std::string_view storeName) noexcept
        {
            auto content = ReadTextFile(bundlePath);
            if (!content.HasValue())
            {
                return content.Error();
            }
            if (content.Value().empty())
            {
                return UnsupportedBackend();
            }

            auto blocks = NGIN::Crypto::Encoding::ParsePem(
                    content.Value(),
                    NGIN::Crypto::Encoding::PemParseOptions {
                            .allowedLabels   = {"CERTIFICATE"},
                            .maxDecodedBytes = 1u << 20,
                    });
            if (!blocks.HasValue())
            {
                return blocks.Error();
            }

            NGIN::Containers::Vector<Certificate> certificates;
            NGIN::UIntSize                        skipped = 0;
            for (const auto& block: blocks.Value())
            {
                TryAppendParsedCertificate(
                        certificates,
                        ConstByteSpan {block.decoded.data(), block.decoded.Size()},
                        skipped);
            }

            if (certificates.Size() == 0)
            {
                return ParseError();
            }

            auto diagnostic = std::string {"loaded "} + std::to_string(certificates.Size()) + " certificates from " +
                              std::string {bundlePath} + "; skipped " + std::to_string(skipped);
            CertificateStoreInfo info {
                    .kind                = CertificateStoreKind::PlatformRoot,
                    .name                = std::string {storeName},
                    .operatingSystem     = "linux",
                    .source              = "system-ca-bundle",
                    .sourcePath          = std::string {bundlePath},
                    .certificatesLoaded  = certificates.Size(),
                    .certificatesSkipped = skipped,
                    .platformBacked      = true,
                    .available           = true,
                    .diagnostic          = std::move(diagnostic),
            };

            return CertificateStore {std::move(info), std::move(certificates)};
        }
#elif defined(_WIN32)
        struct WindowsCertificateStoreHandle
        {
            HCERTSTORE value {nullptr};

            WindowsCertificateStoreHandle() noexcept = default;
            explicit WindowsCertificateStoreHandle(HCERTSTORE handle) noexcept
                : value {handle}
            {
            }

            WindowsCertificateStoreHandle(const WindowsCertificateStoreHandle&)            = delete;
            WindowsCertificateStoreHandle& operator=(const WindowsCertificateStoreHandle&) = delete;

            WindowsCertificateStoreHandle(WindowsCertificateStoreHandle&& other) noexcept
                : value {std::exchange(other.value, nullptr)}
            {
            }

            WindowsCertificateStoreHandle& operator=(WindowsCertificateStoreHandle&& other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    value = std::exchange(other.value, nullptr);
                }
                return *this;
            }

            ~WindowsCertificateStoreHandle()
            {
                Reset();
            }

            void Reset() noexcept
            {
                if (value != nullptr)
                {
                    CertCloseStore(value, 0);
                    value = nullptr;
                }
            }
        };

        [[nodiscard]] CryptoExpected<void> AppendWindowsCertificateStore(
                std::wstring_view                      storeName,
                NGIN::Containers::Vector<Certificate>& certificates,
                NGIN::UIntSize&                        skipped) noexcept
        {
            WindowsCertificateStoreHandle store {
                    CertOpenSystemStoreW(static_cast<HCRYPTPROV_LEGACY>(0), storeName.data())};
            if (store.value == nullptr)
            {
                return UnsupportedBackend();
            }

            PCCERT_CONTEXT context = nullptr;
            while ((context = CertEnumCertificatesInStore(store.value, context)) != nullptr)
            {
                TryAppendParsedCertificate(
                        certificates,
                        ConstByteSpan {
                                reinterpret_cast<const NGIN::Byte*>(context->pbCertEncoded),
                                static_cast<NGIN::UIntSize>(context->cbCertEncoded),
                        },
                        skipped);
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<CertificateStore> OpenWindowsNativeCertificateStore(
                NGIN::Containers::Vector<CertificateStoreOpenDiagnostic>& diagnostics) noexcept
        {
            NGIN::Containers::Vector<Certificate> certificates;
            NGIN::UIntSize                        skipped        = 0;
            bool                                  openedAnyStore = false;

            for (const auto storeName: {std::wstring_view {L"ROOT"}, std::wstring_view {L"CA"}})
            {
                auto       appended   = AppendWindowsCertificateStore(storeName, certificates, skipped);
                const auto sourcePath = storeName == std::wstring_view {L"ROOT"} ? std::string {"ROOT"} : std::string {"CA"};
                if (appended.HasValue())
                {
                    openedAnyStore = true;
                    AddDiagnostic(
                            diagnostics,
                            MakePlatformStoreInfo(
                                    "windows-system-certificates",
                                    "native-windows-certificate-store",
                                    sourcePath,
                                    true,
                                    "opened native Windows certificate store"),
                            CryptoErrorCode::None,
                            "opened native Windows certificate store");
                }
                else
                {
                    AddDiagnostic(
                            diagnostics,
                            MakePlatformStoreInfo(
                                    "windows-system-certificates",
                                    "native-windows-certificate-store",
                                    sourcePath,
                                    false,
                                    "could not open native Windows certificate store"),
                            appended.Error().Code(),
                            appended.Error().Message());
                }
            }

            if (!openedAnyStore)
            {
                return UnsupportedBackend();
            }
            if (certificates.Size() == 0)
            {
                return ParseError();
            }

            auto diagnostic = std::string {"loaded "} + std::to_string(certificates.Size()) +
                              " certificates from Windows ROOT/CA stores; skipped " + std::to_string(skipped);
            CertificateStoreInfo info {
                    .kind                = CertificateStoreKind::PlatformRoot,
                    .name                = "windows-system-certificates",
                    .operatingSystem     = "windows",
                    .source              = "native-windows-certificate-store",
                    .sourcePath          = "ROOT;CA",
                    .certificatesLoaded  = certificates.Size(),
                    .certificatesSkipped = skipped,
                    .platformBacked      = true,
                    .available           = true,
                    .diagnostic          = std::move(diagnostic),
            };

            return CertificateStore {std::move(info), std::move(certificates)};
        }
#elif defined(__APPLE__)
        struct CoreFoundationObject
        {
            CFTypeRef value {nullptr};

            CoreFoundationObject() noexcept = default;
            explicit CoreFoundationObject(CFTypeRef object) noexcept
                : value {object}
            {
            }

            CoreFoundationObject(const CoreFoundationObject&)            = delete;
            CoreFoundationObject& operator=(const CoreFoundationObject&) = delete;

            CoreFoundationObject(CoreFoundationObject&& other) noexcept
                : value {std::exchange(other.value, nullptr)}
            {
            }

            CoreFoundationObject& operator=(CoreFoundationObject&& other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    value = std::exchange(other.value, nullptr);
                }
                return *this;
            }

            ~CoreFoundationObject()
            {
                Reset();
            }

            void Reset() noexcept
            {
                if (value != nullptr)
                {
                    CFRelease(value);
                    value = nullptr;
                }
            }
        };

        [[nodiscard]] CryptoExpected<CertificateStore> OpenAppleAnchorCertificateStore(
                NGIN::Containers::Vector<CertificateStoreOpenDiagnostic>& diagnostics) noexcept
        {
            CFArrayRef anchors = nullptr;
            const auto status  = SecTrustCopyAnchorCertificates(&anchors);
            if (status != errSecSuccess || anchors == nullptr)
            {
                AddDiagnostic(
                        diagnostics,
                        MakePlatformStoreInfo(
                                "macos-system-anchors",
                                "native-apple-security-trust-anchors",
                                "SecTrustCopyAnchorCertificates",
                                false,
                                "could not copy Apple trust anchor certificates"),
                        CryptoErrorCode::UnsupportedBackend,
                        "could not copy Apple trust anchor certificates");
                return UnsupportedBackend();
            }

            CoreFoundationObject anchorOwner {anchors};
            const auto           count = CFArrayGetCount(anchors);

            NGIN::Containers::Vector<Certificate> certificates;
            NGIN::UIntSize                        skipped = 0;

            for (CFIndex i = 0; i < count; ++i)
            {
                auto* value = CFArrayGetValueAtIndex(anchors, i);
                if (value == nullptr || CFGetTypeID(value) != SecCertificateGetTypeID())
                {
                    ++skipped;
                    continue;
                }

                auto* data = SecCertificateCopyData(reinterpret_cast<SecCertificateRef>(value));
                if (data == nullptr)
                {
                    ++skipped;
                    continue;
                }

                CoreFoundationObject dataOwner {data};
                TryAppendParsedCertificate(
                        certificates,
                        ConstByteSpan {
                                reinterpret_cast<const NGIN::Byte*>(CFDataGetBytePtr(data)),
                                static_cast<NGIN::UIntSize>(CFDataGetLength(data)),
                        },
                        skipped);
            }

            if (certificates.Size() == 0)
            {
                return ParseError();
            }

            auto diagnostic = std::string {"loaded "} + std::to_string(certificates.Size()) +
                              " certificates from Apple trust anchors; skipped " + std::to_string(skipped);
            CertificateStoreInfo info {
                    .kind                = CertificateStoreKind::PlatformRoot,
                    .name                = "macos-system-anchors",
                    .operatingSystem     = "macos",
                    .source              = "native-apple-security-trust-anchors",
                    .sourcePath          = "SecTrustCopyAnchorCertificates",
                    .certificatesLoaded  = certificates.Size(),
                    .certificatesSkipped = skipped,
                    .platformBacked      = true,
                    .available           = true,
                    .diagnostic          = std::move(diagnostic),
            };

            AddDiagnostic(diagnostics, info, CryptoErrorCode::None, "loaded Apple trust anchor certificates");
            return CertificateStore {std::move(info), std::move(certificates)};
        }
#endif
    }// namespace

    CertificateStore::CertificateStore(CertificateStoreInfo info, NGIN::Containers::Vector<Certificate> certificates)
        : m_info {std::move(info)}, m_certificates {std::move(certificates)}
    {
    }

    const CertificateStoreInfo& CertificateStore::Info() const noexcept
    {
        return m_info;
    }

    NGIN::UIntSize CertificateStore::Size() const noexcept
    {
        return m_certificates.Size();
    }

    bool CertificateStore::Empty() const noexcept
    {
        return m_certificates.Size() == 0;
    }

    const Certificate& CertificateStore::operator[](NGIN::UIntSize index) const noexcept
    {
        return m_certificates[index];
    }

    CryptoExpected<CertificateStoreLookupResult> CertificateStore::FindBySubjectDer(ConstByteSpan subjectDer) const
    {
        CertificateStoreLookupResult result;

        for (const auto& certificate: m_certificates)
        {
            if (BytesEqual(ConstByteSpan {certificate.subjectDer.data(), certificate.subjectDer.Size()}, subjectDer))
            {
                result.certificates.PushBack(certificate);
            }
        }

        return result;
    }

    CryptoExpected<CertificateStoreLookupResult> CertificateStore::FindBySubjectKeyIdentifier(
            ConstByteSpan keyIdentifier) const
    {
        CertificateStoreLookupResult result;

        for (const auto& certificate: m_certificates)
        {
            if (certificate.hasSubjectKeyIdentifier &&
                BytesEqual(
                        ConstByteSpan {certificate.subjectKeyIdentifier.data(), certificate.subjectKeyIdentifier.Size()},
                        keyIdentifier))
            {
                result.certificates.PushBack(certificate);
            }
        }

        return result;
    }

    CryptoExpected<CertificateStoreLookupResult> CertificateStore::FindByAuthorityKeyIdentifier(
            ConstByteSpan keyIdentifier) const
    {
        CertificateStoreLookupResult result;

        for (const auto& certificate: m_certificates)
        {
            if (certificate.hasAuthorityKeyIdentifier &&
                BytesEqual(
                        ConstByteSpan {certificate.authorityKeyIdentifier.data(), certificate.authorityKeyIdentifier.Size()},
                        keyIdentifier))
            {
                result.certificates.PushBack(certificate);
            }
        }

        return result;
    }

    CryptoExpected<CertificateStore> CreateCustomCertificateStore(NGIN::Containers::Vector<Certificate> certificates)
    {
        return CertificateStore {
                CertificateStoreInfo {
                        .kind               = CertificateStoreKind::Custom,
                        .name               = "custom",
                        .operatingSystem    = {},
                        .source             = "custom",
                        .sourcePath         = {},
                        .certificatesLoaded = certificates.Size(),
                        .platformBacked     = false,
                        .available          = true,
                        .diagnostic         = {},
                },
                std::move(certificates),
        };
    }

    CryptoExpected<CertificateStore> OpenPlatformRootCertificateStore() noexcept
    {
        return OpenPlatformRootCertificateStoreWithDiagnostics().store;
    }

    CertificateStoreOpenSelection OpenPlatformRootCertificateStoreWithDiagnostics() noexcept
    {
#if defined(__linux__)
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
        for (const auto path: LINUX_CA_BUNDLE_PATHS)
        {
            auto store = OpenPemCertificateBundle(path, "linux-system-roots");
            if (store.HasValue())
            {
                auto info = store.Value().Info();
                AddDiagnostic(diagnostics, info, CryptoErrorCode::None, "loaded platform root certificate bundle");
                return CertificateStoreOpenSelection {
                        .store       = std::move(store),
                        .diagnostics = std::move(diagnostics),
                };
            }

            auto reason = std::string {"could not load platform root certificate bundle from "} + std::string {path};
            auto info   = MakePlatformStoreInfo(
                    "linux-system-roots",
                    "system-ca-bundle",
                    path,
                    false,
                    std::move(reason));
            AddDiagnostic(diagnostics, std::move(info), store.Error().Code(), store.Error().Message());

            if (store.Error().Code() != CryptoErrorCode::UnsupportedBackend)
            {
                return CertificateStoreOpenSelection {
                        .store       = store.Error(),
                        .diagnostics = std::move(diagnostics),
                };
            }
        }

        return CertificateStoreOpenSelection {
                .store       = UnsupportedBackend(),
                .diagnostics = std::move(diagnostics),
        };
#elif defined(_WIN32)
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
        auto                                                     store = OpenWindowsNativeCertificateStore(diagnostics);
        return CertificateStoreOpenSelection {
                .store       = std::move(store),
                .diagnostics = std::move(diagnostics),
        };
#elif defined(__APPLE__)
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
        auto                                                     store = OpenAppleAnchorCertificateStore(diagnostics);
        return CertificateStoreOpenSelection {
                .store       = std::move(store),
                .diagnostics = std::move(diagnostics),
        };
#else
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
        auto                                                     info = MakePlatformStoreInfo(
                "platform-roots",
                "native-platform-store",
                {},
                false,
                "platform root store is not implemented for this operating system");
        AddDiagnostic(
                diagnostics,
                std::move(info),
                CryptoErrorCode::UnsupportedBackend,
                "platform root store is not implemented for this operating system");

        return CertificateStoreOpenSelection {
                .store       = UnsupportedBackend(),
                .diagnostics = std::move(diagnostics),
        };
#endif
    }
}// namespace NGIN::Crypto::Certificates
