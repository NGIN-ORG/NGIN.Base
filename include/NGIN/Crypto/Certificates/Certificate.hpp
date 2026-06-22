#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Keys/KeyFormat.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <string>

namespace NGIN::Crypto::Certificates
{
    struct CertificateValidity
    {
        std::string notBefore;
        std::string notAfter;
    };

    enum class DistinguishedNameAttributeType : NGIN::UInt8
    {
        Unknown,
        CommonName,
        CountryName,
        OrganizationName,
        OrganizationalUnitName,
        LocalityName,
        StateOrProvinceName,
        SerialNumber,
        DomainComponent,
        EmailAddress,
    };

    struct DistinguishedNameAttribute
    {
        DistinguishedNameAttributeType         type {DistinguishedNameAttributeType::Unknown};
        NGIN::Containers::Vector<NGIN::UInt32> objectIdentifier;
        std::string                            value;
        NGIN::UInt32                           valueTag {12};
    };

    struct DistinguishedName
    {
        NGIN::Containers::Vector<DistinguishedNameAttribute> attributes;
    };

    struct SubjectAltNames
    {
        NGIN::Containers::Vector<std::string> dnsNames;
        NGIN::Containers::Vector<std::string> emailAddresses;
        NGIN::Containers::Vector<ByteBuffer>  ipAddresses;
    };

    struct KeyUsage
    {
        NGIN::UInt8 unusedBitCount {0};
        ByteBuffer  bits;
    };

    struct Certificate
    {
        NGIN::UInt32                                                     version {1};
        ByteBuffer                                                       serialNumber;
        ByteBuffer                                                       tbsCertificateDer;
        ByteBuffer                                                       issuerDer;
        DistinguishedName                                                issuer;
        ByteBuffer                                                       subjectDer;
        DistinguishedName                                                subject;
        CertificateValidity                                              validity;
        NGIN::Crypto::Keys::SubjectPublicKeyInfo                         subjectPublicKeyInfo;
        NGIN::Crypto::Keys::KeyAlgorithmIdentifier                       publicKeyAlgorithm;
        NGIN::Crypto::SignatureAlgorithm                                 signatureAlgorithm {NGIN::Crypto::SignatureAlgorithm::Ed25519};
        bool                                                             hasKnownSignatureAlgorithm {false};
        ByteBuffer                                                       signatureAlgorithmDer;
        ByteBuffer                                                       signatureValue;
        SubjectAltNames                                                  subjectAltNames;
        bool                                                             hasSubjectAltNames {false};
        KeyUsage                                                         keyUsage;
        bool                                                             hasKeyUsage {false};
        NGIN::Containers::Vector<NGIN::Containers::Vector<NGIN::UInt32>> extendedKeyUsages;
        ByteBuffer                                                       subjectKeyIdentifier;
        bool                                                             hasSubjectKeyIdentifier {false};
        ByteBuffer                                                       authorityKeyIdentifier;
        bool                                                             hasAuthorityKeyIdentifier {false};
    };

    [[nodiscard]] CryptoExpected<Certificate> ParseX509Certificate(ConstByteSpan der);

    [[nodiscard]] CryptoExpected<void> VerifyCertificateSignature(
            const NGIN::Crypto::Backend::CryptoContext&     context,
            const Certificate&                              certificate,
            const NGIN::Crypto::Keys::SubjectPublicKeyInfo& issuerPublicKey) noexcept;
}// namespace NGIN::Crypto::Certificates
