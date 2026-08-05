#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Net/Resolver.hpp>

#include <chrono>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Net.Resolver resolves numeric loopback without external network access", "[Net][Resolver]")
{
    NGIN::Net::ResolveOptions options;
    options.family         = NGIN::Net::AddressFamily::V4;
    options.socketType     = NGIN::Net::ResolveSocketType::Stream;
    options.numericHost    = true;
    options.numericService = true;

    const auto result = NGIN::Net::Resolve("127.0.0.1", "8080", options);
    REQUIRE(result.HasValue());
    REQUIRE_FALSE(result.Value().empty());
    for (const auto& address: result.Value())
    {
        CHECK(address.endpoint.address == NGIN::Net::IpAddress::LoopbackV4());
        CHECK(address.endpoint.port == 8080);
        CHECK(address.socketType == NGIN::Net::ResolveSocketType::Stream);
    }
}

TEST_CASE("Net.Resolver applies address-family filters and preserves resolver diagnostics", "[Net][Resolver]")
{
    NGIN::Net::ResolveOptions v6;
    v6.family           = NGIN::Net::AddressFamily::V6;
    v6.socketType       = NGIN::Net::ResolveSocketType::Datagram;
    v6.numericHost      = true;
    v6.numericService   = true;
    const auto v6Result = NGIN::Net::Resolve("::1", "53", v6);
    REQUIRE(v6Result.HasValue());
    REQUIRE_FALSE(v6Result.Value().empty());
    CHECK(v6Result.Value().front().endpoint.address == NGIN::Net::IpAddress::LoopbackV6());
    CHECK(v6Result.Value().front().socketType == NGIN::Net::ResolveSocketType::Datagram);

    NGIN::Net::ResolveOptions numeric;
    numeric.numericHost = true;
    const auto missing  = NGIN::Net::Resolve("not-a-numeric-address", "80", numeric);
    REQUIRE_FALSE(missing.HasValue());
    CHECK(missing.Error().network.code == NGIN::Net::NetErrorCode::NameNotFound);
    CHECK(missing.Error().resolverCode != 0);
    CHECK_FALSE(missing.Error().diagnostic.empty());

    const auto invalid = NGIN::Net::Resolve("", "");
    REQUIRE_FALSE(invalid.HasValue());
    CHECK(invalid.Error().network.code == NGIN::Net::NetErrorCode::InvalidArgument);
}

TEST_CASE("Net.Resolver async work uses its explicit driver and caller executor", "[Net][Resolver]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::Async::TaskContext             context {scheduler};
    NGIN::Net::ResolverDriver            driver;
    NGIN::Net::ResolveOptions            options;
    options.family         = NGIN::Net::AddressFamily::V4;
    options.socketType     = NGIN::Net::ResolveSocketType::Stream;
    options.numericHost    = true;
    options.numericService = true;

    auto completion = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "127.0.0.1", "443", options));
    REQUIRE(completion.HasValue());
    REQUIRE_FALSE(completion.Value().empty());
    CHECK(completion.Value().front().endpoint.port == 443);
}

TEST_CASE("Net.Resolver async cancellation and timeout complete without waiting for lookup", "[Net][Resolver]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::Async::TaskContext             context {scheduler};
    NGIN::Net::ResolverDriver            driver;

    NGIN::Async::CancellationSource source;
    source.Cancel();
    auto canceled = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "localhost", "80", {}, source.GetToken()));
    CHECK(canceled.IsCanceled());

    NGIN::Net::ResolveOptions timedOptions;
    timedOptions.timeout = std::chrono::milliseconds {0};
    auto timedOut        = NGIN::Async::SyncWait(
            context,
            NGIN::Net::ResolveAsync(context, driver, "localhost", "80", timedOptions));
    REQUIRE(timedOut.IsDomainError());
    CHECK(timedOut.DomainError().network.code == NGIN::Net::NetErrorCode::TimedOut);
}
