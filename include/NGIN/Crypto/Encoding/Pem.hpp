#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace NGIN::Crypto::Encoding
{
    /// @brief One decoded PEM block with its textual label.
    struct PemBlock
    {
        std::string label;
        ByteBuffer  decoded;
    };

    /// @brief Strict PEM parser options.
    struct PemParseOptions
    {
        /// @brief Optional allowlist of accepted PEM labels. An empty list accepts any syntactically valid label.
        std::initializer_list<std::string_view> allowedLabels {};

        /// @brief Maximum decoded bytes per PEM block.
        NGIN::UIntSize maxDecodedBytes {1u << 20};

        /// @brief Whether more than one PEM block may be present in the input.
        bool allowMultipleBlocks {true};
    };

    using PemBlocks = NGIN::Containers::Vector<PemBlock>;

    /// @brief Parses strict RFC 7468-style PEM blocks and returns decoded bytes without interpreting their contents.
    [[nodiscard]] CryptoExpected<PemBlocks> ParsePem(std::string_view input, PemParseOptions options = {});
}// namespace NGIN::Crypto::Encoding
