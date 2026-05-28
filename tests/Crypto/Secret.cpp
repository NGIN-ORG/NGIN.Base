#include <NGIN/Crypto/Memory/Secret.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

TEST_CASE("Secret is move-only typed storage", "[Crypto][Secret]")
{
    using Key = NGIN::Crypto::Memory::FixedSecret<4>;

    static_assert(!std::is_copy_constructible_v<Key>);
    static_assert(!std::is_copy_assignable_v<Key>);
    static_assert(std::is_move_constructible_v<Key>);
    static_assert(std::is_move_assignable_v<Key>);

    NGIN::Crypto::FixedBytes<4> bytes {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0x03},
            NGIN::Byte {0x04},
    };

    auto key = Key::FromValue(bytes);

    REQUIRE(key.View()[0] == NGIN::Byte {0x01});
    REQUIRE(key.View()[3] == NGIN::Byte {0x04});
    REQUIRE(key.Bytes().size() == 4);
}

TEST_CASE("Secret move transfers value and wipes moved-from storage", "[Crypto][Secret]")
{
    using Key = NGIN::Crypto::Memory::FixedSecret<4>;

    NGIN::Crypto::FixedBytes<4> bytes {
            NGIN::Byte {0xaa},
            NGIN::Byte {0xbb},
            NGIN::Byte {0xcc},
            NGIN::Byte {0xdd},
    };

    auto first  = Key::FromValue(bytes);
    auto second = std::move(first);

    REQUIRE(second.View()[0] == NGIN::Byte {0xaa});
    REQUIRE(second.View()[3] == NGIN::Byte {0xdd});

    for (auto byte: first.Bytes())
    {
        REQUIRE(byte == NGIN::Byte {0});
    }
}

TEST_CASE("FixedSecret Generate fills fixed-size secret bytes", "[Crypto][Secret]")
{
    using Key = NGIN::Crypto::Memory::FixedSecret<16>;

    auto key = Key::Generate();

    REQUIRE(key.HasValue());
    REQUIRE(key.Value().Bytes().size() == 16);
}

TEST_CASE("DynamicSecret names secure dynamic byte storage", "[Crypto][Secret]")
{
    NGIN::Crypto::Memory::DynamicSecret secret {8};

    REQUIRE(secret.Size() == 8);
    REQUIRE_FALSE(secret.Empty());
}
