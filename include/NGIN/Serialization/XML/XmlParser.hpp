#pragma once

#include <NGIN/IO/IByteReader.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <span>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief XML parsing configuration.
    struct XmlParseOptions
    {
        bool     normalizeWhitespace {false};
        bool     trackLocation {false};
        bool     decodeEntities {false};
        bool     allowComments {true};
        bool     allowProcessingInstructions {true};
        bool     inSitu {false};
        bool     precomputeContainerSizes {true};
        bool     internNames {false};
        UIntSize maxDepth {256};
        UIntSize arenaBytes {0};
    };

    /// @brief Streaming XML event interface.
    class NGIN_BASE_API XmlReader
    {
    public:
        virtual ~XmlReader() = default;

        virtual bool OnStartElement(std::string_view name) noexcept                      = 0;
        virtual bool OnAttribute(std::string_view name, std::string_view value) noexcept = 0;
        virtual bool OnText(std::string_view text) noexcept                              = 0;
        virtual bool OnCData(std::string_view text) noexcept                             = 0;
        virtual bool OnEndElement(std::string_view name) noexcept                        = 0;
    };

    /// @brief XML parser entry points for DOM and streaming usage.
    class NGIN_BASE_API XmlParser
    {
    public:
        static NGIN::Utilities::Expected<XmlDocument, ParseError>
        Parse(std::span<const NGIN::Byte> input, const XmlParseOptions& options = {});

        static NGIN::Utilities::Expected<XmlDocument, ParseError>
        Parse(std::span<NGIN::Byte> input, const XmlParseOptions& options = {});

        static NGIN::Utilities::Expected<XmlDocument, ParseError>
        Parse(std::string_view input, const XmlParseOptions& options = {});

        static NGIN::Utilities::Expected<XmlDocument, ParseError>
        Parse(NGIN::IO::IByteReader& reader, const XmlParseOptions& options = {});

        static NGIN::Utilities::Expected<void, ParseError>
        Parse(XmlReader& reader, std::span<const NGIN::Byte> input, const XmlParseOptions& options = {});

        /// @brief Decode XML entities using the document arena.
        static NGIN::Utilities::Expected<std::string_view, ParseError>
        DecodeEntities(XmlDocument& document, std::string_view input);

        /// @brief Normalize XML whitespace using the document arena.
        static NGIN::Utilities::Expected<std::string_view, ParseError>
        NormalizeWhitespace(XmlDocument& document, std::string_view input);

        /// @brief Decode entities (and optionally normalize whitespace) using the document arena.
        static NGIN::Utilities::Expected<std::string_view, ParseError>
        DecodeText(XmlDocument& document, std::string_view input, bool normalizeWhitespace = false);
    };
}// namespace NGIN::Serialization
