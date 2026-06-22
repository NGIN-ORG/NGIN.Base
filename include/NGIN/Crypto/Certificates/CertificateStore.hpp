#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Certificates/Certificate.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <string>

namespace NGIN::Crypto::Certificates
{
    enum class CertificateStoreKind : NGIN::UInt8
    {
        Custom,
        PlatformRoot,
    };

    struct CertificateStoreInfo
    {
        CertificateStoreKind kind {CertificateStoreKind::Custom};
        std::string          name;
        std::string          operatingSystem;
        std::string          source;
        std::string          sourcePath;
        NGIN::UIntSize       certificatesLoaded {0};
        NGIN::UIntSize       certificatesSkipped {0};
        bool                 platformBacked {false};
        bool                 available {true};
        std::string          diagnostic;
    };

    struct CertificateStoreOpenDiagnostic
    {
        CertificateStoreInfo info;
        CryptoErrorCode      code {CryptoErrorCode::None};
        std::string          reason;
    };

    struct CertificateStoreLookupResult
    {
        NGIN::Containers::Vector<Certificate> certificates;
    };

    class CertificateStore
    {
    public:
        CertificateStore() = default;

        explicit CertificateStore(CertificateStoreInfo info, NGIN::Containers::Vector<Certificate> certificates = {});

        [[nodiscard]] const CertificateStoreInfo& Info() const noexcept;
        [[nodiscard]] NGIN::UIntSize              Size() const noexcept;
        [[nodiscard]] bool                        Empty() const noexcept;
        [[nodiscard]] const Certificate&          operator[](NGIN::UIntSize index) const noexcept;

        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindBySubjectDer(ConstByteSpan subjectDer) const;
        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindBySubjectKeyIdentifier(
                ConstByteSpan keyIdentifier) const;
        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindByAuthorityKeyIdentifier(
                ConstByteSpan keyIdentifier) const;

    private:
        CertificateStoreInfo                  m_info;
        NGIN::Containers::Vector<Certificate> m_certificates;
    };

    struct CertificateStoreOpenSelection
    {
        CryptoExpected<CertificateStore>                         store;
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
    };

    [[nodiscard]] CryptoExpected<CertificateStore> CreateCustomCertificateStore(
            NGIN::Containers::Vector<Certificate> certificates);

    [[nodiscard]] CryptoExpected<CertificateStore> OpenPlatformRootCertificateStore() noexcept;

    [[nodiscard]] CertificateStoreOpenSelection OpenPlatformRootCertificateStoreWithDiagnostics() noexcept;
}// namespace NGIN::Crypto::Certificates
