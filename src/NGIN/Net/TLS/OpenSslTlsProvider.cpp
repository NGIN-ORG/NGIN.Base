#include "TlsProvider.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <utility>

#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Net/Types/IpAddress.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

namespace NGIN::Net::TLS::detail
{
    namespace
    {
        using SslContextPtr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
        using SslPtr        = std::unique_ptr<SSL, decltype(&SSL_free)>;
        using X509Ptr       = std::unique_ptr<X509, decltype(&X509_free)>;
        using EvpPkeyPtr    = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

        [[nodiscard]] std::string OpenSslErrorText(unsigned long code)
        {
            if (code == 0)
            {
                return "OpenSSL did not provide an error detail";
            }
            std::array<char, 256> text {};
            ERR_error_string_n(code, text.data(), text.size());
            return text.data();
        }

        [[nodiscard]] TlsError OpenSslError(
                TlsErrorCategory category,
                TlsErrorCode     code,
                std::string      prefix)
        {
            const auto native = ERR_get_error();
            if (!prefix.empty())
            {
                prefix += ": ";
            }
            prefix += OpenSslErrorText(native);
            return MakeTlsError(category, code, std::move(prefix), static_cast<int>(native & INT_MAX));
        }

        [[nodiscard]] int ProtocolVersion(TlsProtocolVersion version) noexcept
        {
            switch (version)
            {
                case TlsProtocolVersion::Tls12:
                    return TLS1_2_VERSION;
                case TlsProtocolVersion::Tls13:
                    return TLS1_3_VERSION;
            }
            return TLS1_2_VERSION;
        }

        [[nodiscard]] bool ValidateProtocolOptions(const TlsProtocolOptions& options) noexcept
        {
            return ProtocolVersion(options.minimum) <= ProtocolVersion(options.maximum);
        }

        [[nodiscard]] TlsExpected<std::vector<unsigned char>> EncodeAlpn(
                const std::vector<std::string>& protocols)
        {
            std::vector<unsigned char> wire;
            for (const auto& protocol: protocols)
            {
                if (protocol.empty() || protocol.size() > 255)
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::State,
                            TlsErrorCode::InvalidConfiguration,
                            "ALPN protocols must contain between 1 and 255 bytes"));
                }
                if (wire.size() > 65535u - protocol.size() - 1u)
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::State,
                            TlsErrorCode::InvalidConfiguration,
                            "encoded ALPN protocol list is too large"));
                }
                wire.push_back(static_cast<unsigned char>(protocol.size()));
                wire.insert(wire.end(), protocol.begin(), protocol.end());
            }
            return wire;
        }

        [[nodiscard]] TlsExpected<X509Ptr> DecodeCertificate(
                const NGIN::Crypto::Certificates::Certificate& certificate)
        {
            if (certificate.certificateDer.Size() == 0)
            {
                return NGIN::Utilities::Unexpected(MakeTlsError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "TLS certificate does not retain its original DER encoding"));
            }
            const auto* cursor = reinterpret_cast<const unsigned char*>(certificate.certificateDer.data());
            X509Ptr     decoded {
                    d2i_X509(nullptr, &cursor, static_cast<long>(certificate.certificateDer.Size())),
                    X509_free,
            };
            if (!decoded)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to import X.509 certificate"));
            }
            return decoded;
        }

        [[nodiscard]] TlsExpected<void> ConfigureTrust(
                SSL_CTX*               context,
                const TlsTrustOptions& trust)
        {
            if (trust.useSystemRoots && SSL_CTX_set_default_verify_paths(context) != 1)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Provider,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to load the OpenSSL system trust paths"));
            }

            auto* store = SSL_CTX_get_cert_store(context);
            for (const auto& certificate: trust.customCertificates)
            {
                auto decoded = DecodeCertificate(certificate);
                if (!decoded.HasValue())
                {
                    return NGIN::Utilities::Unexpected(decoded.Error());
                }
                if (X509_STORE_add_cert(store, decoded.Value().get()) != 1)
                {
                    const auto error = ERR_peek_last_error();
                    if (ERR_GET_REASON(error) == X509_R_CERT_ALREADY_IN_HASH_TABLE)
                    {
                        ERR_clear_error();
                        continue;
                    }
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Certificate,
                            TlsErrorCode::InvalidConfiguration,
                            "failed to add a custom trust certificate"));
                }
            }
            return {};
        }

        [[nodiscard]] TlsExpected<void> ConfigureCredentials(
                SSL_CTX*                                                 context,
                const NGIN::Crypto::Certificates::TlsCredentialMaterial& credentials)
        {
            if (credentials.certificateChain.certificates.Size() == 0)
            {
                return NGIN::Utilities::Unexpected(MakeTlsError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "TLS credentials require a non-empty certificate chain"));
            }

            auto leaf = DecodeCertificate(credentials.certificateChain.certificates[0]);
            if (!leaf.HasValue())
            {
                return NGIN::Utilities::Unexpected(leaf.Error());
            }
            if (SSL_CTX_use_certificate(context, leaf.Value().get()) != 1)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to configure the leaf certificate"));
            }

            for (NGIN::UIntSize index = 1; index < credentials.certificateChain.certificates.Size(); ++index)
            {
                auto intermediate = DecodeCertificate(credentials.certificateChain.certificates[index]);
                if (!intermediate.HasValue())
                {
                    return NGIN::Utilities::Unexpected(intermediate.Error());
                }
                auto* transferred = intermediate.Value().release();
                if (SSL_CTX_add_extra_chain_cert(context, transferred) != 1)
                {
                    X509_free(transferred);
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Certificate,
                            TlsErrorCode::InvalidConfiguration,
                            "failed to configure an intermediate certificate"));
                }
            }

            auto privateKeyDer = NGIN::Crypto::Keys::WritePrivateKeyInfo(
                    credentials.privateKey.algorithm.algorithm,
                    NGIN::Crypto::ConstByteSpan {
                            credentials.privateKey.privateKey.data(),
                            credentials.privateKey.privateKey.Size(),
                    });
            if (!privateKeyDer.HasValue())
            {
                return NGIN::Utilities::Unexpected(MakeTlsError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to serialize TLS private key material"));
            }
            const auto* keyCursor = reinterpret_cast<const unsigned char*>(privateKeyDer.Value().data());
            EvpPkeyPtr  privateKey {
                    d2i_AutoPrivateKey(nullptr, &keyCursor, static_cast<long>(privateKeyDer.Value().Size())),
                    EVP_PKEY_free,
            };
            if (!privateKey || SSL_CTX_use_PrivateKey(context, privateKey.get()) != 1)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to import TLS private key material"));
            }
            if (SSL_CTX_check_private_key(context) != 1)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Certificate,
                        TlsErrorCode::InvalidConfiguration,
                        "TLS private key does not match the leaf certificate"));
            }
            return {};
        }

        [[nodiscard]] TlsExpected<void> ConfigureProtocols(
                SSL_CTX*                  context,
                const TlsProtocolOptions& protocols)
        {
            if (!ValidateProtocolOptions(protocols))
            {
                return NGIN::Utilities::Unexpected(MakeTlsError(
                        TlsErrorCategory::State,
                        TlsErrorCode::InvalidConfiguration,
                        "TLS minimum protocol version exceeds the maximum"));
            }
            if (SSL_CTX_set_min_proto_version(context, ProtocolVersion(protocols.minimum)) != 1 ||
                SSL_CTX_set_max_proto_version(context, ProtocolVersion(protocols.maximum)) != 1)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Provider,
                        TlsErrorCode::InvalidConfiguration,
                        "failed to configure the TLS protocol range"));
            }
            if (!protocols.cipherSuites.empty())
            {
                std::string joined;
                for (const auto& cipher: protocols.cipherSuites)
                {
                    if (cipher.empty())
                    {
                        return NGIN::Utilities::Unexpected(MakeTlsError(
                                TlsErrorCategory::State,
                                TlsErrorCode::InvalidConfiguration,
                                "TLS cipher names cannot be empty"));
                    }
                    if (!joined.empty())
                    {
                        joined.push_back(':');
                    }
                    joined += cipher;
                }
                const auto legacyConfigured = SSL_CTX_set_cipher_list(context, joined.c_str()) == 1;
                const auto tls13Configured  = SSL_CTX_set_ciphersuites(context, joined.c_str()) == 1;
                if (!legacyConfigured && !tls13Configured)
                {
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Provider,
                            TlsErrorCode::InvalidConfiguration,
                            "none of the requested TLS cipher suites are supported"));
                }
                ERR_clear_error();
            }
            return {};
        }

        class OpenSslSession final : public TlsSession
        {
        public:
            OpenSslSession(SslPtr ssl, BIO* readBio, BIO* writeBio, bool requireAlpn) noexcept
                : m_ssl(std::move(ssl)), m_readBio(readBio), m_writeBio(writeBio), m_requireAlpn(requireAlpn)
            {
            }

            TlsExpected<TlsProviderResult> Handshake() override
            {
                const auto result = SSL_do_handshake(m_ssl.get());
                if (result == 1)
                {
                    const unsigned char* selected       = nullptr;
                    unsigned int         selectedLength = 0;
                    SSL_get0_alpn_selected(m_ssl.get(), &selected, &selectedLength);
                    if (m_requireAlpn && selectedLength == 0)
                    {
                        return NGIN::Utilities::Unexpected(MakeTlsError(
                                TlsErrorCategory::Protocol,
                                TlsErrorCode::AlpnMismatch,
                                "TLS handshake did not negotiate a required ALPN protocol"));
                    }
                    return TlsProviderResult {TlsProviderStatus::Complete, 0};
                }
                return Translate(result, "TLS handshake");
            }

            TlsExpected<TlsProviderResult> Read(NGIN::Net::ByteSpan destination) override
            {
                std::size_t bytes  = 0;
                const auto  result = SSL_read_ex(m_ssl.get(), destination.data(), destination.size(), &bytes);
                if (result == 1)
                {
                    return TlsProviderResult {
                            TlsProviderStatus::Complete,
                            static_cast<NGIN::UInt32>(bytes),
                    };
                }
                return Translate(result, "TLS read");
            }

            TlsExpected<TlsProviderResult> Write(NGIN::Net::ConstByteSpan source) override
            {
                std::size_t bytes  = 0;
                const auto  result = SSL_write_ex(m_ssl.get(), source.data(), source.size(), &bytes);
                if (result == 1)
                {
                    return TlsProviderResult {
                            TlsProviderStatus::Complete,
                            static_cast<NGIN::UInt32>(bytes),
                    };
                }
                return Translate(result, "TLS write");
            }

            TlsExpected<TlsProviderResult> Shutdown() override
            {
                const auto result = SSL_shutdown(m_ssl.get());
                if (result == 1)
                {
                    return TlsProviderResult {TlsProviderStatus::Complete, 0};
                }
                if (result == 0)
                {
                    return TlsProviderResult {TlsProviderStatus::WantRead, 0};
                }
                return Translate(result, "TLS shutdown");
            }

            TlsExpected<std::vector<NGIN::Byte>> DrainEncrypted() override
            {
                std::vector<NGIN::Byte> encrypted;
                while (BIO_ctrl_pending(m_writeBio) > 0)
                {
                    const auto pending = BIO_ctrl_pending(m_writeBio);
                    const auto chunk   = static_cast<int>(std::min<std::size_t>(pending, static_cast<std::size_t>(INT_MAX)));
                    const auto offset  = encrypted.size();
                    encrypted.resize(offset + static_cast<std::size_t>(chunk));
                    const auto read = BIO_read(m_writeBio, encrypted.data() + offset, chunk);
                    if (read <= 0)
                    {
                        return NGIN::Utilities::Unexpected(OpenSslError(
                                TlsErrorCategory::Provider,
                                TlsErrorCode::ProtocolViolation,
                                "failed to drain encrypted TLS records"));
                    }
                    encrypted.resize(offset + static_cast<std::size_t>(read));
                }
                return encrypted;
            }

            TlsExpected<void> FeedEncrypted(NGIN::Net::ConstByteSpan encrypted) override
            {
                std::size_t offset = 0;
                while (offset < encrypted.size())
                {
                    std::size_t written = 0;
                    if (BIO_write_ex(
                                m_readBio,
                                encrypted.data() + offset,
                                encrypted.size() - offset,
                                &written) != 1 ||
                        written == 0)
                    {
                        return NGIN::Utilities::Unexpected(OpenSslError(
                                TlsErrorCategory::Provider,
                                TlsErrorCode::ProtocolViolation,
                                "failed to feed encrypted TLS records"));
                    }
                    offset += written;
                }
                return {};
            }

            void NotifyTransportEof() noexcept override
            {
                m_transportEof = true;
                BIO_set_mem_eof_return(m_readBio, 0);
            }

            std::string NegotiatedProtocol() const override
            {
                const unsigned char* selected       = nullptr;
                unsigned int         selectedLength = 0;
                SSL_get0_alpn_selected(m_ssl.get(), &selected, &selectedLength);
                return selectedLength == 0
                               ? std::string {}
                               : std::string(reinterpret_cast<const char*>(selected), selectedLength);
            }

            std::string ServerName() const override
            {
                const auto* value = SSL_get_servername(m_ssl.get(), TLSEXT_NAMETYPE_host_name);
                return value == nullptr ? std::string {} : std::string {value};
            }

            std::vector<NGIN::Byte> PeerCertificateDer() const override
            {
                X509Ptr peer {SSL_get1_peer_certificate(m_ssl.get()), X509_free};
                if (!peer)
                {
                    return {};
                }
                const auto size = i2d_X509(peer.get(), nullptr);
                if (size <= 0)
                {
                    ERR_clear_error();
                    return {};
                }
                std::vector<NGIN::Byte> der(static_cast<std::size_t>(size));
                auto*                   cursor = reinterpret_cast<unsigned char*>(der.data());
                if (i2d_X509(peer.get(), &cursor) != size)
                {
                    ERR_clear_error();
                    return {};
                }
                return der;
            }

        private:
            [[nodiscard]] TlsExpected<TlsProviderResult> Translate(int result, std::string_view operation)
            {
                const auto sslError = SSL_get_error(m_ssl.get(), result);
                switch (sslError)
                {
                    case SSL_ERROR_WANT_READ:
                        return TlsProviderResult {TlsProviderStatus::WantRead, 0};
                    case SSL_ERROR_WANT_WRITE:
                        return TlsProviderResult {TlsProviderStatus::WantWrite, 0};
                    case SSL_ERROR_ZERO_RETURN:
                        return TlsProviderResult {TlsProviderStatus::Closed, 0};
                    default:
                        break;
                }

                const auto verify = SSL_get_verify_result(m_ssl.get());
                if (verify != X509_V_OK)
                {
                    const auto hostname = verify == X509_V_ERR_HOSTNAME_MISMATCH;
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            hostname ? TlsErrorCategory::Hostname : TlsErrorCategory::Certificate,
                            hostname ? TlsErrorCode::HostnameMismatch : TlsErrorCode::CertificateRejected,
                            std::string(operation) + ": " + X509_verify_cert_error_string(verify),
                            static_cast<int>(verify)));
                }

                if (m_transportEof)
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::Protocol,
                            TlsErrorCode::TruncatedStream,
                            std::string(operation) + ": transport EOF arrived without close_notify"));
                }

                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Protocol,
                        operation == "TLS handshake" ? TlsErrorCode::HandshakeFailed : TlsErrorCode::ProtocolViolation,
                        std::string(operation) + " failed"));
            }

            SslPtr m_ssl;
            BIO*   m_readBio {nullptr};
            BIO*   m_writeBio {nullptr};
            bool   m_requireAlpn {false};
            bool   m_transportEof {false};
        };

        class OpenSslContextState final : public TlsContextState
        {
        public:
            OpenSslContextState(
                    SslContextPtr              context,
                    bool                       client,
                    std::vector<unsigned char> alpn,
                    bool                       requireAlpn) noexcept
                : m_context(std::move(context)), m_client(client), m_alpn(std::move(alpn)), m_requireAlpn(requireAlpn)
            {
            }

            bool             IsClient() const noexcept override { return m_client; }
            std::string_view ProviderName() const noexcept override { return "openssl"; }

            TlsExpected<std::unique_ptr<TlsSession>> CreateClientSession(
                    const TlsClientOptions& options) const override
            {
                if (!m_client)
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::State,
                            TlsErrorCode::InvalidState,
                            "cannot create a TLS client session from a server context"));
                }
                auto created = CreateSession();
                if (!created.HasValue())
                {
                    return NGIN::Utilities::Unexpected(created.Error());
                }
                auto& session = created.Value();
                SSL_set_connect_state(session.ssl.get());

                const auto verificationName = options.verificationName.empty()
                                                      ? options.serverName
                                                      : options.verificationName;
                if (SSL_get_verify_mode(session.ssl.get()) != SSL_VERIFY_NONE && verificationName.empty())
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::Hostname,
                            TlsErrorCode::InvalidConfiguration,
                            "verified TLS clients require a server or verification name"));
                }

                if (!options.serverName.empty())
                {
                    const auto parsedAddress = NGIN::Net::IpAddress::Parse(options.serverName);
                    if (!parsedAddress.HasValue() &&
                        SSL_set_tlsext_host_name(session.ssl.get(), options.serverName.c_str()) != 1)
                    {
                        return NGIN::Utilities::Unexpected(OpenSslError(
                                TlsErrorCategory::Provider,
                                TlsErrorCode::InvalidConfiguration,
                                "failed to configure TLS SNI"));
                    }
                }

                if (SSL_get_verify_mode(session.ssl.get()) != SSL_VERIFY_NONE)
                {
                    const auto parsedAddress = NGIN::Net::IpAddress::Parse(verificationName);
                    if (parsedAddress.HasValue())
                    {
                        if (X509_VERIFY_PARAM_set1_ip_asc(
                                    SSL_get0_param(session.ssl.get()), verificationName.c_str()) != 1)
                        {
                            return NGIN::Utilities::Unexpected(OpenSslError(
                                    TlsErrorCategory::Hostname,
                                    TlsErrorCode::InvalidConfiguration,
                                    "failed to configure TLS IP verification"));
                        }
                    }
                    else if (SSL_set1_host(session.ssl.get(), verificationName.c_str()) != 1)
                    {
                        return NGIN::Utilities::Unexpected(OpenSslError(
                                TlsErrorCategory::Hostname,
                                TlsErrorCode::InvalidConfiguration,
                                "failed to configure TLS hostname verification"));
                    }
                    X509_VERIFY_PARAM_set_hostflags(
                            SSL_get0_param(session.ssl.get()), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
                }

                if (!m_alpn.empty() &&
                    SSL_set_alpn_protos(session.ssl.get(), m_alpn.data(), static_cast<unsigned int>(m_alpn.size())) != 0)
                {
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Provider,
                            TlsErrorCode::InvalidConfiguration,
                            "failed to configure client ALPN protocols"));
                }
                std::unique_ptr<TlsSession> out = std::make_unique<OpenSslSession>(
                        std::move(session.ssl), session.readBio, session.writeBio, m_requireAlpn);
                return out;
            }

            TlsExpected<std::unique_ptr<TlsSession>> CreateServerSession(
                    const TlsServerOptions&) const override
            {
                if (m_client)
                {
                    return NGIN::Utilities::Unexpected(MakeTlsError(
                            TlsErrorCategory::State,
                            TlsErrorCode::InvalidState,
                            "cannot create a TLS server session from a client context"));
                }
                auto created = CreateSession();
                if (!created.HasValue())
                {
                    return NGIN::Utilities::Unexpected(created.Error());
                }
                auto& session = created.Value();
                SSL_set_accept_state(session.ssl.get());
                std::unique_ptr<TlsSession> out = std::make_unique<OpenSslSession>(
                        std::move(session.ssl), session.readBio, session.writeBio, m_requireAlpn);
                return out;
            }

            void ConfigureServerAlpnCallback()
            {
                if (!m_client && (!m_alpn.empty() || m_requireAlpn))
                {
                    SSL_CTX_set_alpn_select_cb(m_context.get(), &SelectAlpn, this);
                }
            }

            [[nodiscard]] SSL_CTX* RawContext() noexcept { return m_context.get(); }

        private:
            struct SessionParts final
            {
                SslPtr ssl {nullptr, SSL_free};
                BIO*   readBio {nullptr};
                BIO*   writeBio {nullptr};
            };

            [[nodiscard]] TlsExpected<SessionParts> CreateSession() const
            {
                SslPtr ssl {SSL_new(m_context.get()), SSL_free};
                if (!ssl)
                {
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Provider,
                            TlsErrorCode::ProviderUnavailable,
                            "failed to create an OpenSSL TLS session"));
                }
                auto* readBio  = BIO_new(BIO_s_mem());
                auto* writeBio = BIO_new(BIO_s_mem());
                if (readBio == nullptr || writeBio == nullptr)
                {
                    BIO_free(readBio);
                    BIO_free(writeBio);
                    return NGIN::Utilities::Unexpected(OpenSslError(
                            TlsErrorCategory::Provider,
                            TlsErrorCode::ProviderUnavailable,
                            "failed to create TLS memory BIOs"));
                }
                BIO_set_mem_eof_return(readBio, -1);
                BIO_set_mem_eof_return(writeBio, -1);
                SSL_set_bio(ssl.get(), readBio, writeBio);
                return SessionParts {std::move(ssl), readBio, writeBio};
            }

            static int SelectAlpn(
                    SSL*,
                    const unsigned char** selected,
                    unsigned char*        selectedLength,
                    const unsigned char*  offered,
                    unsigned int          offeredLength,
                    void*                 rawState)
            {
                const auto* state = static_cast<const OpenSslContextState*>(rawState);
                if (state == nullptr || state->m_alpn.empty())
                {
                    return state != nullptr && state->m_requireAlpn
                                   ? SSL_TLSEXT_ERR_ALERT_FATAL
                                   : SSL_TLSEXT_ERR_NOACK;
                }
                unsigned char* mutableSelected = nullptr;
                const auto     result          = SSL_select_next_proto(
                        &mutableSelected,
                        selectedLength,
                        state->m_alpn.data(),
                        static_cast<unsigned int>(state->m_alpn.size()),
                        offered,
                        offeredLength);
                if (result == OPENSSL_NPN_NEGOTIATED)
                {
                    *selected = mutableSelected;
                    return SSL_TLSEXT_ERR_OK;
                }
                return state->m_requireAlpn ? SSL_TLSEXT_ERR_ALERT_FATAL : SSL_TLSEXT_ERR_NOACK;
            }

            SslContextPtr              m_context;
            bool                       m_client {false};
            std::vector<unsigned char> m_alpn;
            bool                       m_requireAlpn {false};
        };

        template<typename Options>
        [[nodiscard]] TlsExpected<std::shared_ptr<OpenSslContextState>> CreateContextBase(
                Options& options,
                bool     client)
        {
            ERR_clear_error();
            SslContextPtr context {
                    SSL_CTX_new(client ? TLS_client_method() : TLS_server_method()),
                    SSL_CTX_free,
            };
            if (!context)
            {
                return NGIN::Utilities::Unexpected(OpenSslError(
                        TlsErrorCategory::Provider,
                        TlsErrorCode::ProviderUnavailable,
                        "failed to create OpenSSL TLS context"));
            }
            SSL_CTX_set_options(context.get(), SSL_OP_NO_COMPRESSION);
            SSL_CTX_set_mode(context.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

            auto protocols = ConfigureProtocols(context.get(), options.protocols);
            if (!protocols.HasValue())
            {
                return NGIN::Utilities::Unexpected(protocols.Error());
            }
            auto alpn = EncodeAlpn(options.applicationProtocols);
            if (!alpn.HasValue())
            {
                return NGIN::Utilities::Unexpected(alpn.Error());
            }
            return std::make_shared<OpenSslContextState>(
                    std::move(context), client, std::move(alpn.Value()), options.requireApplicationProtocol);
        }
    }// namespace

    TlsExpected<std::shared_ptr<const TlsContextState>> CreateOpenSslClientContext(
            TlsClientContextOptions options)
    {
        auto created = CreateContextBase(options, true);
        if (!created.HasValue())
        {
            return NGIN::Utilities::Unexpected(created.Error());
        }
        auto& state   = created.Value();
        auto* context = state->RawContext();
        if (options.verification == TlsPeerVerification::Required)
        {
            SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
            auto trust = ConfigureTrust(context, options.trust);
            if (!trust.HasValue())
            {
                return NGIN::Utilities::Unexpected(trust.Error());
            }
        }
        else
        {
            SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
        }
        if (options.credentials.has_value())
        {
            auto credentials = ConfigureCredentials(context, *options.credentials);
            if (!credentials.HasValue())
            {
                return NGIN::Utilities::Unexpected(credentials.Error());
            }
        }
        std::shared_ptr<const TlsContextState> out = std::move(state);
        return out;
    }

    TlsExpected<std::shared_ptr<const TlsContextState>> CreateOpenSslServerContext(
            TlsServerContextOptions options)
    {
        auto created = CreateContextBase(options, false);
        if (!created.HasValue())
        {
            return NGIN::Utilities::Unexpected(created.Error());
        }
        auto& state       = created.Value();
        auto* context     = state->RawContext();
        auto  credentials = ConfigureCredentials(context, options.credentials);
        if (!credentials.HasValue())
        {
            return NGIN::Utilities::Unexpected(credentials.Error());
        }

        if (options.clientAuthentication == TlsClientAuthentication::None)
        {
            SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
        }
        else
        {
            auto mode = SSL_VERIFY_PEER;
            if (options.clientAuthentication == TlsClientAuthentication::Required)
            {
                mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            }
            SSL_CTX_set_verify(context, mode, nullptr);
            auto trust = ConfigureTrust(context, options.clientTrust);
            if (!trust.HasValue())
            {
                return NGIN::Utilities::Unexpected(trust.Error());
            }
        }
        state->ConfigureServerAlpnCallback();
        std::shared_ptr<const TlsContextState> out = std::move(state);
        return out;
    }
}// namespace NGIN::Net::TLS::detail
