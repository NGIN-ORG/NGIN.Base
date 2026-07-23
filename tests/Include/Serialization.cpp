#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/Core/SourceMap.hpp>
#include <NGIN/Serialization/JSON/JsonBuilder.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/JSON/JsonEventParser.hpp>
#include <NGIN/Serialization/JSON/JsonStreamWriter.hpp>
#include <NGIN/Serialization/JSON/JsonWriter.hpp>
#include <NGIN/Serialization/XML/XmlBuilder.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlEventParser.hpp>
#include <NGIN/Serialization/XML/XmlStreamWriter.hpp>
#include <NGIN/Serialization/XML/XmlWriter.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Serialization public headers compile together")
{
    SUCCEED();
}
