#pragma once

#include <NGIN/IO/IByteReader.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <span>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief JSON parsing configuration.
    struct JsonParseOptions
    {
        bool     allowComments {false};
        bool     allowTrailingCommas {false};
        bool     trackLocation {false};
        bool     inSitu {false};
        UIntSize maxDepth {256};
        UIntSize arenaBytes {0};
    };

    /// @brief Streaming JSON event interface (SAX-style).
    class NGIN_BASE_API JsonReader
    {
    public:
        virtual ~JsonReader() = default;

        virtual bool OnNull() noexcept                         = 0;
        virtual bool OnBool(bool value) noexcept               = 0;
        virtual bool OnNumber(F64 value) noexcept              = 0;
        virtual bool OnString(std::string_view value) noexcept = 0;
        virtual bool OnStartObject() noexcept                  = 0;
        virtual bool OnKey(std::string_view key) noexcept      = 0;
        virtual bool OnEndObject() noexcept                    = 0;
        virtual bool OnStartArray() noexcept                   = 0;
        virtual bool OnEndArray() noexcept                     = 0;
    };

    /// @brief JSON parser entry points for DOM and streaming usage.
    class NGIN_BASE_API JsonParser
    {
    public:
        static NGIN::Utilities::Expected<JsonDocument, ParseError>
        Parse(std::span<const NGIN::Byte> input, const JsonParseOptions& options = {});

        static NGIN::Utilities::Expected<JsonDocument, ParseError>
        Parse(std::span<NGIN::Byte> input, const JsonParseOptions& options = {});

        static NGIN::Utilities::Expected<JsonDocument, ParseError>
        Parse(std::string_view input, const JsonParseOptions& options = {});

        static NGIN::Utilities::Expected<JsonDocument, ParseError>
        Parse(NGIN::IO::IByteReader& reader, const JsonParseOptions& options = {});

        static NGIN::Utilities::Expected<void, ParseError>
        Parse(JsonReader& reader, std::span<const NGIN::Byte> input, const JsonParseOptions& options = {});

        static NGIN::Utilities::Expected<void, ParseError>
        Parse(JsonReader& reader, std::span<NGIN::Byte> input, const JsonParseOptions& options = {});
    };
}// namespace NGIN::Serialization
