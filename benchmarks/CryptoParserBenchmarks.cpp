#include <NGIN/Benchmark.hpp>
#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Encoding/Pem.hpp>
#include <NGIN/Crypto/Tokens/Jwt.hpp>
#include <NGIN/Crypto/Tokens/Paseto.hpp>

#include <array>
#include <iostream>
#include <string_view>

namespace
{
    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(std::string_view text) noexcept
    {
        return {reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()};
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan Bytes(const std::array<NGIN::Byte, 11>& bytes) noexcept
    {
        return {bytes.data(), bytes.size()};
    }
}// namespace

int main()
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::Units::Milliseconds;

    constexpr std::string_view pem {
            "-----BEGIN CERTIFICATE-----\n"
            "AQIDBAUGBwgJCgsMDQ4PEA==\n"
            "-----END CERTIFICATE-----\n"};

    constexpr std::array<NGIN::Byte, 11> der {
            NGIN::Byte {0x30},
            NGIN::Byte {0x09},
            NGIN::Byte {0x02},
            NGIN::Byte {0x01},
            NGIN::Byte {0x01},
            NGIN::Byte {0x04},
            NGIN::Byte {0x02},
            NGIN::Byte {0xaa},
            NGIN::Byte {0xbb},
            NGIN::Byte {0x05},
            NGIN::Byte {0x00},
    };

    constexpr std::string_view jwt {
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJpc3MiOiJuZ2luIiwiYXVkIjpbImFwaSIsImNsaSJdLCJzdWIiOiIxMjMiLCJleHAiOjIwMDAwMDAwMDAsImlhdCI6MTcwMDAwMDAwMH0."
            "c2lnbmF0dXJl"};

    constexpr std::string_view paseto {
            "v4.public."
            "eyJkYXRhIjoidGhpcyBpcyBhIHNpZ25lZCBtZXNzYWdlIiwiZXhwIjoiMjAyMi0wMS0wMVQwMDowMDowMCswMDowMCJ9"
            "bg_XBBzds8lTZShVlwwKSgeKpLT3yukTw6JUz3W4h_ExsQV-P0V54zemZDcAxFaSeef1QlXEFtkqxT1ciiQEDA"};

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto blocks = NGIN::Crypto::Encoding::ParsePem(pem, {
                                                                    .allowedLabels   = {"CERTIFICATE"},
                                                                    .maxDecodedBytes = 1024,
                                                            });
        ctx.doNotOptimize(blocks.HasValue());
        ctx.stop();
    },
                        "Crypto PEM parse");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Crypto::Encoding::DerReader reader {Bytes(der)};
        auto                              sequence = reader.ReadElement();
        if (sequence.HasValue())
        {
            auto childReader = reader.EnterConstructed(sequence.Value());
            if (childReader.HasValue())
            {
                while (!childReader.Value().IsAtEnd())
                {
                    auto element = childReader.Value().ReadElement();
                    ctx.doNotOptimize(element.HasValue());
                    if (!element.HasValue())
                    {
                        break;
                    }
                }
            }
            ctx.doNotOptimize(childReader.HasValue());
        }
        ctx.doNotOptimize(sequence.HasValue());
        ctx.stop();
    },
                        "Crypto DER TLV walk");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto parsed = NGIN::Crypto::Tokens::ParseJwtCompact(jwt);
        ctx.doNotOptimize(parsed.HasValue());
        ctx.stop();
    },
                        "Crypto JWT compact parse");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto parsed = NGIN::Crypto::Tokens::ParsePasetoV4Public(paseto);
        ctx.doNotOptimize(parsed.HasValue());
        ctx.stop();
    },
                        "Crypto PASETO v4.public parse");

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
