#pragma once

#include <NGIN/Crypto/Certificates/CertificateChain.hpp>
#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>

namespace NGIN::Crypto::Certificates
{
    /// @brief Parsed certificate chain plus private key material for Net-owned TLS integration.
    struct TlsCredentialMaterial
    {
        CertificateChain                   certificateChain;
        NGIN::Crypto::Keys::PrivateKeyInfo privateKey;
    };
}// namespace NGIN::Crypto::Certificates
