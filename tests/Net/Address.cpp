#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/IpAddress.hpp>

#include <array>
#include <string_view>
#include <unordered_set>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Net.IpAddress parses and formats strict IPv4", "[Net][Address]")
{
    const auto parsed = NGIN::Net::IpAddress::Parse("192.0.2.17");
    REQUIRE(parsed.HasValue());
    CHECK(parsed.Value().IsV4());
    CHECK(parsed.Value().ToString() == "192.0.2.17");

    for (const std::string_view invalid: {
                 "",
                 "1.2.3",
                 "1.2.3.4.5",
                 "256.0.0.1",
                 "01.2.3.4",
                 "1.2.3.-1",
                 "1.2.3.a",
         })
    {
        INFO(invalid);
        CHECK_FALSE(NGIN::Net::IpAddress::Parse(invalid).HasValue());
    }
}

TEST_CASE("Net.IpAddress parses and canonically formats IPv6", "[Net][Address]")
{
    const std::array cases {
            std::pair {std::string_view {"::"}, std::string_view {"::"}},
            std::pair {std::string_view {"::1"}, std::string_view {"::1"}},
            std::pair {std::string_view {"2001:0db8:0:0:0:ff00:0042:8329"},
                       std::string_view {"2001:db8::ff00:42:8329"}},
            std::pair {std::string_view {"2001:db8:0:1:0:0:0:1"},
                       std::string_view {"2001:db8:0:1::1"}},
            std::pair {std::string_view {"::ffff:192.0.2.1"},
                       std::string_view {"::ffff:192.0.2.1"}},
    };
    for (const auto& [input, canonical]: cases)
    {
        INFO(input);
        const auto parsed = NGIN::Net::IpAddress::Parse(input);
        REQUIRE(parsed.HasValue());
        CHECK(parsed.Value().IsV6());
        CHECK(parsed.Value().ToString() == canonical);
        CHECK(NGIN::Net::IpAddress::Parse(canonical).Value() == parsed.Value());
    }

    for (const std::string_view invalid: {
                 ":",
                 "1:2:3:4:5:6:7",
                 "1:2:3:4:5:6:7:8:9",
                 "1::2::3",
                 "12345::1",
                 "::ffff:999.0.0.1",
                 "fe80::1%3",
         })
    {
        INFO(invalid);
        CHECK_FALSE(NGIN::Net::IpAddress::Parse(invalid).HasValue());
    }
}

TEST_CASE("Net.IpAddress supports allocation-free formatting and hashing", "[Net][Address]")
{
    const auto           address = NGIN::Net::IpAddress::Parse("2001:db8::1").Value();
    std::array<char, 64> output {};
    NGIN::UIntSize       written = 0;
    REQUIRE(address.TryFormat(output, written));
    CHECK(std::string_view {output.data(), written} == "2001:db8::1");

    std::array<char, 4> tooSmall {};
    written = 99;
    CHECK_FALSE(address.TryFormat(tooSmall, written));
    CHECK(written == 0);

    std::unordered_set<NGIN::Net::IpAddress, NGIN::Net::IpAddressHash> addresses;
    addresses.insert(address);
    CHECK(addresses.contains(NGIN::Net::IpAddress::Parse("2001:0db8:0:0:0:0:0:1").Value()));
}

TEST_CASE("Net.Endpoint parses ports, bracketed IPv6, and numeric scopes", "[Net][Address]")
{
    const auto v4 = NGIN::Net::Endpoint::Parse("127.0.0.1:8080");
    REQUIRE(v4.HasValue());
    CHECK(v4.Value().ToString() == "127.0.0.1:8080");

    const auto v6 = NGIN::Net::Endpoint::Parse("[fe80::1%7]:443");
    REQUIRE(v6.HasValue());
    CHECK(v6.Value().address.IsV6());
    CHECK(v6.Value().scopeId == 7);
    CHECK(v6.Value().port == 443);
    CHECK(v6.Value().ToString() == "[fe80::1%7]:443");
    CHECK(NGIN::Net::Endpoint::Parse(v6.Value().ToString()).Value() == v6.Value());

    for (const std::string_view invalid: {
                 "127.0.0.1",
                 "127.0.0.1:",
                 "127.0.0.1:65536",
                 "::1:80",
                 "[::1]80",
                 "[::1]:abc",
                 "[127.0.0.1]:80",
                 "[fe80::1%eth0]:80",
         })
    {
        INFO(invalid);
        CHECK_FALSE(NGIN::Net::Endpoint::Parse(invalid).HasValue());
    }

    std::unordered_set<NGIN::Net::Endpoint, NGIN::Net::EndpointHash> endpoints;
    endpoints.insert(v6.Value());
    CHECK(endpoints.contains(NGIN::Net::Endpoint::Parse("[fe80:0:0:0:0:0:0:1%7]:443").Value()));
}
