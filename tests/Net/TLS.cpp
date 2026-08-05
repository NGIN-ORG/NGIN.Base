#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Crypto/Encoding/Pem.hpp>
#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Net/TLS/TlsStream.hpp>

namespace
{
    using NGIN::Net::TLS::TlsError;

    struct PipeState final
    {
        std::mutex                            mutex;
        std::condition_variable               changed;
        std::array<std::deque<NGIN::Byte>, 2> incoming;
        std::array<bool, 2>                   closed {false, false};
        std::size_t                           maxRead {13};
        std::size_t                           maxWrite {17};
    };

    class MemoryByteStream final : public NGIN::Net::Transport::IByteStream
    {
    public:
        MemoryByteStream(std::shared_ptr<PipeState> state, std::size_t side) noexcept
            : m_state(std::move(state)), m_side(side)
        {
        }

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(
                NGIN::Async::TaskContext&,
                NGIN::Net::ByteSpan            destination,
                NGIN::Async::CancellationToken token) override
        {
            if (destination.empty())
            {
                co_return NGIN::UInt32 {0};
            }

            NGIN::Async::CancellationRegistration registration;
            token.Register(
                    registration,
                    {},
                    {},
                    +[](void* rawState) noexcept -> bool {
                        static_cast<PipeState*>(rawState)->changed.notify_all();
                        return false;
                    },
                    m_state.get());

            std::unique_lock lock(m_state->mutex);
            m_state->changed.wait(lock, [&] {
                return token.IsCancellationRequested() || !m_state->incoming[m_side].empty() ||
                       m_state->closed[1 - m_side];
            });
            if (token.IsCancellationRequested())
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
            }
            if (m_state->incoming[m_side].empty())
            {
                co_return NGIN::UInt32 {0};
            }

            const auto count = std::min({destination.size(), m_state->incoming[m_side].size(), m_state->maxRead});
            for (std::size_t index = 0; index < count; ++index)
            {
                destination[index] = m_state->incoming[m_side].front();
                m_state->incoming[m_side].pop_front();
            }
            co_return static_cast<NGIN::UInt32>(count);
        }

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(
                NGIN::Async::TaskContext&,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token) override
        {
            if (token.IsCancellationRequested())
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
            }
            std::lock_guard lock(m_state->mutex);
            if (m_state->closed[m_side] || m_state->closed[1 - m_side])
            {
                co_return NGIN::Net::NetError {NGIN::Net::NetErrorCode::Disconnected};
            }
            const auto count = std::min(source.size(), m_state->maxWrite);
            for (std::size_t index = 0; index < count; ++index)
            {
                m_state->incoming[1 - m_side].push_back(source[index]);
            }
            m_state->changed.notify_all();
            co_return static_cast<NGIN::UInt32>(count);
        }

        NGIN::Net::NetExpected<void> Close() override
        {
            std::lock_guard lock(m_state->mutex);
            m_state->closed[m_side] = true;
            m_state->changed.notify_all();
            return {};
        }

    private:
        std::shared_ptr<PipeState> m_state;
        std::size_t                m_side;
    };

    class BlockingByteStream final : public NGIN::Net::Transport::IByteStream
    {
    public:
        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(
                NGIN::Async::TaskContext&,
                NGIN::Net::ByteSpan,
                NGIN::Async::CancellationToken token) override
        {
            NGIN::Async::CancellationRegistration registration;
            token.Register(
                    registration,
                    {},
                    {},
                    +[](void* raw) noexcept -> bool {
                        static_cast<BlockingByteStream*>(raw)->m_changed.notify_all();
                        return false;
                    },
                    this);
            std::unique_lock lock(m_mutex);
            m_changed.wait(lock, [&] { return token.IsCancellationRequested() || m_closed; });
            if (token.IsCancellationRequested())
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
            }
            co_return NGIN::UInt32 {0};
        }

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(
                NGIN::Async::TaskContext&,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token) override
        {
            if (token.IsCancellationRequested())
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
            }
            co_return static_cast<NGIN::UInt32>(source.size());
        }

        NGIN::Net::NetExpected<void> Close() override
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
            m_changed.notify_all();
            return {};
        }

    private:
        std::mutex              m_mutex;
        std::condition_variable m_changed;
        bool                    m_closed {false};
    };

    struct StreamPair final
    {
        std::unique_ptr<NGIN::Net::Transport::IByteStream> first;
        std::unique_ptr<NGIN::Net::Transport::IByteStream> second;
    };

    [[nodiscard]] StreamPair MakeMemoryPair()
    {
        auto state = std::make_shared<PipeState>();
        return {
                std::make_unique<MemoryByteStream>(state, 0),
                std::make_unique<MemoryByteStream>(std::move(state), 1),
        };
    }

    [[nodiscard]] std::string ReadText(const std::string& name)
    {
        const auto    path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "tls" / name;
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    [[nodiscard]] NGIN::Crypto::Certificates::Certificate LoadCertificate(const std::string& name)
    {
        const auto pem = NGIN::Crypto::Encoding::ParsePem(
                ReadText(name), {.allowedLabels = {"CERTIFICATE"}, .allowMultipleBlocks = false});
        REQUIRE(pem.HasValue());
        REQUIRE(pem.Value().Size() == 1);
        const auto parsed = NGIN::Crypto::Certificates::ParseX509Certificate(
                NGIN::Crypto::ConstByteSpan {pem.Value()[0].decoded.data(), pem.Value()[0].decoded.Size()});
        REQUIRE(parsed.HasValue());
        return parsed.Value();
    }

    [[nodiscard]] NGIN::Crypto::Keys::PrivateKeyInfo LoadPrivateKey(const std::string& name)
    {
        const auto pem = NGIN::Crypto::Encoding::ParsePem(
                ReadText(name), {.allowedLabels = {"PRIVATE KEY"}, .allowMultipleBlocks = false});
        REQUIRE(pem.HasValue());
        REQUIRE(pem.Value().Size() == 1);
        const auto parsed = NGIN::Crypto::Keys::ParsePrivateKeyInfo(
                NGIN::Crypto::ConstByteSpan {pem.Value()[0].decoded.data(), pem.Value()[0].decoded.Size()});
        REQUIRE(parsed.HasValue());
        return parsed.Value();
    }

    [[nodiscard]] NGIN::Crypto::Certificates::TlsCredentialMaterial Credentials(
            const std::string& certificate,
            const std::string& key)
    {
        NGIN::Crypto::Certificates::TlsCredentialMaterial material;
        material.certificateChain.certificates.PushBack(LoadCertificate(certificate));
        material.privateKey = LoadPrivateKey(key);
        return material;
    }

    struct HandshakeResults final
    {
        NGIN::Async::Completion<void, TlsError> client;
        NGIN::Async::Completion<void, TlsError> server;
    };

    NGIN::Async::Task<void> HandshakeBoth(
            NGIN::Async::TaskContext&  ctx,
            NGIN::Net::TLS::TlsStream& client,
            NGIN::Net::TLS::TlsStream& server,
            HandshakeResults&          results)
    {
        constexpr auto timeout         = std::chrono::seconds(2);
        auto           clientOperation = NGIN::Async::Spawn(ctx, client.HandshakeAsync(ctx, {}, {.timeout = timeout}));
        auto           serverOperation = NGIN::Async::Spawn(ctx, server.HandshakeAsync(ctx, {}, {.timeout = timeout}));
        results.client                 = co_await clientOperation;
        results.server                 = co_await serverOperation;
        co_return;
    }

    [[nodiscard]] HandshakeResults RunHandshake(
            NGIN::Execution::ThreadPoolScheduler& scheduler,
            NGIN::Net::TLS::TlsStream&            client,
            NGIN::Net::TLS::TlsStream&            server)
    {
        NGIN::Async::TaskContext ctx(scheduler);
        HandshakeResults         results;
        const auto               completion = NGIN::Async::SyncWait(ctx, HandshakeBoth(ctx, client, server, results));
        REQUIRE(completion.Succeeded());
        return results;
    }

    struct OpenStreams final
    {
        std::unique_ptr<NGIN::Net::TLS::TlsStream> client;
        std::unique_ptr<NGIN::Net::TLS::TlsStream> server;
    };

    [[nodiscard]] OpenStreams MakeStreams(
            const NGIN::Net::TLS::TlsContext& clientContext,
            const NGIN::Net::TLS::TlsContext& serverContext,
            std::string                       verificationName = "localhost",
            bool                              allowTruncated   = false)
    {
        auto pair   = MakeMemoryPair();
        auto client = NGIN::Net::TLS::TlsStream::CreateClient(
                std::move(pair.first),
                clientContext,
                {
                        .serverName        = "localhost",
                        .verificationName  = std::move(verificationName),
                        .allowTruncatedEof = allowTruncated,
                });
        auto server = NGIN::Net::TLS::TlsStream::CreateServer(std::move(pair.second), serverContext);
        REQUIRE(client.HasValue());
        REQUIRE(server.HasValue());
        return {std::move(client.Value()), std::move(server.Value())};
    }

    [[nodiscard]] NGIN::Net::TLS::TlsContext MakeClientContext(
            bool                                                             trustRoot   = true,
            std::optional<NGIN::Crypto::Certificates::TlsCredentialMaterial> credentials = std::nullopt,
            std::vector<std::string>                                         alpn        = {"http/1.1", "h2"},
            bool                                                             requireAlpn = true)
    {
        NGIN::Net::TLS::TlsClientContextOptions options;
        options.trust.useSystemRoots = false;
        if (trustRoot)
        {
            options.trust.customCertificates.push_back(LoadCertificate("root.pem"));
        }
        options.credentials                = std::move(credentials);
        options.applicationProtocols       = std::move(alpn);
        options.requireApplicationProtocol = requireAlpn;
        auto context                       = NGIN::Net::TLS::TlsContext::CreateClient(std::move(options));
        REQUIRE(context.HasValue());
        return context.Value();
    }

    [[nodiscard]] NGIN::Net::TLS::TlsContext MakeServerContext(
            NGIN::Crypto::Certificates::TlsCredentialMaterial credentials,
            NGIN::Net::TLS::TlsClientAuthentication           authentication = NGIN::Net::TLS::TlsClientAuthentication::None,
            std::vector<std::string>                          alpn           = {"h2", "http/1.1"},
            bool                                              requireAlpn    = true)
    {
        NGIN::Net::TLS::TlsServerContextOptions options;
        options.credentials                = std::move(credentials);
        options.clientAuthentication       = authentication;
        options.clientTrust.useSystemRoots = false;
        if (authentication != NGIN::Net::TLS::TlsClientAuthentication::None)
        {
            options.clientTrust.customCertificates.push_back(LoadCertificate("root.pem"));
        }
        options.applicationProtocols       = std::move(alpn);
        options.requireApplicationProtocol = requireAlpn;
        auto context                       = NGIN::Net::TLS::TlsContext::CreateServer(std::move(options));
        REQUIRE(context.HasValue());
        return context.Value();
    }
}// namespace

TEST_CASE("TLS provider availability is explicit", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        auto context = NGIN::Net::TLS::TlsContext::CreateClient();
        REQUIRE_FALSE(context.HasValue());
        CHECK(context.Error().category == NGIN::Net::TLS::TlsErrorCategory::Provider);
        CHECK(context.Error().code == NGIN::Net::TLS::TlsErrorCode::ProviderUnavailable);
        return;
    }

    NGIN::Net::TLS::TlsClientContextOptions options;
    options.verification = NGIN::Net::TLS::TlsPeerVerification::Disabled;
    auto context         = NGIN::Net::TLS::TlsContext::CreateClient(std::move(options));
    REQUIRE(context.HasValue());
    CHECK(context.Value().ProviderName() == "openssl");
}

TEST_CASE("TLS trusted handshake negotiates SNI ALPN and fragmented application data", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        SKIP("OpenSSL TLS provider is disabled");
    }
    auto                                 clientContext = MakeClientContext();
    auto                                 serverContext = MakeServerContext(Credentials("server.pem", "server-key-pkcs8.pem"));
    auto                                 streams       = MakeStreams(clientContext, serverContext);
    NGIN::Execution::ThreadPoolScheduler scheduler(4);

    const auto handshake = RunHandshake(scheduler, *streams.client, *streams.server);
    REQUIRE(handshake.client.Succeeded());
    REQUIRE(handshake.server.Succeeded());
    CHECK(streams.client->NegotiatedProtocol() == "h2");
    CHECK(streams.server->NegotiatedProtocol() == "h2");
    CHECK(streams.server->ServerName() == "localhost");
    REQUIRE(streams.client->PeerCertificate().has_value());

    NGIN::Async::TaskContext   ctx(scheduler);
    const std::string          payload = "fragmented TLS payload";
    std::array<NGIN::Byte, 64> received {};
    auto                       read  = NGIN::Async::Spawn(ctx, streams.server->ReadTlsAsync(ctx, received));
    auto                       write = NGIN::Async::SyncWait(
            ctx,
            streams.client->WriteTlsAsync(
                    ctx,
                    NGIN::Net::ConstByteSpan {
                            reinterpret_cast<const NGIN::Byte*>(payload.data()),
                            payload.size(),
                    }));
    REQUIRE(write.Succeeded());
    NGIN::Async::Completion<NGIN::UInt32, TlsError> readCompletion;
    auto                                            waitForRead = [&]() -> NGIN::Async::Task<void> {
        readCompletion = co_await read;
        co_return;
    };
    REQUIRE(NGIN::Async::SyncWait(ctx, waitForRead()).Succeeded());
    REQUIRE(readCompletion.Succeeded());
    CHECK(readCompletion.Value() == payload.size());
    CHECK(std::string(reinterpret_cast<const char*>(received.data()), readCompletion.Value()) == payload);

    HandshakeResults shutdown;
    auto             shutdownBoth = [&]() -> NGIN::Async::Task<void> {
        auto client     = NGIN::Async::Spawn(ctx, streams.client->ShutdownAsync(ctx));
        auto server     = NGIN::Async::Spawn(ctx, streams.server->ShutdownAsync(ctx));
        shutdown.client = co_await client;
        shutdown.server = co_await server;
        co_return;
    };
    REQUIRE(NGIN::Async::SyncWait(ctx, shutdownBoth()).Succeeded());
    CHECK(shutdown.client.Succeeded());
    CHECK(shutdown.server.Succeeded());
}

TEST_CASE("TLS verification rejects untrusted expired and mismatched certificates", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        SKIP("OpenSSL TLS provider is disabled");
    }
    NGIN::Execution::ThreadPoolScheduler scheduler(4);

    SECTION("untrusted issuer")
    {
        auto       clientContext = MakeClientContext(false);
        auto       serverContext = MakeServerContext(Credentials("server.pem", "server-key-pkcs8.pem"));
        auto       streams       = MakeStreams(clientContext, serverContext);
        const auto result        = RunHandshake(scheduler, *streams.client, *streams.server);
        REQUIRE(result.client.IsDomainError());
        CHECK(result.client.DomainError().category == NGIN::Net::TLS::TlsErrorCategory::Certificate);
    }

    SECTION("expired certificate")
    {
        auto       clientContext = MakeClientContext();
        auto       serverContext = MakeServerContext(Credentials("expired-server.pem", "server-key-pkcs8.pem"));
        auto       streams       = MakeStreams(clientContext, serverContext);
        const auto result        = RunHandshake(scheduler, *streams.client, *streams.server);
        REQUIRE(result.client.IsDomainError());
        CHECK(result.client.DomainError().category == NGIN::Net::TLS::TlsErrorCategory::Certificate);
    }

    SECTION("hostname mismatch")
    {
        auto       clientContext = MakeClientContext();
        auto       serverContext = MakeServerContext(Credentials("server.pem", "server-key-pkcs8.pem"));
        auto       streams       = MakeStreams(clientContext, serverContext, "wrong.example");
        const auto result        = RunHandshake(scheduler, *streams.client, *streams.server);
        REQUIRE(result.client.IsDomainError());
        CHECK(result.client.DomainError().category == NGIN::Net::TLS::TlsErrorCategory::Hostname);
        CHECK(result.client.DomainError().code == NGIN::Net::TLS::TlsErrorCode::HostnameMismatch);
    }
}

TEST_CASE("TLS mutual authentication and required ALPN policies are enforced", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        SKIP("OpenSSL TLS provider is disabled");
    }
    NGIN::Execution::ThreadPoolScheduler scheduler(4);

    SECTION("required client certificate succeeds")
    {
        auto clientContext = MakeClientContext(true, Credentials("client.pem", "client-key-pkcs8.pem"));
        auto serverContext = MakeServerContext(
                Credentials("server.pem", "server-key-pkcs8.pem"),
                NGIN::Net::TLS::TlsClientAuthentication::Required);
        auto       streams = MakeStreams(clientContext, serverContext);
        const auto result  = RunHandshake(scheduler, *streams.client, *streams.server);
        CHECK(result.client.Succeeded());
        CHECK(result.server.Succeeded());
        CHECK(streams.server->PeerCertificate().has_value());
    }

    SECTION("missing client certificate fails")
    {
        auto clientContext = MakeClientContext();
        auto serverContext = MakeServerContext(
                Credentials("server.pem", "server-key-pkcs8.pem"),
                NGIN::Net::TLS::TlsClientAuthentication::Required);
        auto       streams = MakeStreams(clientContext, serverContext);
        const auto result  = RunHandshake(scheduler, *streams.client, *streams.server);
        CHECK(result.server.IsDomainError());
    }

    SECTION("required ALPN overlap fails")
    {
        auto clientContext = MakeClientContext(true, std::nullopt, {"client-only"}, true);
        auto serverContext = MakeServerContext(
                Credentials("server.pem", "server-key-pkcs8.pem"),
                NGIN::Net::TLS::TlsClientAuthentication::None,
                {"server-only"},
                true);
        auto       streams = MakeStreams(clientContext, serverContext);
        const auto result  = RunHandshake(scheduler, *streams.client, *streams.server);
        CHECK((result.client.IsDomainError() || result.server.IsDomainError()));
    }
}

TEST_CASE("TLS handshake distinguishes caller cancellation and timeout", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        SKIP("OpenSSL TLS provider is disabled");
    }
    NGIN::Net::TLS::TlsClientContextOptions options;
    options.verification = NGIN::Net::TLS::TlsPeerVerification::Disabled;
    auto context         = NGIN::Net::TLS::TlsContext::CreateClient(std::move(options));
    REQUIRE(context.HasValue());
    NGIN::Execution::ThreadPoolScheduler scheduler(3);
    NGIN::Async::TaskContext             ctx(scheduler);

    SECTION("caller cancellation")
    {
        auto stream = NGIN::Net::TLS::TlsStream::CreateClient(
                std::make_unique<BlockingByteStream>(),
                context.Value(),
                {.serverName = "localhost", .verificationName = {}, .allowTruncatedEof = false});
        REQUIRE(stream.HasValue());
        NGIN::Async::CancellationSource source;
        source.Cancel();
        const auto result = NGIN::Async::SyncWait(ctx, stream.Value()->HandshakeAsync(ctx, source.GetToken()));
        REQUIRE(result.IsDomainError());
        CHECK(result.DomainError().category == NGIN::Net::TLS::TlsErrorCategory::Cancellation);
    }

    SECTION("deadline")
    {
        auto stream = NGIN::Net::TLS::TlsStream::CreateClient(
                std::make_unique<BlockingByteStream>(),
                context.Value(),
                {.serverName = "localhost", .verificationName = {}, .allowTruncatedEof = false});
        REQUIRE(stream.HasValue());
        const auto result = NGIN::Async::SyncWait(
                ctx,
                stream.Value()->HandshakeAsync(
                        ctx,
                        {},
                        {.timeout = std::chrono::milliseconds(20)}));
        REQUIRE(result.IsDomainError());
        CHECK(result.DomainError().category == NGIN::Net::TLS::TlsErrorCategory::Timeout);
        CHECK(result.DomainError().code == NGIN::Net::TLS::TlsErrorCode::TimedOut);
    }
}

TEST_CASE("TLS reports EOF without close_notify as truncation", "[Net][TLS]")
{
    if (!NGIN::Net::TLS::TlsProviderAvailable())
    {
        SKIP("OpenSSL TLS provider is disabled");
    }
    auto                                 clientContext = MakeClientContext();
    auto                                 serverContext = MakeServerContext(Credentials("server.pem", "server-key-pkcs8.pem"));
    auto                                 streams       = MakeStreams(clientContext, serverContext);
    NGIN::Execution::ThreadPoolScheduler scheduler(4);
    const auto                           handshake = RunHandshake(scheduler, *streams.client, *streams.server);
    REQUIRE(handshake.client.Succeeded());
    REQUIRE(handshake.server.Succeeded());

    REQUIRE(streams.server->Close().HasValue());
    NGIN::Async::TaskContext  ctx(scheduler);
    std::array<NGIN::Byte, 8> buffer {};
    const auto                result = NGIN::Async::SyncWait(ctx, streams.client->ReadTlsAsync(ctx, buffer));
    REQUIRE(result.IsDomainError());
    CHECK(result.DomainError().code == NGIN::Net::TLS::TlsErrorCode::TruncatedStream);
}
