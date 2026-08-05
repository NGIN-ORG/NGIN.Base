/// @file TlsTypes.hpp
/// @brief Provider-neutral TLS configuration and state types.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <NGIN/Crypto/Certificates/Certificate.hpp>
#include <NGIN/Crypto/Certificates/TlsCredentialMaterial.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Net::TLS
{
    enum class TlsProtocolVersion : NGIN::UInt8
    {
        Tls12,
        Tls13,
    };

    enum class TlsPeerVerification : NGIN::UInt8
    {
        Required,
        Disabled,
    };

    enum class TlsClientAuthentication : NGIN::UInt8
    {
        None,
        Optional,
        Required,
    };

    enum class TlsStreamState : NGIN::UInt8
    {
        Created,
        Handshaking,
        Open,
        ShuttingDown,
        Closed,
        Failed,
    };

    struct TlsProtocolOptions final
    {
        TlsProtocolVersion       minimum {TlsProtocolVersion::Tls12};
        TlsProtocolVersion       maximum {TlsProtocolVersion::Tls13};
        std::vector<std::string> cipherSuites;
    };

    struct TlsTrustOptions final
    {
        bool                                                 useSystemRoots {true};
        std::vector<NGIN::Crypto::Certificates::Certificate> customCertificates;
    };

    struct TlsClientContextOptions final
    {
        TlsProtocolOptions                                               protocols;
        TlsPeerVerification                                              verification {TlsPeerVerification::Required};
        TlsTrustOptions                                                  trust;
        std::optional<NGIN::Crypto::Certificates::TlsCredentialMaterial> credentials;
        std::vector<std::string>                                         applicationProtocols;
        bool                                                             requireApplicationProtocol {false};
    };

    struct TlsServerContextOptions final
    {
        TlsProtocolOptions                                protocols;
        NGIN::Crypto::Certificates::TlsCredentialMaterial credentials;
        TlsClientAuthentication                           clientAuthentication {TlsClientAuthentication::None};
        TlsTrustOptions                                   clientTrust;
        std::vector<std::string>                          applicationProtocols;
        bool                                              requireApplicationProtocol {false};
    };

    struct TlsClientOptions final
    {
        std::string serverName;
        std::string verificationName;
        bool        allowTruncatedEof {false};
    };

    struct TlsServerOptions final
    {
        bool allowTruncatedEof {false};
    };

    struct TlsHandshakeOptions final
    {
        std::chrono::milliseconds timeout {0};
    };
}// namespace NGIN::Net::TLS
