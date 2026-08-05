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
    /// @brief Supported TLS protocol version.
    enum class TlsProtocolVersion : NGIN::UInt8
    {
        Tls12,
        Tls13,
    };

    /// @brief Client policy for verifying server certificates and identity.
    enum class TlsPeerVerification : NGIN::UInt8
    {
        Required,
        Disabled,
    };

    /// @brief Server policy for requesting and verifying client certificates.
    enum class TlsClientAuthentication : NGIN::UInt8
    {
        None,
        Optional,
        Required,
    };

    /// @brief Lifecycle state of a TLS stream.
    enum class TlsStreamState : NGIN::UInt8
    {
        Created,
        Handshaking,
        Open,
        ShuttingDown,
        Closed,
        Failed,
    };

    /// @brief Protocol-version and cipher-suite policy.
    struct TlsProtocolOptions final
    {
        TlsProtocolVersion       minimum {TlsProtocolVersion::Tls12};
        TlsProtocolVersion       maximum {TlsProtocolVersion::Tls13};
        std::vector<std::string> cipherSuites;
    };

    /// @brief Certificate trust-anchor policy.
    struct TlsTrustOptions final
    {
        bool                                                 useSystemRoots {true};
        std::vector<NGIN::Crypto::Certificates::Certificate> customCertificates;
    };

    /// @brief Immutable client-context configuration.
    struct TlsClientContextOptions final
    {
        TlsProtocolOptions                                               protocols;
        TlsPeerVerification                                              verification {TlsPeerVerification::Required};
        TlsTrustOptions                                                  trust;
        std::optional<NGIN::Crypto::Certificates::TlsCredentialMaterial> credentials;
        std::vector<std::string>                                         applicationProtocols;
        bool                                                             requireApplicationProtocol {false};
    };

    /// @brief Immutable server-context configuration and credentials.
    struct TlsServerContextOptions final
    {
        TlsProtocolOptions                                protocols;
        NGIN::Crypto::Certificates::TlsCredentialMaterial credentials;
        TlsClientAuthentication                           clientAuthentication {TlsClientAuthentication::None};
        TlsTrustOptions                                   clientTrust;
        std::vector<std::string>                          applicationProtocols;
        bool                                              requireApplicationProtocol {false};
    };

    /// @brief Per-connection client identity and truncated-EOF policy.
    struct TlsClientOptions final
    {
        std::string serverName;
        std::string verificationName;
        bool        allowTruncatedEof {false};
    };

    /// @brief Per-connection server behavior.
    struct TlsServerOptions final
    {
        bool allowTruncatedEof {false};
    };

    /// @brief Per-handshake timeout policy; zero disables the timeout.
    struct TlsHandshakeOptions final
    {
        std::chrono::milliseconds timeout {0};
    };
}// namespace NGIN::Net::TLS
