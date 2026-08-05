/// @file CertificateStore.hpp
/// @brief Custom and platform-root certificate stores with lookup diagnostics.
#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Certificates/Certificate.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <string>

namespace NGIN::Crypto::Certificates
{
    /// @brief Origin of certificates contained in a store.
    enum class CertificateStoreKind : NGIN::UInt8
    {
        Custom,
        PlatformRoot,
    };

    /// @brief Metadata and load statistics for a certificate store.
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

    /// @brief Failure or rejection recorded while opening a certificate store.
    struct CertificateStoreOpenDiagnostic
    {
        CertificateStoreInfo info;
        CryptoErrorCode      code {CryptoErrorCode::None};
        std::string          reason;
    };

    /// @brief Certificates matching a store lookup.
    struct CertificateStoreLookupResult
    {
        NGIN::Containers::Vector<Certificate> certificates;
    };

    /// @brief Immutable collection of parsed certificates and store metadata.
    class NGIN_CRYPTO_API CertificateStore
    {
    public:
        /// @brief Constructs an empty custom store.
        CertificateStore() = default;

        /// @brief Constructs a store from metadata and owned certificates.
        explicit CertificateStore(CertificateStoreInfo info, NGIN::Containers::Vector<Certificate> certificates = {});

        /// @brief Returns store origin, diagnostics, and load statistics.
        [[nodiscard]] const CertificateStoreInfo& Info() const noexcept;
        /// @brief Returns the number of loaded certificates.
        [[nodiscard]] NGIN::UIntSize Size() const noexcept;
        /// @brief Returns whether the store contains no certificates.
        [[nodiscard]] bool Empty() const noexcept;
        /// @brief Returns a certificate without bounds checking.
        [[nodiscard]] const Certificate& operator[](NGIN::UIntSize index) const noexcept;

        /// @brief Finds certificates whose encoded subject name exactly matches `subjectDer`.
        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindBySubjectDer(ConstByteSpan subjectDer) const;
        /// @brief Finds certificates with a matching subject key identifier extension.
        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindBySubjectKeyIdentifier(
                ConstByteSpan keyIdentifier) const;
        /// @brief Finds certificates with a matching authority key identifier extension.
        [[nodiscard]] CryptoExpected<CertificateStoreLookupResult> FindByAuthorityKeyIdentifier(
                ConstByteSpan keyIdentifier) const;

    private:
        CertificateStoreInfo                  m_info;
        NGIN::Containers::Vector<Certificate> m_certificates;
    };

    /// @brief Store-opening result plus diagnostics from rejected platform sources.
    struct CertificateStoreOpenSelection
    {
        CryptoExpected<CertificateStore>                         store;
        NGIN::Containers::Vector<CertificateStoreOpenDiagnostic> diagnostics;
    };

    /// @brief Creates an owned custom store from parsed certificates.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CertificateStore> CreateCustomCertificateStore(
            NGIN::Containers::Vector<Certificate> certificates);

    /// @brief Opens the operating system's trusted root certificate store.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<CertificateStore> OpenPlatformRootCertificateStore() noexcept;

    /// @brief Opens the platform root store and retains diagnostics from attempted sources.
    [[nodiscard]] NGIN_CRYPTO_API CertificateStoreOpenSelection OpenPlatformRootCertificateStoreWithDiagnostics() noexcept;
}// namespace NGIN::Crypto::Certificates
