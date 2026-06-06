#include <NGIN/Serialization/Archive.hpp>
#include <NGIN/Serialization/JSON/JsonArchive.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/XML/XmlArchive.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Serialization public headers compile together")
{
    SUCCEED();
}
